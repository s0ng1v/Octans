#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "hid_enum.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int hex_nibble(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return 10 + (ch - L'a');
    if (ch >= L'A' && ch <= L'F') return 10 + (ch - L'A');
    return -1;
}

static int parse_hex4_after(const wchar_t *haystack, const wchar_t *needle, uint16_t *out_val) {
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = haystack; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) {
            const wchar_t *h = p + nlen;
            int n0 = hex_nibble(h[0]);
            int n1 = n0 >= 0 ? hex_nibble(h[1]) : -1;
            int n2 = n1 >= 0 ? hex_nibble(h[2]) : -1;
            int n3 = n2 >= 0 ? hex_nibble(h[3]) : -1;
            if (n3 >= 0) {
                *out_val = (uint16_t)((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
                return 1;
            }
        }
    }
    return 0;
}

static int parse_mi_index(const wchar_t *path) {
    for (const wchar_t *p = path; *p; ++p) {
        if (_wcsnicmp(p, L"mi_", 3) == 0) {
            int n0 = hex_nibble(p[3]);
            int n1 = n0 >= 0 ? hex_nibble(p[4]) : -1;
            if (n1 >= 0) {
                return (n0 << 4) | n1;
            }
        }
    }
    return -1;
}

static int path_is_bluetooth(const wchar_t *path) {
    for (const wchar_t *p = path; *p; ++p) {
        if (_wcsnicmp(p, L"bthenum", 7) == 0 ||
            _wcsnicmp(p, L"bthledevice", 11) == 0 ||
            _wcsnicmp(p, L"vid&0002", 8) == 0 ||
            _wcsnicmp(p, L"vid&0001", 8) == 0 ||
            _wcsnicmp(p, L"{00001812", 9) == 0) {
            return 1;
        }
    }
    return 0;
}

static void trim_wide(wchar_t *s) {
    size_t len = wcslen(s);
    while (len > 0 && iswspace((wint_t)s[len - 1])) {
        s[--len] = L'\0';
    }
    size_t start = 0;
    while (s[start] && iswspace((wint_t)s[start])) {
        ++start;
    }
    if (start > 0) {
        memmove(s, s + start, (len - start + 1) * sizeof(wchar_t));
    }
}

static void query_hid_string(HANDLE h,
                             BOOLEAN (WINAPI *fn)(HANDLE, PVOID, ULONG),
                             wchar_t *out,
                             size_t out_chars) {
    wchar_t tmp[128];
    memset(tmp, 0, sizeof(tmp));
    out[0] = L'\0';
    if (fn(h, tmp, (ULONG)sizeof(tmp))) {
        tmp[127] = L'\0';
        trim_wide(tmp);
        wcsncpy(out, tmp, out_chars - 1);
        out[out_chars - 1] = L'\0';
    }
}

static void build_group_key(const wchar_t *path, char *out, size_t out_cap) {
    char utf8[512];
    octans_wide_to_utf8(path, utf8, sizeof(utf8));
    for (size_t i = 0; utf8[i]; ++i) {
        utf8[i] = (char)tolower((unsigned char)utf8[i]);
    }

    char *hash1 = strchr(utf8, '#');
    char *seg = hash1 ? (hash1 + 1) : utf8;
    char *hash2 = strchr(seg, '#');
    if (hash2) *hash2 = '\0';

    out[0] = '\0';
    size_t pos = 0;
    char *tok = seg;
    while (tok && *tok) {
        char *amp = strchr(tok, '&');
        if (amp) *amp = '\0';
        if (strncmp(tok, "col", 3) != 0 && strncmp(tok, "mi_", 3) != 0) {
            size_t tlen = strlen(tok);
            if (pos > 0 && pos + 1 < out_cap) {
                out[pos++] = '&';
            }
            size_t avail = (out_cap > pos + 1) ? (out_cap - 1 - pos) : 0;
            size_t copy_n = tlen < avail ? tlen : avail;
            if (copy_n > 0) {
                memcpy(out + pos, tok, copy_n);
                pos += copy_n;
            }
        }
        tok = amp ? (amp + 1) : NULL;
    }
    out[pos] = '\0';
}

int octans_hid_is_exclusive_usage(uint16_t usage_page, uint16_t usage) {
    if (usage_page == 0x0001) {
        return usage == 0x0001 || usage == 0x0002 || usage == 0x0006 || usage == 0x0007;
    }
    if (usage_page == 0x000D) {
        return usage == 0x0001 || usage == 0x0002 || usage == 0x0004 || usage == 0x0005;
    }
    return 0;
}

void octans_wide_to_utf8(const wchar_t *src, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    dst[0] = '\0';
    if (!src || !src[0]) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_cap, NULL, NULL);
    if (n <= 0) {
        dst[dst_cap - 1] = '\0';
    }
}

static void probe_single_interface(const wchar_t *path, octans_hid_info_t *info) {
    memset(info, 0, sizeof(*info));
    wcsncpy(info->path, path, 511);
    info->path[511] = L'\0';
    info->interface_num = parse_mi_index(path);
    info->is_bluetooth = path_is_bluetooth(path);

    if (!parse_hex4_after(path, L"vid_", &info->vid)) {
        if (!parse_hex4_after(path, L"vid&0002", &info->vid)) {
            if (!parse_hex4_after(path, L"vid&0001", &info->vid)) {
                if (!parse_hex4_after(path, L"vid&02", &info->vid)) {
                    parse_hex4_after(path, L"vid&01", &info->vid);
                }
            }
        }
    }
    if (!parse_hex4_after(path, L"pid_", &info->pid)) {
        parse_hex4_after(path, L"pid&", &info->pid);
    }

    build_group_key(path, info->group_key, sizeof(info->group_key));

    /* Always open with dwDesiredAccess = 0 during enumeration (zero-interference). */
    HANDLE h = CreateFileW(path,
                           0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           0,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        info->open_error = (int)GetLastError();
        return;
    }

    HIDD_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.Size = sizeof(attrs);
    if (HidD_GetAttributes(h, &attrs)) {
        if (attrs.VendorID != 0) info->vid = attrs.VendorID;
        if (attrs.ProductID != 0) info->pid = attrs.ProductID;
        info->version = attrs.VersionNumber;
    }

    query_hid_string(h, HidD_GetProductString, info->product, 64);
    query_hid_string(h, HidD_GetManufacturerString, info->manufacturer, 64);
    query_hid_string(h, HidD_GetSerialNumberString, info->serial, 64);

    PHIDP_PREPARSED_DATA preparsed = NULL;
    if (HidD_GetPreparsedData(h, &preparsed) && preparsed != NULL) {
        HIDP_CAPS caps;
        memset(&caps, 0, sizeof(caps));
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            info->usage_page = caps.UsagePage;
            info->usage = caps.Usage;
            info->input_len = caps.InputReportByteLength;
            info->output_len = caps.OutputReportByteLength;
            info->feature_len = caps.FeatureReportByteLength;
            info->num_input_value_caps = caps.NumberInputValueCaps;
            info->num_output_value_caps = caps.NumberOutputValueCaps;
            info->num_feature_value_caps = caps.NumberFeatureValueCaps;
            info->is_system_exclusive = octans_hid_is_exclusive_usage(caps.UsagePage, caps.Usage);
        }
        HidD_FreePreparsedData(preparsed);
    }

    CloseHandle(h);
}

size_t octans_hid_enumerate(octans_hid_info_t *out, size_t max_count) {
    if (!out || max_count == 0) return 0;

    GUID hid_guid;
    HidD_GetHidGuid(&hid_guid);

    HDEVINFO dev_info = SetupDiGetClassDevsW(
        &hid_guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return 0;
    }

    size_t count = 0;
    DWORD index = 0;
    SP_DEVICE_INTERFACE_DATA iface_data;
    uint8_t detail_buf[2048];

    while (count < max_count) {
        memset(&iface_data, 0, sizeof(iface_data));
        iface_data.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
        if (!SetupDiEnumDeviceInterfaces(dev_info, NULL, &hid_guid, index, &iface_data)) {
            break;
        }
        ++index;

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, NULL, 0, &required, NULL);
        if (required == 0 || required > sizeof(detail_buf)) {
            continue;
        }

        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail =
            (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)detail_buf;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(
                dev_info, &iface_data, detail, required, NULL, NULL)) {
            continue;
        }

        probe_single_interface(detail->DevicePath, &out[count]);
        ++count;
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return count;
}
