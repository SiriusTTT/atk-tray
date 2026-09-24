#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#include <strsafe.h>
#include <dbt.h>
#include <initguid.h>
#include <devguid.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "atk_protocol.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAY_ICON         (WM_USER + 101)
#define WM_APP_DPI_UPDATE    (WM_USER + 102)
#define WM_APP_BAT_UPDATE    (WM_USER + 103)
#define WM_APP_RESTORE_TRAY  (WM_USER + 104)

#define IDM_HEADER          2001
#define IDM_BATTERY         2002
#define IDM_DPI             2003
#define IDM_AUTORUN         2004
#define IDM_RECONNECT       2005
#define IDM_EXIT            2006

#define TIMER_OSD_HIDE      3001
#define TIMER_OSD_FADE      3002

#define OSD_KEY_COLOR       RGB(255, 0, 255)

#define ATK_VID             0x373B
#define ATK_PID_1           0x101B
#define ATK_PID_2           0x104A

static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hOsdWnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static HANDLE g_hHidThread = NULL;
static HANDLE g_hStopEvent = NULL;

static volatile LONG g_battery = 100;
static volatile LONG g_dpiLevel = 1;
static volatile LONG g_dpiX = 800;
static volatile LONG g_dpiY = 800;

static volatile LONG g_voltageMv = 0;
static volatile LONG g_charging = 0;
static volatile LONG g_maxDpi = 1;
static volatile LONG g_reportRateHz = 0;
static volatile bool g_mouseOnline = false;
static volatile bool g_dpiSlotsValid = false;

static WCHAR g_osdTextLine1[64] = L"\x7B2C 1 \x6863  DPI 800";
static WCHAR g_osdTextLine2[64] = L"ATK \x65E0\x7EBF\x9F20\x6807  |  \x7535\x91CF 100%%";
static BYTE g_osdAlpha = 0;

static UINT g_uTaskbarRestartMsg = 0;
static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APP_NAME = L"atk-tray";
static WCHAR g_detectedModel[64] = L"ATK \x65E0\x7EBF\x9F20\x6807";
static volatile bool g_deviceConnected = false;

static FILE* g_logFile = NULL;

static void LogMsg(const char* fmt, ...) {
    if (!g_logFile) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(g_logFile, fmt, args);
    va_end(args);
    fflush(g_logFile);
}

static void UpdateTrayTooltip();
static void UpdateTrayIcon(int battery);
static void ShowOsdNotification(int dpiLevel, int dpiX, int battery);
static bool IsAutoRunEnabled();
static void SetAutoRun(bool enable);

static const uint8_t FONT_3X5[10][5] = {
    { 0x07, 0x05, 0x05, 0x05, 0x07 },
    { 0x02, 0x06, 0x02, 0x02, 0x07 },
    { 0x07, 0x01, 0x07, 0x04, 0x07 },
    { 0x07, 0x01, 0x07, 0x01, 0x07 },
    { 0x05, 0x05, 0x07, 0x01, 0x01 },
    { 0x07, 0x04, 0x07, 0x01, 0x07 },
    { 0x07, 0x04, 0x07, 0x05, 0x07 },
    { 0x07, 0x01, 0x02, 0x02, 0x02 },
    { 0x07, 0x05, 0x07, 0x05, 0x07 },
    { 0x07, 0x05, 0x07, 0x01, 0x07 }
};

static const uint16_t FONT_5X9[10][9] = {
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10, 0x1F },
    { 0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x01, 0x01, 0x1E },
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x02, 0x02 },
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x01, 0x01, 0x11, 0x0E },
    { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x1F, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x08 },
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x11, 0x11, 0x0E },
    { 0x0E, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01, 0x0E }
};

static const uint8_t FONT_4X9_0[9] = {
    0x06, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x06
};

static int GetTrayIconSize(HWND hWnd) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *pfnGetDpiForWindow)(HWND);
    typedef int (WINAPI *pfnGetSystemMetricsForDpi)(int, UINT);
    pfnGetDpiForWindow fnDpi = (pfnGetDpiForWindow)GetProcAddress(hUser, "GetDpiForWindow");
    pfnGetSystemMetricsForDpi fnMetrics = (pfnGetSystemMetricsForDpi)GetProcAddress(hUser, "GetSystemMetricsForDpi");

    int sz = 0;
    if (fnDpi && fnMetrics && hWnd) {
        UINT dpi = fnDpi(hWnd);
        if (dpi > 0) sz = fnMetrics(SM_CXSMICON, dpi);
    }
    if (sz <= 0) sz = GetSystemMetrics(SM_CXSMICON);
    if (sz <= 0) sz = 16;
    return sz;
}

static bool IsSystemDarkTheme() {
    DWORD val = 0, size = sizeof(val);
    if (RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, NULL, &val, &size) == ERROR_SUCCESS) {
        return (val == 0);
    }
    return true;
}

static HICON CreateBatteryIcon(int battery) {
    int size = GetTrayIconSize(g_hMainWnd);
    if (size <= 0) size = 16;
    const int SS = 4;
    const int W = size * SS;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -W;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = NULL;
    HBITMAP hbmColor = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hbmColor || !pvBits) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    uint32_t* hi = (uint32_t*)pvBits;
    memset(hi, 0, W * W * sizeof(uint32_t));

    bool isDark = IsSystemDarkTheme();
    const uint32_t c_fill  = 0xFF2DD773;
    const uint32_t c_frame = isDark ? 0xFFFFFFFF : 0xFF1E1E1E;
    const uint32_t c_digit = 0xFF000000;

    auto HiPixel = [&](int x, int y, uint32_t col) {
        if (x >= 0 && x < W && y >= 0 && y < W) hi[y * W + x] = col;
    };
    auto FillRoundRect = [&](int x0, int y0, int x1, int y1, uint32_t col, int rad) {
        if (rad > (x1 - x0) / 2) rad = (x1 - x0) / 2;
        if (rad > (y1 - y0) / 2) rad = (y1 - y0) / 2;
        if (rad < 0) rad = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                int dx = (x < x0 + rad) ? (x0 + rad - x) : (x > x1 - rad) ? (x - (x1 - rad)) : 0;
                int dy = (y < y0 + rad) ? (y0 + rad - y) : (y > y1 - rad) ? (y - (y1 - rad)) : 0;
                if (dx * dx + dy * dy <= rad * rad) HiPixel(x, y, col);
            }
    };

    int pad_y = (size < 20) ? 1 : ((size <= 24) ? 2 : 3);
    int tip_w = (size < 20) ? 2 : ((size <= 24) ? 3 : 4);
    int tip_h = (size < 20) ? 6 : (size / 3);
    int tip_y = (size - tip_h) / 2;

    int body_x0 = 0;
    int body_x1 = (size - 1 - tip_w) * SS + (SS - 1);
    int body_y0 = pad_y * SS;
    int body_y1 = (size - 1 - pad_y) * SS + (SS - 1);

    int frame_t = 1 * SS;
    int radius  = 2 * SS;

    FillRoundRect(body_x0, body_y0, body_x1, body_y1, c_frame, radius);
    FillRoundRect(body_x0 + frame_t, body_y0 + frame_t,
                  body_x1 - frame_t, body_y1 - frame_t, c_fill, (radius > frame_t ? radius - frame_t : 0));

    int tip_x0 = (size - tip_w) * SS;
    int tip_x1 = size * SS - 1;
    int tip_y0 = tip_y * SS;
    int tip_y1 = (tip_y + tip_h) * SS - 1;
    FillRoundRect(tip_x0, tip_y0, tip_x1, tip_y1, c_frame, SS);

    int in_x0 = body_x0 + frame_t;
    int in_x1 = body_x1 - frame_t;
    int in_y0 = body_y0 + frame_t;
    int in_w  = in_x1 - in_x0 + 1;
    int in_h  = (body_y1 - frame_t) - in_y0 + 1;

    char s[8];
    snprintf(s, sizeof(s), "%d", battery);
    int len = (int)strlen(s);

    {
        const int fw = (size < 20) ? 3 : 5;
        const int fh = (size < 20) ? 5 : 9;
        int gap_fp = (len == 1) ? 0 : 1;
        int target_h = in_h * 70 / 100;
        int scale = target_h / fh;
        if (scale < 1) scale = 1;
        int bold_w = 1;

        auto CalcTextWidth = [&](int sc, int bw) -> int {
            if (battery == 100 && size < 20) {
                return (sc + bw) + (gap_fp * sc) + 2 * (3 * sc + bw) + (gap_fp * sc);
            } else if (battery == 100) {
                return (2 * sc + bw) + (gap_fp * sc) + 2 * (4 * sc + bw) + (gap_fp * sc);
            } else {
                return len * (fw * sc + bw) + (len - 1) * (gap_fp * sc);
            }
        };

        while (scale > 1 && CalcTextWidth(scale, bold_w) > in_w) --scale;

        int text_h = fh * scale;
        int text_w = CalcTextWidth(scale, bold_w);
        int sx = in_x0 + (in_w - text_w) / 2;
        int sy = in_y0 + (in_h - text_h) / 2;

        auto DrawDigitScaled = [&](const uint16_t* rows, int fw, int fh,
                                   int x0, int y0, int scale) {
            for (int r = 0; r < fh; ++r) {
                for (int c = 0; c < fw; ++c) {
                    if (!((rows[r] >> (fw - 1 - c)) & 1)) continue;
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale + bold_w; ++dx) {
                            int px = x0 + c * scale + dx;
                            int py = y0 + r * scale + dy;
                            HiPixel(px, py, c_digit);
                        }
                    }
                }
            }
        };

        if (battery == 100 && size < 20) {
            int cur = sx;
            for (int t = 0; t < scale + bold_w; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r, c_digit);
            cur += scale + bold_w + gap_fp * scale;
            uint16_t rows5[5];
            for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[0][r];
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
            cur += 3 * scale + bold_w + gap_fp * scale;
            DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
        } else if (battery == 100) {
            int cur = sx;
            for (int t = 0; t < 2 * scale + bold_w; ++t)
                for (int r = 0; r < text_h; ++r)
                    HiPixel(cur + t, sy + r, c_digit);
            cur += 2 * scale + bold_w + gap_fp * scale;
            uint16_t rows9[9];
            for (int r = 0; r < 9; ++r) rows9[r] = FONT_4X9_0[r];
            DrawDigitScaled(rows9, 4, 9, cur, sy, scale);
            cur += 4 * scale + bold_w + gap_fp * scale;
            DrawDigitScaled(rows9, 4, 9, cur, sy, scale);
        } else {
            int cur = sx;
            for (int i = 0; i < len; ++i) {
                int d = s[i] - '0';
                if (size < 20) {
                    uint16_t rows5[5];
                    for (int r = 0; r < 5; ++r) rows5[r] = FONT_3X5[d][r];
                    DrawDigitScaled(rows5, 3, 5, cur, sy, scale);
                } else {
                    DrawDigitScaled(FONT_5X9[d], 5, 9, cur, sy, scale);
                }
                cur += fw * scale + bold_w + gap_fp * scale;
            }
        }
    }

    BITMAPINFO bomi = {0};
    bomi.bmiHeader = bmi.bmiHeader;
    bomi.bmiHeader.biWidth = size;
    bomi.bmiHeader.biHeight = -size;
    void* pvOut = NULL;
    HBITMAP hbmOut = CreateDIBSection(hdcMem, &bomi, DIB_RGB_COLORS, &pvOut, NULL, 0);
    if (!hbmOut || !pvOut) {
        DeleteObject(hbmColor);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }
    uint32_t* out = (uint32_t*)pvOut;
    const int SS2 = SS * SS;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            for (int sy2 = 0; sy2 < SS; ++sy2) {
                const uint32_t* row = hi + (y * SS + sy2) * W + x * SS;
                for (int sx2 = 0; sx2 < SS; ++sx2) {
                    uint32_t c = row[sx2];
                    uint32_t a = (c >> 24) & 0xFF;
                    if (a > 0) {
                        sumA += a;
                        sumR += (c >> 16) & 0xFF;
                        sumG += (c >> 8) & 0xFF;
                        sumB += (c & 0xFF);
                    }
                }
            }
            uint32_t a = sumA / SS2;
            if (a < 8) {
                out[y * size + x] = 0;
            } else {
                out[y * size + x] = (a << 24) | ((sumR / SS2) << 16) | ((sumG / SS2) << 8) | (sumB / SS2);
            }
        }
    }

    int maskPitch = ((size + 15) / 16) * 2;
    BYTE maskBits[256] = {0};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t px = out[y * size + x];
            uint8_t a = (px >> 24) & 0xFF;
            if (a < 32) maskBits[y * maskPitch + (x / 8)] |= (1 << (7 - (x % 8)));
        }
    }
    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, maskBits);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmOut;
    ii.hbmMask = hbmMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmOut);
    DeleteObject(hbmMask);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hIcon;
}

static void UpdateTrayTooltip() {
    if (!g_deviceConnected) {
        StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
            L"atk-tray\n(\x7B49\x5F85\x8BBE\x5907\x8FDE\x63A5...)");
        return;
    }

    if (!g_mouseOnline) {
        // The dongle is up but the mouse is asleep: every value we hold is stale.
        StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
            L"%s\n\x9F20\x6807\x4F11\x7720\x4E2D  |  \x7535\x91CF: %d%%",
            g_detectedModel, g_battery);
        return;
    }

    WCHAR dpiValue[16];
    if (g_dpiSlotsValid)
        StringCchPrintfW(dpiValue, ARRAYSIZE(dpiValue), L"%d", g_dpiX);
    else
        StringCchCopyW(dpiValue, ARRAYSIZE(dpiValue), L"--");

    WCHAR batText[48];
    StringCchPrintfW(batText, ARRAYSIZE(batText), L"%d%%%s  %dmV",
        g_battery, g_charging ? L" (\x5145\x7535)" : L"", g_voltageMv);

    StringCchPrintfW(g_nid.szTip, ARRAYSIZE(g_nid.szTip),
        L"%s\n\x7535\x91CF: %s\nDPI: %s (\x7B2C %d \x6863)",
        g_detectedModel, batText, dpiValue, g_dpiLevel);
}

static void UpdateTrayIcon(int battery) {
    HICON hNewIcon = CreateBatteryIcon(battery);
    if (hNewIcon) {
        if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = hNewIcon;
    }
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    UpdateTrayTooltip();
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static LRESULT CALLBACK OsdWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            HBRUSH hBrKey = CreateSolidBrush(OSD_KEY_COLOR);
            FillRect(memDC, &rc, hBrKey);
            DeleteObject(hBrKey);

            RECT rcBox = rc;
            InflateRect(&rcBox, -2, -2);
            HBRUSH hBg = CreateSolidBrush(RGB(24, 26, 32));
            HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(70, 75, 90));
            HGDIOBJ oldBr = SelectObject(memDC, hBg);
            HGDIOBJ oldPen = SelectObject(memDC, hBorder);
            RoundRect(memDC, rcBox.left, rcBox.top, rcBox.right, rcBox.bottom, 22, 22);

            SetBkMode(memDC, TRANSPARENT);

            HFONT hFontBig = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
            HGDIOBJ oldFont = SelectObject(memDC, hFontBig);
            SetTextColor(memDC, RGB(255, 255, 255));
            RECT rcTop = rcBox;
            rcTop.bottom = rcBox.top + 42;
            DrawTextW(memDC, g_osdTextLine1, -1, &rcTop, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            HFONT hFontSub = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
            SelectObject(memDC, hFontSub);
            SetTextColor(memDC, RGB(130, 215, 255));
            RECT rcBot = rcBox;
            rcBot.top = rcBox.top + 40;
            DrawTextW(memDC, g_osdTextLine2, -1, &rcBot, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBr);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            DeleteObject(hFontBig);
            DeleteObject(hFontSub);
            DeleteObject(hBorder);
            DeleteObject(hBg);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == TIMER_OSD_HIDE) {
                KillTimer(hWnd, TIMER_OSD_HIDE);
                SetTimer(hWnd, TIMER_OSD_FADE, 16, NULL);
            } else if (wParam == TIMER_OSD_FADE) {
                if (g_osdAlpha > 15) {
                    g_osdAlpha -= 15;
                    SetLayeredWindowAttributes(hWnd, OSD_KEY_COLOR, g_osdAlpha, LWA_COLORKEY | LWA_ALPHA);
                } else {
                    KillTimer(hWnd, TIMER_OSD_FADE);
                    ShowWindow(hWnd, SW_HIDE);
                    g_osdAlpha = 0;
                }
            }
            return 0;
        }
        default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

static void ShowOsdNotification(int dpiLevel, int dpiX, int battery) {
    if (!g_hOsdWnd) return;

    StringCchPrintfW(g_osdTextLine1, ARRAYSIZE(g_osdTextLine1),
        L"\x7B2C %d \x6863  DPI %d", dpiLevel, dpiX);
    StringCchPrintfW(g_osdTextLine2, ARRAYSIZE(g_osdTextLine2),
        L"%s  |  \x7535\x91CF %d%%", g_detectedModel, battery);

    RECT rcWork;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0)) {
        rcWork.left = 0; rcWork.top = 0;
        rcWork.right = GetSystemMetrics(SM_CXSCREEN);
        rcWork.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = 260, h = 76;
    int x = rcWork.left + ((rcWork.right - rcWork.left) - w) / 2;
    int y = rcWork.bottom - h - 80;

    SetWindowPos(g_hOsdWnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_osdAlpha = 240;
    SetLayeredWindowAttributes(g_hOsdWnd, OSD_KEY_COLOR, g_osdAlpha, LWA_COLORKEY | LWA_ALPHA);
    InvalidateRect(g_hOsdWnd, NULL, FALSE);
    UpdateWindow(g_hOsdWnd);

    KillTimer(g_hOsdWnd, TIMER_OSD_FADE);
    SetTimer(g_hOsdWnd, TIMER_OSD_HIDE, 1400, NULL);
}

struct DeviceMatch {
    WCHAR path[MAX_PATH];
    WCHAR model[64];
    int reportSize;
    int featureSize;
    int colNum;
};

// ---------------------------------------------------------------------------
// HID worker
// ---------------------------------------------------------------------------

static void PublishBattery(const atk::Battery& bat) {
    int level = bat.level;
    if (level > 100) level = 100;
    if (level < 0) level = 0;

    g_voltageMv = bat.voltageMv;
    g_charging = (bat.charging != 0);

    if (level != g_battery) {
        g_battery = level;
        if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)level, 0);
        UpdateTrayTooltip();
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

static DWORD WINAPI HidWorkerThread(LPVOID lpParam) {
    (void)lpParam;

    WCHAR path[MAX_PATH] = {0};
    atk::Device dev;
    int lastDpi = -1;
    int failCount = 0;
    int tick = 0;
    bool loggedAsleep = false;

    while (WaitForSingleObject(g_hStopEvent, 0) != WAIT_OBJECT_0) {
        if (!dev.IsOpen()) {
            if (atk::FindCommandChannel(path, MAX_PATH) && dev.Open(path)) {
                LogMsg("Opened command channel: %ls\n", path);
                g_deviceConnected = true;
                lastDpi = -1;          // force a full DPI refresh after connect
                failCount = 0;
                if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);
            } else {
                if (g_deviceConnected) {
                    LogMsg("Command channel lost, retrying...\n");
                    g_deviceConnected = false;
                    g_mouseOnline = false;
                    if (g_hMainWnd) PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);
                }
                if (WaitForSingleObject(g_hStopEvent, 1500) == WAIT_OBJECT_0) break;
                continue;
            }
        }

        bool online = false;
        if (!atk::QueryOnline(dev, &online)) {
            // Three consecutive failures means the handle is stale, not that the
            // mouse is asleep — QueryOnline is answered by the dongle itself.
            if (++failCount >= 3) { dev.Close(); g_deviceConnected = false; }
            if (WaitForSingleObject(g_hStopEvent, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        failCount = 0;

        bool wasOnline = g_mouseOnline;
        g_mouseOnline = online;

        if (online) {
            loggedAsleep = false;

            atk::Battery bat;
            if (atk::QueryBattery(dev, &bat)) {
                PublishBattery(bat);
                LogMsg("battery=%d%% %s %dmV\n", bat.level,
                       bat.charging ? "charging" : "discharging", bat.voltageMv);
            }

            atk::DpiConfig cfg;
            if (atk::QueryDpiConfig(dev, &cfg)) {
                g_dpiLevel     = cfg.currentDpi + 1;   // stored 0-based, shown 1-based
                g_maxDpi       = cfg.maxDpi;           // maxDpi is the stage count
                g_reportRateHz = atk::ReportRateHz(cfg.reportRateCode);
                LogMsg("dpi stage=%d/%d reportRate=%dHz\n",
                       g_dpiLevel, g_maxDpi, g_reportRateHz);

                bool firstRead = (lastDpi < 0);
                if (firstRead || (int)cfg.currentDpi != lastDpi) {
                    lastDpi = cfg.currentDpi;

                    int xs[8] = {0}, ys[8] = {0};
                    int n = atk::QueryDpiSlots(dev, xs, ys);
                    if (n > 0 && cfg.currentDpi < 8) {
                        g_dpiX = xs[cfg.currentDpi];
                        g_dpiY = ys[cfg.currentDpi];
                        g_dpiSlotsValid = true;
                        LogMsg("dpi slots[%d]: ", n);
                        for (int s = 0; s < n; ++s) LogMsg("%d/%d ", xs[s], ys[s]);
                        LogMsg("-> active=%d\n", g_dpiX);
                    } else {
                        LogMsg("dpi slots: query failed (n=%d, stage=%d)\n", n, cfg.currentDpi);
                    }

                    // Suppress the OSD on the first read after connecting so the
                    // app doesn't flash a notification every time it starts.
                    if (!firstRead && g_hMainWnd)
                        PostMessageW(g_hMainWnd, WM_APP_DPI_UPDATE,
                                     (WPARAM)g_dpiLevel, (LPARAM)g_dpiX);
                }
            }
            if ((tick % 10) == 0 && g_hMainWnd)
                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);
        } else {
            // Dongle is alive but the mouse is not answering, so battery and DPI
            // would be stale. Say so once per transition rather than every poll.
            if (wasOnline || !loggedAsleep) {
                loggedAsleep = true;
                LogMsg("Mouse asleep (dongle OK, no forwarded replies).\n");
            }
            if (wasOnline && g_hMainWnd)
                PostMessageW(g_hMainWnd, WM_APP_BAT_UPDATE, (WPARAM)g_battery, 0);
        }

        ++tick;
        if (WaitForSingleObject(g_hStopEvent, 3000) == WAIT_OBJECT_0) break;
    }

    dev.Close();
    LogMsg("HID worker stopped.\n");
    return 0;
}


static bool IsAutoRunEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        WCHAR path[MAX_PATH];
        DWORD len = sizeof(path);
        DWORD type = 0;
        LSTATUS st = RegQueryValueExW(hKey, APP_NAME, NULL, &type, (LPBYTE)path, &len);
        RegCloseKey(hKey);
        return (st == ERROR_SUCCESS);
    }
    return false;
}

static void SetAutoRun(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            WCHAR selfPath[MAX_PATH];
            GetModuleFileNameW(NULL, selfPath, MAX_PATH);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)selfPath,
                (DWORD)((wcslen(selfPath) + 1) * sizeof(WCHAR)));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

static void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    WCHAR bufHeader[64], bufBat[64], bufDpi[64], bufExtra[64];

    if (!g_deviceConnected) {
        StringCchPrintfW(bufHeader, 64, L"atk-tray (\x672A\x8FDE\x63A5)");
        StringCchPrintfW(bufBat, 64, L"\x7535\x6C60\x7535\x91CF: --");
        StringCchPrintfW(bufDpi, 64, L"\x5F53\x524D DPI: --");
        bufExtra[0] = L'\0';
    } else if (!g_mouseOnline) {
        StringCchPrintfW(bufHeader, 64, L"%s", g_detectedModel);
        StringCchPrintfW(bufBat, 64, L"\x7535\x6C60\x7535\x91CF: %d%%", g_battery);
        StringCchPrintfW(bufDpi, 64, L"\x5F53\x524D DPI: --");
        StringCchCopyW(bufExtra, 64, L"\x9F20\x6807\x4F11\x7720\x4E2D\xFF0C\x6309\x952E\x5524\x9192");
    } else {
        StringCchPrintfW(bufHeader, 64, L"%s", g_detectedModel);
        if (g_charging)
            StringCchPrintfW(bufBat, 64, L"\x7535\x6C60\x7535\x91CF: %d%% (%dmV, \x5145\x7535\x4E2D)",
                             g_battery, g_voltageMv);
        else
            StringCchPrintfW(bufBat, 64, L"\x7535\x6C60\x7535\x91CF: %d%% (%dmV)",
                             g_battery, g_voltageMv);

        if (g_dpiSlotsValid)
            StringCchPrintfW(bufDpi, 64, L"\x5F53\x524D DPI: %d (\x7B2C %d \x6863)",
                             g_dpiX, g_dpiLevel);
        else
            StringCchPrintfW(bufDpi, 64, L"\x5F53\x524D DPI: -- (\x7B2C %d \x6863)", g_dpiLevel);

        if (g_reportRateHz > 0)
            StringCchPrintfW(bufExtra, 64, L"\x56DE\x62A5\x7387: %d Hz", g_reportRateHz);
        else
            bufExtra[0] = L'\0';
    }

    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER, bufHeader);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_BATTERY, bufBat);
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_DPI, bufDpi);
    if (bufExtra[0])
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_DPI + 100, bufExtra);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    UINT autoRunFlags = MF_STRING | (IsAutoRunEnabled() ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, autoRunFlags, IDM_AUTORUN, L"\x5F00\x673A\x81EA\x542F\x52A8");
    AppendMenuW(hMenu, MF_STRING, IDM_RECONNECT, L"\x91CD\x65B0\x8FDE\x63A5\x5916\x8BBE");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"\x9000\x51FA");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_uTaskbarRestartMsg && g_uTaskbarRestartMsg != 0) {
        // Explorer 重启会清空所有托盘图标，必须重新 NIM_ADD。
        // 只发 NIM_MODIFY 会静默失败 —— 图标就再也回不来了。
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        UpdateTrayIcon(g_battery);
        return 0;
    }

    if (msg == WM_APP_RESTORE_TRAY) {
        // 用户又双击了一次 exe：把图标重新挂回去，并弹一下 OSD 作为反馈
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        UpdateTrayIcon(g_battery);
        ShowOsdNotification(g_dpiLevel, g_dpiX, g_battery);
        return 0;
    }

    switch (msg) {
        case WM_TRAY_ICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU)
                ShowContextMenu(hWnd);
            else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK)
                ShowOsdNotification(g_dpiLevel, g_dpiX, g_battery);
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDM_AUTORUN: SetAutoRun(!IsAutoRunEnabled()); break;
                case IDM_RECONNECT: {
                    SetEvent(g_hStopEvent);
                    WaitForSingleObject(g_hHidThread, 1000);
                    CloseHandle(g_hHidThread);
                    ResetEvent(g_hStopEvent);
                    g_hHidThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);
                    break;
                }
                case IDM_EXIT: DestroyWindow(hWnd); break;
            }
            return 0;
        }
        case WM_APP_DPI_UPDATE: {
            int level = (int)wParam;
            int dpix = (int)lParam;
            ShowOsdNotification(level, dpix, g_battery);
            UpdateTrayTooltip();
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            return 0;
        }
        case WM_APP_BAT_UPDATE: {
            UpdateTrayIcon((int)wParam);
            return 0;
        }
        case WM_SETTINGCHANGE: {
            UpdateTrayIcon(g_battery);
            return 0;
        }
        case WM_DEVICECHANGE: return 0;
        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        }
        default: return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\atk-traySingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已经有实例在跑。别静默退出 —— 那样用户双击就是"毫无反应"。
        // 通知已有实例把托盘图标重新挂上（多半是被 Explorer 重启清掉了）。
        HWND hPrev = FindWindowW(L"AtkTrayMessageWnd", NULL);
        if (hPrev) PostMessageW(hPrev, WM_APP_RESTORE_TRAY, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }

    WCHAR logPath[MAX_PATH];
    GetModuleFileNameW(NULL, logPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(logPath, L'\\');
    if (lastSlash) StringCchCopyW(lastSlash + 1, MAX_PATH - (lastSlash - logPath + 1), L"atk-tray.log");
    g_logFile = _wfopen(logPath, L"w");
    LogMsg("atk-tray starting...\n");

    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *pfnSetDpiAwareV2)(DPI_AWARENESS_CONTEXT);
        pfnSetDpiAwareV2 fn = (pfnSetDpiAwareV2)GetProcAddress(hUser, "SetProcessDpiAwarenessContext");
        if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    g_hInstance = hInstance;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"AtkTrayMessageWnd";
    RegisterClassExW(&wc);

    g_hMainWnd = CreateWindowExW(0, wc.lpszClassName, L"AtkTray", WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    g_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wcOsd = {0};
    wcOsd.cbSize = sizeof(WNDCLASSEXW);
    wcOsd.lpfnWndProc = OsdWndProc;
    wcOsd.hInstance = hInstance;
    wcOsd.lpszClassName = L"AtkOsdPopupWnd";
    wcOsd.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wcOsd);

    g_hOsdWnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wcOsd.lpszClassName, L"AtkOSD", WS_POPUP,
        0, 0, 260, 76, NULL, NULL, hInstance, NULL);

    DEV_BROADCAST_DEVICEINTERFACE_W dbFilter = {0};
    dbFilter.dbcc_size = sizeof(dbFilter);
    dbFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    HidD_GetHidGuid(&dbFilter.dbcc_classguid);
    RegisterDeviceNotificationW(g_hMainWnd, &dbFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hMainWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = CreateBatteryIcon(100);
    UpdateTrayTooltip();
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_hHidThread = CreateThread(NULL, 0, HidWorkerThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    SetEvent(g_hStopEvent);
    WaitForSingleObject(g_hHidThread, 2000);
    CloseHandle(g_hHidThread);
    CloseHandle(g_hStopEvent);

    if (g_nid.hIcon) DestroyIcon(g_nid.hIcon);
    if (g_hOsdWnd) DestroyWindow(g_hOsdWnd);
    if (g_logFile) { LogMsg("atk-tray exiting.\n"); fclose(g_logFile); }

    CloseHandle(hMutex);
    return (int)msg.wParam;
}
