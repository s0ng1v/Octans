#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "xinput.h"

#include <string.h>

#define OCTANS_XINPUT_BATTERY_DEVTYPE_GAMEPAD 0x00u

#define OCTANS_BATTERY_TYPE_DISCONNECTED 0x00u
#define OCTANS_BATTERY_TYPE_WIRED        0x01u
#define OCTANS_BATTERY_TYPE_ALKALINE     0x02u
#define OCTANS_BATTERY_TYPE_NIMH         0x03u

#define OCTANS_BATTERY_LEVEL_EMPTY  0x00u
#define OCTANS_BATTERY_LEVEL_LOW    0x01u
#define OCTANS_BATTERY_LEVEL_MEDIUM 0x02u
#define OCTANS_BATTERY_LEVEL_FULL   0x03u

typedef struct octans_xinput_batt_info {
    BYTE BatteryType;
    BYTE BatteryLevel;
} octans_xinput_batt_info_t;

typedef DWORD (WINAPI *pfn_XInputGetBatteryInformation)(
    DWORD dwUserIndex, BYTE devType, octans_xinput_batt_info_t *pBatteryInformation);

int octans_xinput_map_battery(uint8_t type, uint8_t level, int *out_pct, int *out_wired) {
    if (out_pct) *out_pct = -1;
    if (out_wired) *out_wired = 0;

    if (type == OCTANS_BATTERY_TYPE_DISCONNECTED) {
        return 0;
    }
    if (type == OCTANS_BATTERY_TYPE_WIRED) {
        if (out_wired) *out_wired = 1;
        return 0;
    }
    if (type != OCTANS_BATTERY_TYPE_ALKALINE && type != OCTANS_BATTERY_TYPE_NIMH) {
        return 0;
    }

    int pct;
    switch (level) {
    case OCTANS_BATTERY_LEVEL_EMPTY:  pct = 5; break;
    case OCTANS_BATTERY_LEVEL_LOW:    pct = 25; break;
    case OCTANS_BATTERY_LEVEL_MEDIUM: pct = 60; break;
    case OCTANS_BATTERY_LEVEL_FULL:   pct = 100; break;
    default:                          return 0;
    }
    if (out_pct) *out_pct = pct;
    return 1;
}

size_t octans_xinput_enumerate(octans_xinput_battery_t *out, size_t max_count) {
    if (!out || max_count == 0) return 0;

    HMODULE mod = LoadLibraryW(L"xinput1_4.dll");
    if (!mod) mod = LoadLibraryW(L"xinput9_1_0.dll");
    if (!mod) return 0;

    /* Cast through void* to avoid -Wcast-function-type under -Werror. */
    union {
        FARPROC raw;
        pfn_XInputGetBatteryInformation fn;
    } u;
    u.raw = GetProcAddress(mod, "XInputGetBatteryInformation");
    if (!u.fn) {
        FreeLibrary(mod);
        return 0;
    }

    size_t count = 0;
    for (DWORD slot = 0; slot < OCTANS_MAX_XINPUT_SLOTS && count < max_count; ++slot) {
        octans_xinput_batt_info_t bi;
        memset(&bi, 0, sizeof(bi));
        if (u.fn(slot, OCTANS_XINPUT_BATTERY_DEVTYPE_GAMEPAD, &bi) != ERROR_SUCCESS) {
            continue;
        }
        int pct = -1;
        int wired = 0;
        if (!octans_xinput_map_battery(bi.BatteryType, bi.BatteryLevel, &pct, &wired)) {
            continue;
        }
        octans_xinput_battery_t item;
        memset(&item, 0, sizeof(item));
        item.slot = (uint8_t)slot;
        item.battery_type = bi.BatteryType;
        item.battery_percent = (int8_t)pct;
        item.is_wired = wired;
        out[count++] = item;
    }

    FreeLibrary(mod);
    return count;
}
