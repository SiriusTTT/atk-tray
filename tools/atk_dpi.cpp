// atk_dpi — read-only hunt for the DPI path on the ATK 8K dongle.
//
// Read-only on purpose: no Set* / Restore* / pair commands are ever sent.
//
// Build: g++ -static -O2 -o atk_dpi.exe atk_dpi.cpp -lsetupapi -lhid

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
    out[0] = id; out[1] = status;
    out[2] = (uint8_t)(addr >> 8); out[3] = (uint8_t)(addr & 0xFF);
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

    // Unsolicited ReportMouseStatus(0x0A) pushes interleave with responses —
    // skip anything that isn't the answer to the command we just sent.
    for (int attempt = 0; attempt < 10; attempt++) {
        memset(resp, 0, respSize);
        DWORD br = 0;
        OVERLAPPED rov = {0};
        rov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!ReadFile(h, resp, respSize, &br, &rov)) {
            if (GetLastError() != ERROR_IO_PENDING) { CloseHandle(rov.hEvent); return 0; }
            if (WaitForSingleObject(rov.hEvent, 250) != WAIT_OBJECT_0) {
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
    GUID g; HidD_GetHidGuid(&g);
    HDEVINFO di = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof(ifd);
    HANDLE h = INVALID_HANDLE_VALUE;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, NULL, &g, i, &ifd); i++) {
        DWORD sz = 0;
        SetupDiGetDeviceInterfaceDetailA(di, &ifd, NULL, 0, &sz, NULL);
        if (!sz) continue;
        SP_DEVICE_INTERFACE_DETAIL_DATA_A* d = (SP_DEVICE_INTERFACE_DETAIL_DATA_A*)malloc(sz);
        d->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(di, &ifd, d, sz, NULL, NULL)) { free(d); continue; }
        char up[512];
        strncpy(up, d->DevicePath, sizeof(up) - 1); up[sizeof(up) - 1] = 0;
        for (char* p = up; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        if (strstr(up, "VID_373B") && strstr(up, "COL05"))
            h = CreateFileA(d->DevicePath, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, NULL);
        free(d);
    }
    SetupDiDestroyDeviceInfoList(di);
    return h;
}

int main(void) {
    HANDLE h = open_col05();
    if (h == INVALID_HANDLE_VALUE) { printf("col05 not open\n"); return 1; }
    uint8_t cmd[16], resp[64];

    // Confirm the mouse is awake before trusting any "no response" result.
    build_cmd(cmd, 0x03, 0x00, 0x0000, NULL, 0);
    int n = send_and_read(h, cmd, resp, sizeof(resp));
    if (n <= 0 || resp[6] != 0x01) {
        printf("!! Mouse is NOT online (link=%d). Wake it and re-run.\n", n > 0 ? resp[6] : -1);
        CloseHandle(h);
        return 1;
    }
    printf("Mouse online. Starting read-only DPI hunt.\n\n");

    printf("=== A: GetEEPROM(0x08) address sweep 0x0000-0x00FF, status=0x00 ===\n");
    printf("(only addresses that are NOT rejected with status=0x01 are shown)\n");
    int hits = 0;
    for (int a = 0; a <= 0xFF; a++) {
        build_cmd(cmd, 0x08, 0x00, (uint16_t)a, NULL, 0);
        n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0 && resp[2] != 0x01) {
            printf("  addr=0x%04X status=%d dataLen=%d data=", a, resp[2], resp[5]);
            for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
            hits++;
        }
        Sleep(25);
    }
    if (!hits) printf("  (none — every address came back status=0x01)\n");

    printf("\n=== B: documented DPI addresses, status 0x00 / 0x01 ===\n");
    static const struct { uint16_t a; const char* name; } named[] = {
        { 0x0002, "MaxDpi" },  { 0x0003, "MaxDpiCrc" },
        { 0x0004, "CurrentDpi" }, { 0x0005, "CurrentDpiCrc" },
        { 0x000C, "DpiPair1" }, { 0x0014, "DpiPair3" },
        { 0x001C, "DpiPair5" }, { 0x0024, "DpiPair7" },
        { 0x0000, "ReportRate" },
    };
    for (unsigned i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        for (int st = 0; st <= 1; st++) {
            build_cmd(cmd, 0x08, (uint8_t)st, named[i].a, NULL, 0);
            n = send_and_read(h, cmd, resp, sizeof(resp));
            if (n > 0) {
                printf("  %-12s addr=0x%04X st=%d -> status=%d dataLen=%d data=",
                       named[i].name, named[i].a, st, resp[2], resp[5]);
                for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
                printf("\n");
            } else printf("  %-12s addr=0x%04X st=%d -> NO RESPONSE\n", named[i].name, named[i].a, st);
            Sleep(40);
        }
    }

    printf("\n=== C: GetEEPROM with explicit data_len 1/2/4 ===\n");
    for (int dl = 1; dl <= 4; dl++) {
        build_cmd(cmd, 0x08, 0x00, 0x0004, NULL, dl);
        n = send_and_read(h, cmd, resp, sizeof(resp));
        printf("  actual DPI, dataLen=%d -> ", dl);
        if (n > 0) {
            printf("status=%d dataLen=%d data=", resp[2], resp[5]);
            for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        } else printf("NO RESPONSE\n");
        Sleep(40);
    }

    printf("\n=== D: every command ID that returned status=0x00, with addr=0x0004 ===\n");
    for (int id = 0; id <= 0x1F; id++) {
        build_cmd(cmd, (uint8_t)id, 0x00, 0x0004, NULL, 0);
        n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0 && resp[2] == 0x00 && resp[5] > 0) {
            printf("  cmd=0x%02X status=0 dataLen=%d data=", id, resp[5]);
            for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
            printf("\n");
        }
        Sleep(40);
    }

    printf("\n=== E: passive listen 12s (press DPI button now) ===\n");
    for (int i = 0; i < 60; i++) {
        uint8_t b[64]; DWORD br = 0;
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!ReadFile(h, b, 17, &br, &ov) && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 200) == WAIT_OBJECT_0)
                GetOverlappedResult(h, &ov, &br, FALSE);
            else { CancelIo(h); br = 0; }
        }
        CloseHandle(ov.hEvent);
        if (br > 0) {
            printf("  push: ");
            for (DWORD j = 0; j < br; j++) printf("%02X ", b[j]);
            printf("\n");
        }
    }

    CloseHandle(h);
    printf("\n=== DONE ===\n");
    return 0;
}
