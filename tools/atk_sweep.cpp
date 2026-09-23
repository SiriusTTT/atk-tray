// ATK protocol sweep — enumerates which command IDs / EEPROM addresses this
// dongle actually answers, and hunts for the DPI read path.
//
// Protocol (libatk-rs): report ID 0x08 + 16-byte command
//   [0] cmd id  [1] status  [2..3] eeprom addr (BE)  [4] data len
//   [5..14] data  [15] checksum = 0x55 - ((0x08 + sum(bytes 0..14)) & 0xFF)
// Response report: 17 bytes, resp[0]=0x08, then the same 16-byte layout.
//   status 0x00 == OK (data valid), 0x01 == rejected/unsupported.
//
// Build: g++ -static -O2 -o atk_sweep.exe atk_sweep.cpp -lsetupapi -lhid

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const uint8_t REPORT_ID = 0x08;

static uint8_t checksum16(const uint8_t* cmd) {
    unsigned sum = REPORT_ID;
    for (int i = 0; i < 15; i++) sum += cmd[i];
    return (uint8_t)(0x55u - (sum & 0xFF));
}

static void build_cmd(uint8_t* out, uint8_t id, uint8_t status,
                      uint16_t addr, const uint8_t* data, int dataLen) {
    memset(out, 0, 16);
    out[0] = id;
    out[1] = status;
    out[2] = (uint8_t)(addr >> 8);
    out[3] = (uint8_t)(addr & 0xFF);
    out[4] = (uint8_t)dataLen;
    if (data && dataLen > 0) memcpy(out + 5, data, dataLen > 10 ? 10 : dataLen);
    out[15] = checksum16(out);
}

static int send_and_read(HANDLE h, const uint8_t* cmd, uint8_t* resp, int respSize) {
    uint8_t wbuf[17];
    wbuf[0] = REPORT_ID;
    memcpy(wbuf + 1, cmd, 16);

    DWORD written = 0;
    OVERLAPPED wov = {0};
    wov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!WriteFile(h, wbuf, sizeof(wbuf), &written, &wov)) {
        if (GetLastError() != ERROR_IO_PENDING) { CloseHandle(wov.hEvent); return 0; }
        if (WaitForSingleObject(wov.hEvent, 400) != WAIT_OBJECT_0) {
            CancelIo(h); CloseHandle(wov.hEvent); return 0;
        }
        GetOverlappedResult(h, &wov, &written, FALSE);
    }
    CloseHandle(wov.hEvent);

    for (int attempt = 0; attempt < 8; attempt++) {
        memset(resp, 0, respSize);
        DWORD br = 0;
        OVERLAPPED rov = {0};
        rov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!ReadFile(h, resp, respSize, &br, &rov)) {
            if (GetLastError() != ERROR_IO_PENDING) { CloseHandle(rov.hEvent); return 0; }
            if (WaitForSingleObject(rov.hEvent, 300) != WAIT_OBJECT_0) {
                CancelIo(h); CloseHandle(rov.hEvent); return 0;
            }
            GetOverlappedResult(h, &rov, &br, FALSE);
        }
        CloseHandle(rov.hEvent);
        if (br == 0) continue;
        if (resp[0] == REPORT_ID && resp[1] == cmd[0]) return (int)br;
    }
    return 0;
}

static HANDLE open_col05(void) {
    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifData; ifData.cbSize = sizeof(ifData);
    HANDLE result = INVALID_HANDLE_VALUE;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        if (!reqSize) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* d =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(reqSize);
        d->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, d, reqSize, NULL, NULL)) {
            free(d); continue;
        }
        char up[512];
        strncpy(up, d->DevicePath, sizeof(up) - 1); up[sizeof(up) - 1] = 0;
        for (char* p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;

        if (strstr(up, "VID_373B") && strstr(up, "COL05")) {
            result = CreateFileA(d->DevicePath, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, NULL);
            printf("Opened col05: %s\n\n", d->DevicePath);
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

static const char* cmd_name(uint8_t id) {
    switch (id) {
        case 0x00: return "Zero";
        case 0x01: return "DownLoadData";
        case 0x02: return "DownLoadDriverStatus";
        case 0x03: return "GetWirelessMouseOnline";
        case 0x04: return "GetBatteryLevel";
        case 0x05: return "SetWirelessDonglePair";
        case 0x06: return "GetWirelessDonglePairResult";
        case 0x07: return "SetEEPROM";
        case 0x08: return "GetEEPROM";
        case 0x09: return "RestoreFactory";
        case 0x0A: return "ReportMouseStatus";
        case 0x0B: return "Reserved1";
        case 0x0C: return "Reserved2";
        case 0x0D: return "EnterUSBUpgradeMode";
        case 0x0E: return "GetCurrentConfig";
        case 0x0F: return "SetCurrentConfig";
        case 0x10: return "GetMouseCIDMID";
        case 0x11: return "Reserved3";
        case 0x12: return "GetMouseVersion";
        case 0x13: return "DongleExitPair";
        case 0x14: return "Set4KRGBMode";
        case 0x15: return "Get4KRGBMode";
        case 0x16: return "SetFarDistanceMode";
        case 0x17: return "GetFarDistanceMode";
        case 0x18: return "SetDongleLightMode";
        case 0x19: return "GetDongleLightMode";
        case 0x1A: return "ReportMouseUpgradeErrorStatus";
        case 0x1B: return "ReportMouseUpgradeStatus";
        default:   return "?";
    }
}

int main(void) {
    HANDLE h = open_col05();
    if (h == INVALID_HANDLE_VALUE) {
        printf("Could not open col05 — dongle plugged in?\n");
        return 1;
    }
    uint8_t cmd[16], resp[64];

    printf("=== SWEEP 1: all command IDs, status=0x00, addr=0x0000 ===\n");
    printf("%-6s %-32s %-7s %-8s %s\n", "CMD", "NAME", "STATUS", "DATALEN", "DATA");
    for (int id = 0x00; id <= 0x1F; id++) {
        build_cmd(cmd, (uint8_t)id, 0x00, 0x0000, NULL, 0);
        int n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0) {
            int dl = resp[5];
            printf("0x%02X   %-32s %-7d %-8d ", id, cmd_name((uint8_t)id), resp[2], dl);
            for (int b = 0; b < dl && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        } else {
            printf("0x%02X   %-32s NO RESPONSE\n", id, cmd_name((uint8_t)id));
        }
        Sleep(80);
    }

    printf("\n=== SWEEP 2: all command IDs, status=0x01, addr=0x0000 ===\n");
    printf("%-6s %-32s %-7s %-8s %s\n", "CMD", "NAME", "STATUS", "DATALEN", "DATA");
    for (int id = 0x00; id <= 0x1F; id++) {
        build_cmd(cmd, (uint8_t)id, 0x01, 0x0000, NULL, 0);
        int n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0) {
            int dl = resp[5];
            printf("0x%02X   %-32s %-7d %-8d ", id, cmd_name((uint8_t)id), resp[2], dl);
            for (int b = 0; b < dl && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        } else {
            printf("0x%02X   %-32s NO RESPONSE\n", id, cmd_name((uint8_t)id));
        }
        Sleep(80);
    }

    printf("\n=== SWEEP 3: GetEEPROM(0x08) over addresses, status=0x00 ===\n");
    static const uint16_t addrs[] = {
        0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
        0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x0010, 0x0014, 0x0018,
        0x001c, 0x0020, 0x0024, 0x002c, 0x004c, 0x0060, 0x0064, 0x00a9,
    };
    printf("%-8s %-7s %-8s %s\n", "ADDR", "STATUS", "DATALEN", "DATA");
    for (unsigned i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        build_cmd(cmd, 0x08, 0x00, addrs[i], NULL, 0);
        int n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0) {
            int dl = resp[5];
            printf("0x%04X   %-7d %-8d ", addrs[i], resp[2], dl);
            for (int b = 0; b < dl && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        } else {
            printf("0x%04X   NO RESPONSE\n", addrs[i]);
        }
        Sleep(80);
    }

    printf("\n=== SWEEP 4: GetCurrentConfig(0x0E) with status 0x00/0x01/0x02 ===\n");
    for (int st = 0; st <= 2; st++) {
        build_cmd(cmd, 0x0E, (uint8_t)st, 0x0000, NULL, 0);
        int n = send_and_read(h, cmd, resp, sizeof(resp));
        printf("status=%d -> ", st);
        if (n > 0) {
            printf("st=%d dataLen=%d data=", resp[2], resp[5]);
            for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        } else printf("NO RESPONSE\n");
        Sleep(80);
    }

    printf("\n=== SWEEP 5: battery read x5 (stability) ===\n");
    for (int i = 0; i < 5; i++) {
        build_cmd(cmd, 0x04, 0x00, 0x0000, NULL, 0);
        int n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0) {
            int dl = resp[5];
            printf("  #%d status=%d dataLen=%d data=", i, resp[2], dl);
            for (int b = 0; b < dl && b < 10; b++) printf("%02X ", resp[6 + b]);
            if (dl >= 2) printf("  -> level=%d%% voltage=%umV", resp[6], (resp[7] << 8) | resp[8]);
            printf("\n");
        } else printf("  #%d NO RESPONSE\n", i);
        Sleep(400);
    }

    printf("\n=== SWEEP 6: listen for unsolicited reports (8s) ===\n");
    printf("  >>> Press the mouse's DPI button / move the mouse now <<<\n");
    for (int i = 0; i < 40; i++) {
        uint8_t b[64]; DWORD br = 0;
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        BOOL ok = ReadFile(h, b, 17, &br, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 200) == WAIT_OBJECT_0) {
                GetOverlappedResult(h, &ov, &br, FALSE);
            } else { CancelIo(h); br = 0; }
        }
        CloseHandle(ov.hEvent);
        if (br > 0) {
            printf("  unsolicited (%d): ", (int)br);
            for (DWORD j = 0; j < br; j++) printf("%02X ", b[j]);
            printf("\n");
        }
    }

    CloseHandle(h);
    printf("\n=== DONE ===\n");
    return 0;
}
