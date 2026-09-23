#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#include <hidpi.h>
#include <devguid.h>
#include <stdio.h>
#include <stdint.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

#define ATK_VID 0x373B
#define ATK_PID 0x101B

static void DumpCapabilities(HANDLE hDev, const char* label) {
    PHIDP_PREPARSED_DATA preparsed = NULL;
    if (!HidD_GetPreparsedData(hDev, &preparsed)) return;

    HIDP_CAPS caps;
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS) {
        HidD_FreePreparsedData(preparsed);
        return;
    }

    printf("\n=== %s ===\n", label);
    printf("  Usage: 0x%04X:0x%04X\n", caps.UsagePage, caps.Usage);
    printf("  Input:  len=%d, count=%d\n", caps.InputReportByteLength, caps.NumberInputValueCaps);
    printf("  Output: len=%d, count=%d\n", caps.OutputReportByteLength, caps.NumberOutputValueCaps);
    printf("  Feature: len=%d, count=%d\n", caps.FeatureReportByteLength, caps.NumberFeatureValueCaps);

    // Dump all input value capabilities
    USHORT numVals = caps.NumberInputValueCaps;
    if (numVals > 0) {
        HIDP_VALUE_CAPS* vcaps = (HIDP_VALUE_CAPS*)malloc(numVals * sizeof(HIDP_VALUE_CAPS));
        if (vcaps && HidP_GetValueCaps(HidP_Input, vcaps, &numVals, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numVals; i++) {
                printf("  Input Value[%d]: UP=0x%04X U=0x%04X bitSize=%d count=%d logical=%d..%d reportID=0x%02X\n",
                    i, vcaps[i].UsagePage, vcaps[i].Range.UsageMin,
                    vcaps[i].BitSize, vcaps[i].ReportCount,
                    vcaps[i].LogicalMin, vcaps[i].LogicalMax,
                    vcaps[i].ReportID);
            }
        }
        free(vcaps);
    }

    // Dump all input button capabilities
    USHORT numBtns = caps.NumberInputButtonCaps;
    if (numBtns > 0) {
        HIDP_BUTTON_CAPS* bcaps = (HIDP_BUTTON_CAPS*)malloc(numBtns * sizeof(HIDP_BUTTON_CAPS));
        if (bcaps && HidP_GetButtonCaps(HidP_Input, bcaps, &numBtns, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numBtns; i++) {
                printf("  Input Button[%d]: UP=0x%04X U=0x%04X-0x%04X bits=%d repID=0x%02X\n",
                    i, bcaps[i].UsagePage, bcaps[i].Range.UsageMin, bcaps[i].Range.UsageMax,
                    bcaps[i].BitField, bcaps[i].ReportID);
            }
        }
        free(bcaps);
    }

    // Dump output value capabilities
    numVals = caps.NumberOutputValueCaps;
    if (numVals > 0) {
        HIDP_VALUE_CAPS* vcaps = (HIDP_VALUE_CAPS*)malloc(numVals * sizeof(HIDP_VALUE_CAPS));
        if (vcaps && HidP_GetValueCaps(HidP_Output, vcaps, &numVals, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numVals; i++) {
                printf("  Output Value[%d]: UP=0x%04X U=0x%04X bitSize=%d count=%d logical=%d..%d repID=0x%02X\n",
                    i, vcaps[i].UsagePage, vcaps[i].Range.UsageMin,
                    vcaps[i].BitSize, vcaps[i].ReportCount,
                    vcaps[i].LogicalMin, vcaps[i].LogicalMax,
                    vcaps[i].ReportID);
            }
        }
        free(vcaps);
    }

    // Dump output button capabilities
    numBtns = caps.NumberOutputButtonCaps;
    if (numBtns > 0) {
        HIDP_BUTTON_CAPS* bcaps = (HIDP_BUTTON_CAPS*)malloc(numBtns * sizeof(HIDP_BUTTON_CAPS));
        if (bcaps && HidP_GetButtonCaps(HidP_Output, bcaps, &numBtns, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numBtns; i++) {
                printf("  Output Button[%d]: UP=0x%04X U=0x%04X-0x%04X bits=%d repID=0x%02X\n",
                    i, bcaps[i].UsagePage, bcaps[i].Range.UsageMin, bcaps[i].Range.UsageMax,
                    bcaps[i].BitField, bcaps[i].ReportID);
            }
        }
        free(bcaps);
    }

    // Dump feature value capabilities
    numVals = caps.NumberFeatureValueCaps;
    if (numVals > 0) {
        HIDP_VALUE_CAPS* vcaps = (HIDP_VALUE_CAPS*)malloc(numVals * sizeof(HIDP_VALUE_CAPS));
        if (vcaps && HidP_GetValueCaps(HidP_Feature, vcaps, &numVals, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numVals; i++) {
                printf("  Feature Value[%d]: UP=0x%04X U=0x%04X bitSize=%d count=%d logical=%d..%d repID=0x%02X\n",
                    i, vcaps[i].UsagePage, vcaps[i].Range.UsageMin,
                    vcaps[i].BitSize, vcaps[i].ReportCount,
                    vcaps[i].LogicalMin, vcaps[i].LogicalMax,
                    vcaps[i].ReportID);
            }
        }
        free(vcaps);
    }

    // Dump feature button capabilities
    numBtns = caps.NumberFeatureButtonCaps;
    if (numBtns > 0) {
        HIDP_BUTTON_CAPS* bcaps = (HIDP_BUTTON_CAPS*)malloc(numBtns * sizeof(HIDP_BUTTON_CAPS));
        if (bcaps && HidP_GetButtonCaps(HidP_Feature, bcaps, &numBtns, preparsed) == HIDP_STATUS_SUCCESS) {
            for (int i = 0; i < numBtns; i++) {
                printf("  Feature Button[%d]: UP=0x%04X U=0x%04X-0x%04X bits=%d repID=0x%02X\n",
                    i, bcaps[i].UsagePage, bcaps[i].Range.UsageMin, bcaps[i].Range.UsageMax,
                    bcaps[i].BitField, bcaps[i].ReportID);
            }
        }
        free(bcaps);
    }

    // Try reading feature reports for all discovered report IDs
    BYTE featBuf[1024];
    for (int rid = 0; rid <= 0x1F; rid++) {
        memset(featBuf, 0, sizeof(featBuf));
        featBuf[0] = (BYTE)rid;
        if (HidD_GetFeature(hDev, featBuf, sizeof(featBuf))) {
            int lastNZ = (int)caps.FeatureReportByteLength - 1;
            if (lastNZ >= 0) {
                while (lastNZ >= 0 && featBuf[lastNZ] == 0) lastNZ--;
                if (lastNZ >= 0) {
                    printf("  Feature[0x%02X]:", rid);
                    for (int j = 0; j <= lastNZ; j++) printf(" %02x", featBuf[j]);
                    printf("\n");
                }
            }
        }
    }

    HidD_FreePreparsedData(preparsed);
}

int main() {
    printf("ATK Mouse HID Descriptor Analysis\n");
    printf("==================================\n");

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return 1;

    SP_DEVICE_INTERFACE_DATA ifData = { sizeof(ifData) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, i, &ifData); i++) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, NULL, 0, &reqSize, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) continue;

        SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(reqSize);
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, NULL, NULL)) {
            free(detail);
            continue;
        }

        // Check VID/PID from device path (case-insensitive)
        WCHAR* path = detail->DevicePath;
        // Convert to uppercase for matching
        WCHAR pathUpper[1024];
        wcsncpy(pathUpper, path, 1023);
        pathUpper[1023] = 0;
        for (int k = 0; pathUpper[k]; k++) if (pathUpper[k] >= 'a' && pathUpper[k] <= 'z') pathUpper[k] -= 32;

        WCHAR* vidStr = wcsstr(pathUpper, L"VID_373B");
        WCHAR* pidStr = wcsstr(pathUpper, L"PID_101B");
        if (!vidStr || !pidStr) { free(detail); continue; }

        // Extract MI and Col
        char label[128] = "Unknown";
        WCHAR* miStr = wcsstr(pathUpper, L"MI_");
        WCHAR* colStr = wcsstr(pathUpper, L"COL");
        if (miStr) {
            int mi = wcstol(miStr + 3, NULL, 16);
            if (colStr) {
                int col = wcstol(colStr + 3, NULL, 16);
                snprintf(label, sizeof(label), "MI_%02X&Col%02X", mi, col);
            } else {
                snprintf(label, sizeof(label), "MI_%02X", mi);
            }
        }

        HANDLE hDev = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDev == INVALID_HANDLE_VALUE) {
            // Try read-only
            hDev = CreateFileW(path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        }
        if (hDev == INVALID_HANDLE_VALUE) {
            free(detail);
            continue;
        }

        // Get product string
        WCHAR product[256] = {0};
        HidD_GetProductString(hDev, product, sizeof(product)/sizeof(WCHAR));

        printf("\n--- %s: %ls ---\n", label, product);

        DumpCapabilities(hDev, label);

        // Also dump raw HID descriptor bytes
        BYTE descBuf[4096];
        if (HidD_GetPhysicalDescriptor(hDev, descBuf, sizeof(descBuf))) {
            printf("  Physical descriptor available\n");
        }

        CloseHandle(hDev);
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);

    // Check for Battery class devices
    printf("\n\n=== Windows Battery Devices ===\n");
    // {BA126556-66CF-4cec-B8B8-109C538A30DD} is GUID_DEVCLASS_BATTERY
    GUID batteryGuid = {0xBA126556, 0x66CF, 0x4cec, {0xB8, 0xB8, 0x10, 0x9C, 0x53, 0x8A, 0x30, 0xDD}};
    HDEVINFO batInfo = SetupDiGetClassDevsW(&batteryGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (batInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA batIf = { sizeof(batIf) };
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(batInfo, NULL, &batteryGuid, i, &batIf); i++) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(batInfo, &batIf, NULL, 0, &reqSize, NULL);
            SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(reqSize);
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(batInfo, &batIf, detail, reqSize, NULL, NULL)) {
                printf("  Battery device: %ls\n", detail->DevicePath);
            }
            free(detail);
        }
        SetupDiDestroyDeviceInfoList(batInfo);
    }

    printf("\nDone.\n");
    return 0;
}
