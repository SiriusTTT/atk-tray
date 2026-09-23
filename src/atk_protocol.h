// atk_protocol.h — COMPX / ATK mouse HID protocol.
//
// Verified against an "ATK Mouse 8K Dongle" (VID 0x373B / PID 0x101B) on the
// col05 vendor collection (usage page 0xFF02, usage 0x0002).
//
// Wire format: 17 bytes — report ID 0x08 followed by a 16-byte frame.
//
//   payload  wire    field
//   b[0]     [1]     command ID          (also the response correlation byte)
//   b[1]     [2]     status              (request: 0; response: 0 = OK, 1 = error)
//   b[2..3]  [3..4]  EEPROM address, big-endian
//   b[4]     [5]     data length         (GetEEPROM: number of bytes to read)
//   b[5..14] [6..15] data
//   b[15]    [16]    checksum
//
//   checksum = (0x55 - ((0x08 + sum(b[0..14])) & 0xFF)) & 0xFF
//
// The leading 0x08 in the checksum sum IS the report ID — it is not a typo.
//
// Behaviour worth knowing:
//   * GetEEPROM with data length 0 is rejected (status 1). data length is the
//     number of bytes to read.
//   * Commands that are forwarded to the mouse (battery, EEPROM, CID/MID,
//     version) get NO RESPONSE while the mouse is asleep. Commands the dongle
//     answers itself (online state, dongle light, pair result) always reply.
//     "No response" therefore means "mouse asleep", not "protocol error".
//   * The ATK HUB background service may hold the same collection open, so
//     CreateFile can fail transiently; callers should retry.

#ifndef ATK_PROTOCOL_H
#define ATK_PROTOCOL_H

#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdint.h>
#include <string.h>

namespace atk {

static const uint8_t REPORT_ID = 0x08;

enum CommandId : uint8_t {
    CMD_DOWNLOAD_DATA          = 0x01,
    CMD_GET_WIRELESS_ONLINE    = 0x03,
    CMD_GET_BATTERY_LEVEL      = 0x04,
    CMD_GET_WIRELESS_PAIR      = 0x06,
    CMD_SET_EEPROM             = 0x07,
    CMD_GET_EEPROM             = 0x08,
    CMD_GET_CURRENT_CONFIG     = 0x0E,
    CMD_GET_MOUSE_CID_MID      = 0x10,
    CMD_GET_MOUSE_VERSION      = 0x12,
    CMD_GET_FAR_DISTANCE_MODE  = 0x17,
    CMD_GET_DONGLE_LIGHT_MODE  = 0x19,
};

enum EepromAddress : uint16_t {
    EEP_REPORT_RATE   = 0,    // 10-byte block; see DpiConfig
    EEP_DPI_BLOCK_1   = 12,   // 8-byte block; 2 DPI slots
    EEP_DPI_BLOCK_2   = 20,
    EEP_DPI_BLOCK_3   = 28,
    EEP_DPI_BLOCK_4   = 36,
    EEP_DPI_COLOR_1   = 44,
    EEP_DPI_COLOR_2   = 52,
    EEP_DPI_COLOR_3   = 60,
    EEP_DPI_COLOR_4   = 68,
};

// Values arrive as (value, 0x55 - value) pairs for the single-byte settings.
static inline uint8_t Crc8(uint8_t v) { return (uint8_t)(0x55 - v); }

static inline uint8_t Checksum(const uint8_t frame[16]) {
    unsigned sum = REPORT_ID;
    for (int i = 0; i < 15; ++i) sum += frame[i];
    return (uint8_t)(0x55 - (sum & 0xFF));
}

// Build a 16-byte command frame. `data` may be null. Returns the frame length.
static inline void BuildCommand(uint8_t out[16], uint8_t id, uint8_t status,
                                uint16_t eepromAddr, const uint8_t* data, int dataLen) {
    memset(out, 0, 16);
    out[0] = id;
    out[1] = status;
    out[2] = (uint8_t)(eepromAddr >> 8);
    out[3] = (uint8_t)(eepromAddr & 0xFF);
    out[4] = (uint8_t)dataLen;
    if (data && dataLen > 0) {
        int n = dataLen > 10 ? 10 : dataLen;
        memcpy(out + 5, data, n);
    }
    out[15] = Checksum(out);
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

class Device {
public:
    Device() : h_(INVALID_HANDLE_VALUE) {}
    ~Device() { Close(); }

    bool IsOpen() const { return h_ != INVALID_HANDLE_VALUE; }

    bool Open(const WCHAR* path) {
        Close();
        // Overlapped so reads can time out instead of blocking the worker thread.
        h_ = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, NULL);
        return IsOpen();
    }

    void Close() {
        if (IsOpen()) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    }

    // Send a command and wait for its response. `resp` receives the raw 17-byte
    // report (report ID at resp[0], so the payload starts at resp[1]).
    // Returns false on write failure or timeout — a timeout normally means the
    // mouse is asleep, not that the protocol is wrong.
    bool Transact(const uint8_t cmd[16], uint8_t resp[17], DWORD timeoutMs) {
        if (!IsOpen()) return false;

        uint8_t wire[17];
        wire[0] = REPORT_ID;
        memcpy(wire + 1, cmd, 16);

        DWORD written = 0;
        OVERLAPPED wov = {0};
        wov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!wov.hEvent) return false;

        bool ok = false;
        if (!WriteFile(h_, wire, sizeof(wire), &written, &wov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(wov.hEvent, timeoutMs) == WAIT_OBJECT_0) {
                    DWORD n = 0;
                    ok = GetOverlappedResult(h_, &wov, &n, FALSE) && n == sizeof(wire);
                } else {
                    CancelIo(h_);
                }
            }
        } else {
            ok = (written == sizeof(wire));
        }
        CloseHandle(wov.hEvent);
        if (!ok) return false;

        // Responses to other outstanding commands and unsolicited pushes
        // (payload byte 0 == 0) interleave with ours; match on the command ID.
        for (int attempt = 0; attempt < 6; ++attempt) {
            memset(resp, 0, 17);
            DWORD br = 0;
            OVERLAPPED rov = {0};
            rov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
            if (!rov.hEvent) return false;

            bool readOk = false;
            if (!ReadFile(h_, resp, 17, &br, &rov)) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(rov.hEvent, timeoutMs) == WAIT_OBJECT_0) {
                        readOk = GetOverlappedResult(h_, &rov, &br, FALSE) != 0;
                    } else {
                        CancelIo(h_);
                    }
                }
            } else {
                readOk = true;
            }
            CloseHandle(rov.hEvent);

            if (readOk && br > 0 && resp[0] == REPORT_ID && resp[1] == cmd[0]) return true;
            if (readOk && br > 0 && resp[1] == 0) continue;   // unsolicited push
        }
        return false;
    }

    // Convenience wrapper: returns the payload (b[0..15]) on success.
    bool Query(uint8_t id, uint16_t addr, int dataLen, uint8_t payloadOut[16]) {
        uint8_t cmd[16], resp[17];
        BuildCommand(cmd, id, 0x00, addr, NULL, dataLen);
        if (!Transact(cmd, resp, 400)) return false;
        if (resp[2] != 0x00) return false;                     // status != OK
        memcpy(payloadOut, resp + 1, 16);
        return true;
    }

private:
    HANDLE h_;
};

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

struct Battery {
    int level;       // percent, 0-100
    int charging;    // 1 = charging, 0 = discharging
    int voltageMv;   // millivolts
};

struct DpiConfig {
    uint8_t reportRateCode;
    uint8_t maxDpi;        // total number of selectable DPI slots
    uint8_t currentDpi;    // active slot index
    uint8_t bhop;
    uint8_t buttonMode;
};

// Dongle answers this itself, so it works even while the mouse is asleep.
static inline bool QueryOnline(Device& dev, bool* online) {
    uint8_t p[16];
    if (!dev.Query(CMD_GET_WIRELESS_ONLINE, 0, 0, p)) return false;
    *online = (p[5] != 0);
    return true;
}

static inline bool QueryBattery(Device& dev, Battery* out) {
    uint8_t p[16];
    if (!dev.Query(CMD_GET_BATTERY_LEVEL, 0, 0, p)) return false;
    out->level      = p[5];
    out->charging   = p[6];
    out->voltageMv  = (p[7] << 8) | p[8];   // big-endian
    return true;
}

static inline bool QueryDpiConfig(Device& dev, DpiConfig* out) {
    uint8_t p[16];
    if (!dev.Query(CMD_GET_EEPROM, EEP_REPORT_RATE, 10, p)) return false;
    out->reportRateCode = p[5];
    out->maxDpi         = p[7];
    out->currentDpi     = p[9];
    out->bhop           = p[11];
    out->buttonMode     = p[13];
    return true;
}

// Decode one 4-byte DPI slot record {xDpi, yDpi, dpiEx, crc}.
// Codec is the PAW3950Ultra branch used by the ATK HUB app; it is also the
// app's fallback for COMPX mice whose sensor it cannot identify.
static inline void DecodeDpiSlot(uint8_t xRaw, uint8_t yRaw, uint8_t dpiEx,
                                 int* outX, int* outY) {
    int x = xRaw | ((((dpiEx & 0x0C) >> 2)) << 8);
    int y = yRaw | ((((dpiEx & 0xC0) >> 6)) << 8);
    int rx = (dpiEx & 0x02) ? (50 * x + 10050) : (10 * (x + 1));
    int ry = (dpiEx & 0x20) ? (50 * y + 10050) : (10 * (y + 1));
    if (dpiEx & 0x01) rx *= 2;
    if (dpiEx & 0x10) ry *= 2;
    *outX = rx;
    *outY = ry;
}

// Read all 8 DPI slots. Returns the number filled (the dongle exposes 4 blocks
// of 2 slots). Slots beyond maxDpi are still returned but are usually repeats.
static inline int QueryDpiSlots(Device& dev, int xOut[8], int yOut[8]) {
    static const uint16_t kBlocks[4] = {
        EEP_DPI_BLOCK_1, EEP_DPI_BLOCK_2, EEP_DPI_BLOCK_3, EEP_DPI_BLOCK_4
    };
    int count = 0;
    for (int b = 0; b < 4; ++b) {
        uint8_t p[16];
        if (!dev.Query(CMD_GET_EEPROM, kBlocks[b], 8, p)) continue;
        for (int s = 0; s < 2; ++s) {
            int off = 5 + 4 * s;
            DecodeDpiSlot(p[off], p[off + 1], p[off + 2], &xOut[count], &yOut[count]);
            ++count;
        }
    }
    return count;
}

static inline bool QueryCidMid(Device& dev, uint8_t* cid, uint8_t* mid) {
    uint8_t p[16];
    if (!dev.Query(CMD_GET_MOUSE_CID_MID, 0, 0, p)) return false;
    *cid = p[5];
    *mid = p[6];
    return true;
}

// Report-rate code -> Hz. 0 means unknown.
static inline int ReportRateHz(uint8_t code) {
    switch (code) {
        case 0x01: return 1000;
        case 0x02: return 500;
        case 0x04: return 250;
        case 0x08: return 125;
        case 0x10: return 2000;
        case 0x20: return 4000;
        case 0x40: return 8000;
        default:   return 0;
    }
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

static const uint16_t ATK_VID       = 0x373B;
static const uint16_t ATK_PID_8K    = 0x101B;
static const uint16_t ATK_PID_OTHER = 0x104A;

// The command channel is the 17-byte, report-ID-8 vendor collection. Prefer a
// path that names it explicitly; otherwise take the only collection with 17-byte
// input AND output reports, which is what distinguishes it from the others.
static inline bool PathLooksLikeCommandChannel(const WCHAR* lowerPath) {
    return wcsstr(lowerPath, L"col05") != NULL;
}

static inline bool FindCommandChannel(WCHAR* outPath, int outPathChars) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA ifData = {0};
    ifData.cbSize = sizeof(ifData);

    bool found = false;
    WCHAR fallback[MAX_PATH] = {0};

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &ifData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        if (reqSize == 0) continue;

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)malloc(reqSize);
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, NULL, NULL)) {
            WCHAR lower[MAX_PATH];
            StringCchCopyW(lower, MAX_PATH, detail->DevicePath);
            _wcslwr_s(lower, MAX_PATH);

            if (wcsstr(lower, L"vid_373b") &&
                (wcsstr(lower, L"pid_101b") || wcsstr(lower, L"pid_104a"))) {

                if (PathLooksLikeCommandChannel(lower)) {
                    StringCchCopyW(outPath, outPathChars, detail->DevicePath);
                    found = true;
                } else if (fallback[0] == 0) {
                    // Confirm the shape before trusting a path that isn't col05.
                    HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                           OPEN_EXISTING, 0, NULL);
                    if (h != INVALID_HANDLE_VALUE) {
                        PHIDP_PREPARSED_DATA ppd = NULL;
                        HIDP_CAPS caps = {0};
                        if (HidD_GetPreparsedData(h, &ppd)) {
                            if (HidP_GetCaps(ppd, &caps) == HIDP_STATUS_SUCCESS &&
                                caps.InputReportByteLength == 17 &&
                                caps.OutputReportByteLength == 17) {
                                StringCchCopyW(fallback, MAX_PATH, detail->DevicePath);
                            }
                            HidD_FreePreparsedData(ppd);
                        }
                        CloseHandle(h);
                    }
                }
            }
        }
        free(detail);
        if (found) break;
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    if (!found && fallback[0]) {
        StringCchCopyW(outPath, outPathChars, fallback);
        found = true;
    }
    return found;
}

} // namespace atk

#endif // ATK_PROTOCOL_H
