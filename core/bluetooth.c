#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>

#include "bluetooth.h"

#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid = 2 */
static const DEVPROPKEY OCTANS_DEVPKEY_BT_BATTERY = {
    {0x104EA319u, 0x6EE2u, 0x4701u, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}},
    2
};

/* {e0cbf06c-cd8b-4647-bb8a-263b43f0f974} */
static const GUID OCTANS_GUID_DEVCLASS_BLUETOOTH = {
    0xE0CBF06Cu, 0xCD8Bu, 0x4647u, {0xBB, 0x8A, 0x26, 0x3B, 0x43, 0xF0, 0xF9, 0x74}
};

/* {4d36e97d-e325-11ce-bfc1-08002be10318} */
static const GUID OCTANS_GUID_DEVCLASS_SYSTEM = {
    0x4D36E97Du, 0xE325u, 0x11CEu, {0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18}
};

static int contains_nocase(const wchar_t *haystack, const wchar_t *needle) {
    size_t nlen = wcslen(needle);
    if (nlen == 0) return 1;
    for (const wchar_t *p = haystack; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) return 1;
    }
    return 0;
}

static void strip_suffix_nocase(wchar_t *s, const wchar_t *suffix) {
    size_t slen = wcslen(s);
    size_t suflen = wcslen(suffix);
    if (slen >= suflen && _wcsicmp(s + slen - suflen, suffix) == 0) {
        s[slen - suflen] = L'\0';
    }
}

static void clean_bt_name(wchar_t *name) {
    strip_suffix_nocase(name, L" Hands-Free AG Audio");
    strip_suffix_nocase(name, L" Hands-Free");
    strip_suffix_nocase(name, L" 免提音频");
    strip_suffix_nocase(name, L" 免提");
    size_t len = wcslen(name);
    while (len > 0 && iswspace((wint_t)name[len - 1])) {
        name[--len] = L'\0';
    }
}

static int is_hex_wch(wchar_t c) {
    return (c >= L'0' && c <= L'9') ||
           (c >= L'a' && c <= L'f') ||
           (c >= L'A' && c <= L'F');
}

static int copy_hex12(const wchar_t *p, char out_mac[16]) {
    int non_zero = 0;
    for (int i = 0; i < 12; ++i) {
        if (!is_hex_wch(p[i])) return 0;
        wchar_t c = p[i];
        if (c >= L'A' && c <= L'F') c = (wchar_t)(c - L'A' + L'a');
        if (c != L'0') non_zero = 1;
        out_mac[i] = (char)c;
    }
    if (!non_zero) return 0;
    /* Ensure token boundary after 12 hex digits */
    wchar_t next = p[12];
    if (is_hex_wch(next)) return 0;
    out_mac[12] = '\0';
    return 1;
}

int octans_bt_extract_mac(const wchar_t *inst_id, char out_mac[16]) {
    if (!out_mac) return 0;
    out_mac[0] = '\0';
    if (!inst_id || !inst_id[0]) return 0;

    /* 1. Direct DEV_<12hex> pattern (BTHLE\DEV_A1B2C3D4E5F6\... or BTHENUM\DEV_...) */
    for (const wchar_t *p = inst_id; *p; ++p) {
        if (_wcsnicmp(p, L"DEV_", 4) == 0 && copy_hex12(p + 4, out_mac)) {
            return 1;
        }
    }

    /* 2. Trailing _<12hex> token before '\' or end (BTHLEDEVICE\{...}_VID&02046D_PID&B034_REV&0012_A1B2C3D4E5F6\...) */
    for (const wchar_t *p = inst_id; *p; ++p) {
        if (*p == L'_' && copy_hex12(p + 1, out_mac)) {
            wchar_t after = p[13];
            if (after == L'\0' || after == L'\\' || after == L'&' || after == L'_') {
                return 1;
            }
        }
    }

    out_mac[0] = '\0';
    return 0;
}

static int find_bt_idx(const octans_bt_battery_t *arr,
                       size_t count,
                       const wchar_t *inst_id,
                       const char *mac,
                       const wchar_t *clean_name) {
    for (size_t i = 0; i < count; ++i) {
        if (_wcsicmp(arr[i].instance_id, inst_id) == 0) return (int)i;
        if (mac && mac[0] && arr[i].mac[0] && strcmp(arr[i].mac, mac) == 0) return (int)i;
        if (clean_name && clean_name[0] && arr[i].name[0] &&
            _wcsicmp(arr[i].name, clean_name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

size_t octans_bt_enumerate(octans_bt_battery_t *out, size_t max_count) {
    if (!out || max_count == 0) return 0;

    struct {
        const GUID *guid;
        const wchar_t *enumerator;
        DWORD flags;
    } passes[4] = {
        {NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT},
        {NULL, L"BTHENUM", DIGCF_PRESENT},
        {&OCTANS_GUID_DEVCLASS_SYSTEM, NULL, DIGCF_PRESENT},
        {&OCTANS_GUID_DEVCLASS_BLUETOOTH, NULL, DIGCF_PRESENT},
    };

    size_t count = 0;
    for (int p = 0; p < 4; ++p) {
        HDEVINFO hdev = SetupDiGetClassDevsW(
            passes[p].guid, passes[p].enumerator, NULL, passes[p].flags);
        if (hdev == INVALID_HANDLE_VALUE) continue;

        SP_DEVINFO_DATA info;
        DWORD idx = 0;
        while (count < max_count) {
            memset(&info, 0, sizeof(info));
            info.cbSize = sizeof(info);
            if (!SetupDiEnumDeviceInfo(hdev, idx, &info)) break;
            ++idx;

            wchar_t inst_id[128];
            inst_id[0] = L'\0';
            if (!SetupDiGetDeviceInstanceIdW(hdev, &info, inst_id, 128, NULL)) continue;
            if (!contains_nocase(inst_id, L"BTH")) continue;

            char mac[16];
            octans_bt_extract_mac(inst_id, mac);
            if (find_bt_idx(out, count, inst_id, mac, NULL) >= 0) continue;

            DEVPROPTYPE ptype = 0;
            uint8_t val = 0;
            DWORD need = 0;
            BOOL has_prop = SetupDiGetDevicePropertyW(
                hdev, &info, &OCTANS_DEVPKEY_BT_BATTERY, &ptype, &val, 1, &need, 0);
            if (!has_prop || ptype != DEVPROP_TYPE_BYTE) continue;

            wchar_t name[64];
            name[0] = L'\0';
            if (!SetupDiGetDeviceRegistryPropertyW(
                    hdev, &info, SPDRP_FRIENDLYNAME, NULL, (PBYTE)name, sizeof(name), NULL) ||
                name[0] == L'\0') {
                SetupDiGetDeviceRegistryPropertyW(
                    hdev, &info, SPDRP_DEVICEDESC, NULL, (PBYTE)name, sizeof(name), NULL);
            }
            name[63] = L'\0';

            octans_bt_battery_t item;
            memset(&item, 0, sizeof(item));
            wcsncpy(item.instance_id, inst_id, 127);
            item.instance_id[127] = L'\0';
            strncpy(item.mac, mac, sizeof(item.mac) - 1);
            item.mac[sizeof(item.mac) - 1] = '\0';

            int hf = contains_nocase(name, L"hands-free") ||
                     contains_nocase(name, L"handsfree") ||
                     contains_nocase(name, L"免提");
            clean_bt_name(name);
            wcsncpy(item.name, name[0] ? name : L"Bluetooth Device", 63);
            item.name[63] = L'\0';

            if (find_bt_idx(out, count, inst_id, mac, name[0] ? item.name : NULL) >= 0) continue;

            item.has_property = 1;
            item.is_handsfree = hf;
            /* Bluetooth HFP writes 0 when disconnected or not reporting. */
            item.battery_percent = (val > 0) ? (int8_t)(val <= 100 ? val : 100) : -1;

            if (item.battery_percent >= 0) {
                out[count++] = item;
            }
        }
        SetupDiDestroyDeviceInfoList(hdev);
    }

    return count;
}
