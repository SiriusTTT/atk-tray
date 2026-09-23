// atk_listen — monitors every input-report collection of the ATK dongle at once
// so we can see which channel (if any) carries DPI notifications.
//
// Usage: atk_listen.exe [seconds]      (default 20)
//
// While it runs: press the mouse's DPI button, move the mouse, click buttons.
// Any input report on any vendor collection gets printed with a timestamp.
//
// Build: g++ -static -O2 -o atk_listen.exe atk_listen.cpp -lsetupapi -lhid

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static volatile LONG g_stop = 0;
static CRITICAL_SECTION g_printLock;
static LONG g_startTick = 0;

// Only the vendor-defined collections are interesting; the standard HID ones
// (keyboard / consumer / mouse) would flood us with noise.
static int is_interesting(const char* upperPath) {
    if (!strstr(upperPath, "VID_373B")) return 0;
    if (strstr(upperPath, "COL01")) return 1;   // UP 0xFF05
    if (strstr(upperPath, "COL02")) return 1;   // UP 0xFF03
    if (strstr(upperPath, "COL05")) return 1;   // UP 0xFF02  <- the command channel
    if (strstr(upperPath, "COL07")) return 1;   // UP 0xFF06
    return 0;
}

static void timestamp(char* out, size_t n) {
    DWORD ms = GetTickCount() - g_startTick;
    snprintf(out, n, "%5lu.%03lu", ms / 1000, ms % 1000);
}

typedef struct { char label[32]; HANDLE h; int inLen; } ListenCtx;

static DWORD WINAPI listen_thread(LPVOID param) {
    ListenCtx* ctx = (ListenCtx*)param;
    uint8_t buf[128];

    while (!g_stop) {
        DWORD br = 0;
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) break;

        BOOL ok = ReadFile(ctx->h, buf, ctx->inLen, &br, &ov);
        if (!ok && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(ov.hEvent);
            Sleep(50);
            continue;
        }

        DWORD w = WaitForSingleObject(ov.hEvent, 200);
        if (w == WAIT_OBJECT_0) {
            GetOverlappedResult(ctx->h, &ov, &br, FALSE);
            if (br > 0) {
                char ts[32]; timestamp(ts, sizeof(ts));
                EnterCriticalSection(&g_printLock);
                printf("[%s] %s (%lu): ", ts, ctx->label, (unsigned long)br);
                for (DWORD i = 0; i < br && i < 64; i++) printf("%02X ", buf[i]);
                printf("\n");
                fflush(stdout);
                LeaveCriticalSection(&g_printLock);
            }
        } else {
            CancelIo(ctx->h);
        }
        CloseHandle(ov.hEvent);
    }
    return 0;
}

int main(int argc, char** argv) {
    int seconds = (argc > 1) ? atoi(argv[1]) : 20;
    InitializeCriticalSection(&g_printLock);
    g_startTick = GetTickCount();

    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    ListenCtx ctxs[16];
    HANDLE threads[16];
    int n = 0;

    SP_DEVICE_INTERFACE_DATA ifData; ifData.cbSize = sizeof(ifData);
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

        if (is_interesting(up) && n < 16) {
            HANDLE h = CreateFileA(d->DevicePath, GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                PHIDP_PREPARSED_DATA ppd = NULL;
                HIDP_CAPS caps = {0};
                if (HidD_GetPreparsedData(h, &ppd)) { HidP_GetCaps(ppd, &caps); HidD_FreePreparsedData(ppd); }
                const char* p = strstr(up, "COL0");
                snprintf(ctxs[n].label, sizeof(ctxs[n].label), "%s", p ? p : "?");
                ctxs[n].h = h;
                ctxs[n].inLen = caps.InputReportByteLength;
                printf("watching %s (in=%d)\n", ctxs[n].label, ctxs[n].inLen);
                n++;
            }
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(devInfo);

    if (n == 0) { printf("No collections found — dongle plugged in?\n"); return 1; }

    printf("\nListening for %d seconds. NOW: press the DPI button / move the mouse.\n\n", seconds);
    fflush(stdout);

    for (int i = 0; i < n; i++) threads[i] = CreateThread(NULL, 0, listen_thread, &ctxs[i], 0, NULL);
    Sleep(seconds * 1000);
    g_stop = 1;
    WaitForMultipleObjects(n, threads, TRUE, 3000);

    for (int i = 0; i < n; i++) { CloseHandle(threads[i]); CloseHandle(ctxs[i].h); }
    DeleteCriticalSection(&g_printLock);
    printf("\n=== DONE ===\n");
    return 0;
}
