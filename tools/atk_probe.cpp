// ATK mouse protocol probe — verifies the reverse-engineered Compx/ATK HID protocol
// against a real device (VID 0x373B).
//
// Protocol (from libatk-rs): report ID 0x08, 16-byte command:
//   [0]    Command ID
//   [1]    Status
//   [2..3] EEPROM address (big-endian)
//   [4]    valid data length
//   [5..14]data payload
//   [15]   checksum = 0x55 - ((0x08 + sum(all bytes 0..14)) & 0xFF)
//
// Build:
//   g++ -static -o atk_probe.exe atk_probe.cpp -lsetupapi -lhid

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const uint8_t REPORT_ID = 0x08;

// Command IDs
enum {
    CMD_GET_WIRELESS_ONLINE = 0x03,
    CMD_GET_BATTERY_LEVEL   = 0x04,
    CMD_SET_EEPROM          = 0x07,
    CMD_GET_EEPROM          = 0x08,
    CMD_GET_MOUSE_VERSION   = 0x12,
    CMD_GET_CURRENT_CONFIG  = 0x0E,
};

// EEPROM addresses
enum {
    EEP_REPORT_RATE  = 0x0000,
    EEP_MAX_DPI      = 0x0002,
    EEP_CURRENT_DPI  = 0x0004,
    EEP_DPI_PAIR1    = 0x000C,
    EEP_DPI_PAIR3    = 0x0014,
};

static uint8_t checksum16(const uint8_t* cmd) {
    unsigned sum = REPORT_ID;
    for (int i = 0; i < 15; i++) sum += cmd[i];
    return (uint8_t)(0x55u - (sum & 0xFF));
}

// Build a 16-byte command (checksum filled in at [15]).
static void build_cmd(uint8_t* out, uint8_t id, uint8_t status,
                      uint16_t eeprom_addr, const uint8_t* data, int dataLen) {
    memset(out, 0, 16);
    out[0] = id;
    out[1] = status;
    out[2] = (uint8_t)(eeprom_addr >> 8);
    out[3] = (uint8_t)(eeprom_addr & 0xFF);
    out[4] = (uint8_t)dataLen;
    if (data && dataLen > 0) memcpy(out + 5, data, dataLen > 10 ? 10 : dataLen);
    out[15] = checksum16(out);
}

static void hexdump(const char* label, const uint8_t* p, int n) {
    printf("%s", label);
    for (int i = 0; i < n; i++) printf("%02X ", p[i]);
    printf("\n");
    fflush(stdout);
}

// Send a 16-byte command (prefixed with the report ID) and read the response.
// Returns bytes read (0 on timeout/failure); resp holds the raw response
// including the report ID at resp[0].
static int send_and_read(HANDLE h, const uint8_t* cmd, uint8_t* resp, int respSize) {
    uint8_t wbuf[17];
    wbuf[0] = REPORT_ID;
    memcpy(wbuf + 1, cmd, 16);

    DWORD written = 0;
    OVERLAPPED wov = {0};
    wov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(h, wbuf, sizeof(wbuf), &written, &wov);
    if (!ok) {
        if (GetLastError() != ERROR_IO_PENDING) {
            printf("    WriteFile failed: %lu\n", GetLastError());
            CloseHandle(wov.hEvent);
            return 0;
        }
        if (WaitForSingleObject(wov.hEvent, 500) != WAIT_OBJECT_0) {
            CancelIo(h); CloseHandle(wov.hEvent); return 0;
        }
        GetOverlappedResult(h, &wov, &written, FALSE);
    }
    CloseHandle(wov.hEvent);

    // Drain pending input reports; the device may send status reports first.
    for (int attempt = 0; attempt < 6; attempt++) {
        memset(resp, 0, respSize);
        DWORD br = 0;
        OVERLAPPED rov = {0};
        rov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        BOOL rOk = ReadFile(h, resp, respSize, &br, &rov);
        if (!rOk) {
            if (GetLastError() != ERROR_IO_PENDING) { CloseHandle(rov.hEvent); return 0; }
            if (WaitForSingleObject(rov.hEvent, 400) != WAIT_OBJECT_0) {
                CancelIo(h); CloseHandle(rov.hEvent); return 0;
            }
            GetOverlappedResult(h, &rov, &br, FALSE);
        }
        CloseHandle(rov.hEvent);
        if (br == 0) continue;
        // Skip reports that are not answers to our command ID.
        if (resp[0] == REPORT_ID && resp[1] == cmd[0]) return (int)br;
        printf("    (skip %d bytes, rid=0x%02X id=0x%02X): ", (int)br, resp[0], resp[1]);
        hexdump("", resp, br < 17 ? (int)br : 17);
    }
    return 0;
}

// Print the parsed response: data starts at byte 6 of the full report.
static void show_response(const uint8_t* resp, int n) {
    if (n < 17) { printf("    short response\n"); return; }
    hexdump("    raw : ", resp, n);
    int dataLen = resp[5];
    const uint8_t* data = resp + 6;
    printf("    id=0x%02X status=0x%02X eeprom=0x%04X dataLen=%d\n",
           resp[1], resp[2], (resp[3] << 8) | resp[4], dataLen);
    hexdump("    data: ", data, dataLen > 10 ? 10 : dataLen);
}

int main(void) {
    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    SP_DEVICE_INTERFACE_DATA ifData; ifData.cbSize = sizeof(ifData);
    int found = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        if (!reqSize) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* detail =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(reqSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, detail, reqSize, NULL, NULL)) {
            free(detail); continue;
        }

        char up[512];
        strncpy(up, detail->DevicePath, sizeof(up) - 1); up[sizeof(up) - 1] = 0;
        for (char* p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;

        if (!strstr(up, "VID_373B")) { free(detail); continue; }
        if (!strstr(up, "COL05"))    { free(detail); continue; }

        found++;
        printf("=== col05: %s ===\n", detail->DevicePath);

        HANDLE hSync = CreateFileA(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hSync == INVALID_HANDLE_VALUE) {
            printf("  open(sync) failed: %lu\n", GetLastError()); free(detail); continue;
        }
        HIDD_ATTRIBUTES attrs; attrs.Size = sizeof(attrs);
        HidD_GetAttributes(hSync, &attrs);
        PHIDP_PREPARSED_DATA ppd = NULL;
        HidD_GetPreparsedData(hSync, &ppd);
        HIDP_CAPS caps; HidP_GetCaps(ppd, &caps);
        printf("  VID=%04X PID=%04X in=%d out=%d feat=%d\n",
               attrs.VendorID, attrs.ProductID,
               caps.InputReportByteLength, caps.OutputReportByteLength,
               caps.FeatureReportByteLength);
        HidD_FreePreparsedData(ppd);
        CloseHandle(hSync);

        HANDLE h = CreateFileA(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            printf("  open(overlapped) failed: %lu\n", GetLastError());
            free(detail); continue;
        }

        uint8_t cmd[16], resp[64];

        struct { const char* name; uint8_t id; uint16_t addr; } tests[] = {
            { "GetBatteryLevel",  CMD_GET_BATTERY_LEVEL,  0x0000 },
            { "GetWirelessOnline",CMD_GET_WIRELESS_ONLINE,0x0000 },
            { "GetEEPROM MaxDpi", CMD_GET_EEPROM,         EEP_MAX_DPI },
            { "GetEEPROM CurDpi", CMD_GET_EEPROM,         EEP_CURRENT_DPI },
            { "GetEEPROM DpiPair1",CMD_GET_EEPROM,        EEP_DPI_PAIR1 },
            { "GetEEPROM DpiPair3",CMD_GET_EEPROM,        EEP_DPI_PAIR3 },
            { "GetEEPROM ReportRate",CMD_GET_EEPROM,      EEP_REPORT_RATE },
            { "GetMouseVersion",  CMD_GET_MOUSE_VERSION,  0x0000 },
            { "GetCurrentConfig", CMD_GET_CURRENT_CONFIG, 0x0000 },
        };

        for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
            build_cmd(cmd, tests[t].id, 0x00, tests[t].addr, NULL, 0);
            printf("\n--- %s ---\n", tests[t].name);
            hexdump("    cmd : ", cmd, 16);
            int n = send_and_read(h, cmd, resp, sizeof(resp));
            if (n > 0) show_response(resp, n);
            else       printf("    NO RESPONSE\n");
            Sleep(120);
        }

        CloseHandle(h);
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    if (!found) printf("col05 for VID_373B not found — is the dongle plugged in?\n");
    return 0;
}
