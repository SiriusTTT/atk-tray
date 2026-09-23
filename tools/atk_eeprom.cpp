// atk_eeprom — read EEPROM regions with a correct read length (data_len > 0).
//
// The dongle rejects GetEEPROM(0x08) when the request's data_len is 0; data_len
// is the number of bytes to read.  Values come back as (value, 0x55-value)
// pairs, i.e. every odd byte is the complement of the even byte before it.
//
// Read-only.
//
// Usage: atk_eeprom.exe [start_hex] [end_hex] [readlen]
//   defaults: 0x0000 0x00FF 2
//
// Build: g++ -static -O2 -o atk_eeprom.exe atk_eeprom.cpp -lsetupapi -lhid

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static const uint8_t REPORT_ID = 0x08;

static uint8_t checksum16(const uint8_t* c) {
    unsigned s = REPORT_ID;
    for (int i = 0; i < 15; i++) s += c[i];
    return (uint8_t)(0x55u - (s & 0xFF));
}

static void build_cmd(uint8_t* out, uint8_t id, uint8_t status,
                      uint16_t addr, int dataLen) {
    memset(out, 0, 16);
    out[0] = id; out[1] = status;
    out[2] = (uint8_t)(addr >> 8); out[3] = (uint8_t)(addr & 0xFF);
    out[4] = (uint8_t)dataLen;
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
    for (int k = 0; k < 10; k++) {
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

// Decode (value, 0x55-value) pairs into the underlying values.
static void decode_pairs(const uint8_t* data, int len, char* out, size_t outLen) {
    int pos = 0;
    for (int i = 0; i + 1 < len; i += 2) {
        int v = data[i];
        int c = 0x55 - v;
        int ok = (c == data[i + 1]);
        pos += snprintf(out + pos, outLen - pos, "%s0x%02X%s",
                        i ? " " : "", v, ok ? "" : "?");
        if (pos >= (int)outLen - 8) break;
    }
}

int main(int argc, char** argv) {
    int start = (argc > 1) ? (int)strtol(argv[1], NULL, 0) : 0x0000;
    int end   = (argc > 2) ? (int)strtol(argv[2], NULL, 0) : 0x00FF;
    int rdlen = (argc > 3) ? (int)strtol(argv[3], NULL, 0) : 2;
    if (rdlen < 1 || rdlen > 10) rdlen = 2;

    HANDLE h = open_col05();
    if (h == INVALID_HANDLE_VALUE) { printf("col05 not open\n"); return 1; }
    uint8_t cmd[16], resp[64];

    build_cmd(cmd, 0x03, 0x00, 0x0000, 0);
    int n = send_and_read(h, cmd, resp, sizeof(resp));
    if (n <= 0 || resp[6] != 0x01) {
        printf("!! Mouse offline — wake it and re-run.\n");
        CloseHandle(h); return 1;
    }

    printf("=== EEPROM sweep 0x%04X-0x%04X, readlen=%d ===\n", start, end, rdlen);
    printf("(dash '?' marks a pair whose complement byte does not match)\n\n");
    int shown = 0;
    for (int a = start; a <= end; a++) {
        build_cmd(cmd, 0x08, 0x00, (uint16_t)a, rdlen);
        n = send_and_read(h, cmd, resp, sizeof(resp));
        if (n > 0 && resp[2] == 0x00 && resp[5] > 0) {
            int allZero = 1;
            for (int b = 0; b < resp[5]; b++) if (resp[6 + b]) { allZero = 0; break; }
            if (!allZero) {
                char dec[128] = {0};
                decode_pairs(resp + 6, resp[5], dec, sizeof(dec));
                printf("  addr=0x%04X len=%d raw=", a, resp[5]);
                for (int b = 0; b < resp[5] && b < 10; b++) printf("%02X ", resp[6 + b]);
                printf(" decoded=[%s]\n", dec);
                shown++;
            }
        }
        Sleep(20);
    }
    if (!shown) printf("  (no non-zero EEPROM content found in this range)\n");

    CloseHandle(h);
    printf("\n=== DONE (%d entries) ===\n", shown);
    return 0;
}
