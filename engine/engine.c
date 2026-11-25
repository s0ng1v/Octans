#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "octans_core.h"
#include "../core/bluetooth.h"
#include "../core/hid_dev.h"
#include "../core/hid_enum.h"
#include "../core/hid_parse.h"
#include "../core/hotplug.h"
#include "../core/xinput.h"
#include "family_request.h"
#include "hidpp.h"
#include "merge.h"
#include "prototable.h"
#include "user_rules.h"
#include "broad_scan.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

struct octans_engine {
    octans_config_t cfg;
    octans_hotplug_snapshot_t hotplug;
    octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
    size_t iface_count;
    octans_merge_state_t merge;
    int quit_requested;
    wchar_t ini_path[MAX_PATH];
    wchar_t devices_ini_path[MAX_PATH];
    octans_user_rule_t user_rules[OCTANS_MAX_USER_RULES];
    size_t user_rule_count;
    octans_broad_cache_t broad_cache;
};

static const char *kind_to_str(octans_device_kind_t k) {
    switch (k) {
    case OCTANS_DEV_MOUSE: return "mouse";
    case OCTANS_DEV_KEYBOARD: return "keyboard";
    case OCTANS_DEV_HEADSET: return "headset";
    case OCTANS_DEV_EARBUDS: return "earbuds";
    case OCTANS_DEV_GAMEPAD: return "gamepad";
    case OCTANS_DEV_STYLUS: return "stylus";
    case OCTANS_DEV_SPEAKER: return "speaker";
    case OCTANS_DEV_OTHER: return "other";
    default: return "unknown";
    }
}

static const char *conn_to_str(octans_conn_kind_t c) {
    switch (c) {
    case OCTANS_CONN_DONGLE_24G: return "2.4g";
    case OCTANS_CONN_BLUETOOTH: return "bluetooth";
    case OCTANS_CONN_WIRED: return "wired";
    default: return "unknown";
    }
}

static const char *power_to_str(octans_power_state_t p) {
    switch (p) {
    case OCTANS_PWR_DISCHARGING: return "discharging";
    case OCTANS_PWR_CHARGING: return "charging";
    case OCTANS_PWR_FULL: return "full";
    case OCTANS_PWR_SLEEPING: return "sleeping";
    case OCTANS_PWR_OFFLINE: return "offline";
    default: return "unknown";
    }
}

static void init_ini_path(wchar_t out_path[MAX_PATH], wchar_t out_devices_path[MAX_PATH]) {
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 32) {
        _snwprintf(out_path, MAX_PATH, L"%ls\\Octans", appdata);
        CreateDirectoryW(out_path, NULL);
        _snwprintf(out_path, MAX_PATH, L"%ls\\Octans\\config.ini", appdata);
        _snwprintf(out_devices_path, MAX_PATH, L"%ls\\Octans\\devices.ini", appdata);
    } else {
        wcscpy(out_path, L"octans.ini");
        wcscpy(out_devices_path, L"devices.ini");
    }
}

static void load_user_rules(octans_engine_t *eng) {
    if (!eng) return;
    eng->user_rule_count = octans_user_rules_load_file(
        eng->devices_ini_path, eng->user_rules, OCTANS_MAX_USER_RULES);

    /* Also load optional portable devices.ini next to executable */
    if (eng->user_rule_count < OCTANS_MAX_USER_RULES) {
        wchar_t exe_dir[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            wchar_t *sl = wcsrchr(exe_dir, L'\\');
            if (sl) {
                *sl = L'\0';
                wchar_t local_ini[MAX_PATH];
                _snwprintf(local_ini, MAX_PATH - 1, L"%ls\\devices.ini", exe_dir);
                local_ini[MAX_PATH - 1] = L'\0';
                if (_wcsicmp(local_ini, eng->devices_ini_path) != 0) {
                    eng->user_rule_count += octans_user_rules_load_file(
                        local_ini,
                        eng->user_rules + eng->user_rule_count,
                        OCTANS_MAX_USER_RULES - eng->user_rule_count);
                }
            }
        }
    }
}

static void load_persisted_pin(octans_engine_t *eng) {
    wchar_t wpin[64];
    wpin[0] = L'\0';
    GetPrivateProfileStringW(L"General", L"PinnedDevice", L"", wpin, 64, eng->ini_path);
    if (wpin[0]) {
        char utf8[64];
        octans_wide_to_utf8(wpin, utf8, sizeof(utf8));
        octans_merge_set_pin(&eng->merge, utf8);
    }
}

static void load_persisted_aliases(octans_engine_t *eng) {
    wchar_t sec[4096];
    DWORD n = GetPrivateProfileSectionW(L"Aliases", sec, 4096, eng->ini_path);
    if (n == 0) return;

    const wchar_t *p = sec;
    while (*p != L'\0') {
        const wchar_t *eq = wcschr(p, L'=');
        if (eq && eq > p && eq[1] != L'\0') {
            wchar_t w_id[64];
            size_t id_len = (size_t)(eq - p);
            if (id_len >= 64) id_len = 63;
            wmemcpy(w_id, p, id_len);
            w_id[id_len] = L'\0';

            char id_u8[64];
            octans_wide_to_utf8(w_id, id_u8, sizeof(id_u8));
            octans_merge_set_alias(&eng->merge, id_u8, eq + 1);
        }
        p += wcslen(p) + 1;
    }
}

static octans_device_kind_t guess_kind_from_iface(const octans_hid_info_t *info,
                                                  const octans_hid_info_t *all,
                                                  size_t count) {
    if (info->usage_page == 0x000Du) return OCTANS_DEV_STYLUS;
    if (info->usage_page == 0x0001u && info->usage == 0x0005u) return OCTANS_DEV_GAMEPAD;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(all[i].group_key, info->group_key) != 0) continue;
        if (all[i].usage_page == 0x0001u) {
            if (all[i].usage == 0x0002u) return OCTANS_DEV_MOUSE;
            if (all[i].usage == 0x0006u) return OCTANS_DEV_KEYBOARD;
            if (all[i].usage == 0x0005u) return OCTANS_DEV_GAMEPAD;
        }
    }
    return OCTANS_DEV_UNKNOWN;
}

octans_engine_t *octans_engine_create(const octans_config_t *cfg) {
    octans_engine_t *eng = (octans_engine_t *)calloc(1, sizeof(octans_engine_t));
    if (!eng) return NULL;
    if (cfg) {
        eng->cfg = *cfg;
    }
    if (eng->cfg.poll_interval_ms == 0) eng->cfg.poll_interval_ms = 15000u;
    if (eng->cfg.passive_listen_ms == 0) eng->cfg.passive_listen_ms = 250u;
    octans_merge_init(&eng->merge);
    init_ini_path(eng->ini_path, eng->devices_ini_path);
    load_persisted_pin(eng);
    load_persisted_aliases(eng);
    load_user_rules(eng);
    return eng;
}

void octans_engine_reload_rules(octans_engine_t *eng) {
    if (!eng) return;
    load_user_rules(eng);
    octans_broad_cache_clear(&eng->broad_cache);
}

void octans_engine_destroy(octans_engine_t *eng) {
    free(eng);
}

int octans_engine_poll(octans_engine_t *eng, int force_rescan) {
    if (!eng) return OCTANS_ERR_INVALID_ARG;

    if (force_rescan || octans_hotplug_changed(&eng->hotplug)) {
        eng->iface_count = octans_hid_enumerate(eng->ifaces, OCTANS_MAX_HID_IFACES);
        octans_broad_cache_clear(&eng->broad_cache);
        if (eng->cfg.debug_log) {
            fprintf(stderr, "[octans] enumerated %zu HID interface(s)\n", eng->iface_count);
        }
    }

    octans_proto_reading_t readings[64];
    size_t rcount = 0;

    int group_hit[OCTANS_MAX_HID_IFACES];
    memset(group_hit, 0, sizeof(group_hit));

    /* Priority 0: User/Auto external rules from devices.ini */
    for (size_t i = 0; i < eng->iface_count && rcount < 64; ++i) {
        const octans_hid_info_t *info = &eng->ifaces[i];
        if (info->vid == 0u || group_hit[i]) continue;
        for (size_t u = 0; u < eng->user_rule_count && rcount < 64; ++u) {
            if (!octans_user_rule_matches_iface(&eng->user_rules[u], info)) continue;
            octans_proto_reading_t r;
            if (octans_user_rule_query_iface(&eng->user_rules[u], info,
                                             eng->cfg.passive_listen_ms, &r, NULL, 0, NULL)) {
                readings[rcount++] = r;
                for (size_t k = 0; k < eng->iface_count; ++k) {
                    if (strcmp(eng->ifaces[k].group_key, info->group_key) == 0) {
                        group_hit[k] = 1;
                    }
                }
                if (eng->cfg.debug_log) {
                    fprintf(stderr, "[octans] rule hit %04X:%04X tag=%s batt=%d%%\n",
                            info->vid, info->pid, r.source_tag, (int)r.battery_percent);
                }
                break;
            }
        }
    }

    /* Family 2: Logitech HID++ */
    rcount += octans_hidpp_poll_all(eng->ifaces, eng->iface_count, readings + rcount, 64u - rcount);

    /* Families 3..15: Data-driven protocol table */
    for (size_t i = 0; i < eng->iface_count; ++i) {
        if (eng->ifaces[i].vid == OCTANS_LOGITECH_VID &&
            eng->ifaces[i].pid != 0x0B1Cu &&
            eng->ifaces[i].pid != 0x0AF7u) {
            group_hit[i] = 1;
        }
    }

    for (size_t i = 0; i < eng->iface_count && rcount < 64; ++i) {
        const octans_hid_info_t *info = &eng->ifaces[i];
        if (info->vid == 0u || group_hit[i]) continue;

        for (size_t p = 0; p < g_protocols_count && rcount < 64; ++p) {
            if (!octans_proto_matches_iface(&g_protocols[p], info)) continue;
            octans_proto_reading_t r;
            if (octans_proto_query_iface(&g_protocols[p], info, eng->cfg.passive_listen_ms,
                                         eng->cfg.debug_log, &r)) {
                readings[rcount++] = r;
                for (size_t k = 0; k < eng->iface_count; ++k) {
                    if (strcmp(eng->ifaces[k].group_key, info->group_key) == 0) {
                        group_hit[k] = 1;
                    }
                }
                if (eng->cfg.debug_log) {
                    fprintf(stderr, "[octans] hit %04X:%04X tag=%s batt=%d%%\n",
                            info->vid, info->pid, r.source_tag, (int)r.battery_percent);
                }
                break;
            }
        }
    }

    /* Family 1: Standard HID Battery Usage fallback for groups without vendor hit */
    for (size_t i = 0; i < eng->iface_count && rcount < 64; ++i) {
        const octans_hid_info_t *info = &eng->ifaces[i];
        if (group_hit[i] || info->feature_len == 0 || info->vid == 0u) continue;

        octans_hid_dev_t dev;
        if (octans_hid_open(info->path, 0, 0, &dev) < 0) continue;
        int pct = -1;
        int sig = -1;
        uint16_t up = 0, u = 0;
        if (octans_hid_read_std_battery(&dev, &pct, &sig, &up, &u) == 0 && pct >= 0) {
            octans_proto_reading_t r;
            octans_reading_init(&r);
            r.valid = 1;
            r.vid = info->vid;
            r.pid = info->pid;
            r.kind = guess_kind_from_iface(info, eng->ifaces, eng->iface_count);
            r.conn = info->is_bluetooth ? OCTANS_CONN_BLUETOOTH : OCTANS_CONN_DONGLE_24G;
            r.power_state = OCTANS_PWR_DISCHARGING;
            r.battery_percent = (int8_t)pct;
            r.signal_percent = (int8_t)sig;
            if (info->manufacturer[0]) {
                wcsncpy(r.brand, info->manufacturer, 31);
                r.brand[31] = L'\0';
            }
            if (info->product[0]) {
                wcsncpy(r.name, info->product, 63);
                r.name[63] = L'\0';
            } else {
                _snwprintf(r.name, 64, L"HID %04X:%04X", info->vid, info->pid);
            }
            snprintf(r.stable_id, sizeof(r.stable_id), "std:%04x:%s", info->vid, info->group_key);
            snprintf(r.source_tag, sizeof(r.source_tag), "std_%04x_%04x", up, u);
            readings[rcount++] = r;
            for (size_t k = 0; k < eng->iface_count; ++k) {
                if (strcmp(eng->ifaces[k].group_key, info->group_key) == 0) group_hit[k] = 1;
            }
        }
        octans_hid_close(&dev);
    }

    /* Family 16: Windows Bluetooth DEVPKEY_Bluetooth_Battery */
    octans_bt_battery_t bts[OCTANS_MAX_BT_DEVICES];
    size_t bt_count = octans_bt_enumerate(bts, OCTANS_MAX_BT_DEVICES);
    for (size_t i = 0; i < bt_count && rcount < 64; ++i) {
        octans_proto_reading_t r;
        octans_reading_init(&r);
        r.valid = 1;
        r.kind = bts[i].is_handsfree ? OCTANS_DEV_HEADSET : OCTANS_DEV_OTHER;
        r.conn = OCTANS_CONN_BLUETOOTH;
        r.power_state = OCTANS_PWR_DISCHARGING;
        r.battery_percent = bts[i].battery_percent;
        wcsncpy(r.brand, L"Bluetooth", 31);
        wcsncpy(r.name, bts[i].name, 63);
        r.name[63] = L'\0';

        char inst_utf8[96];
        octans_wide_to_utf8(bts[i].instance_id, inst_utf8, sizeof(inst_utf8));
        snprintf(r.stable_id, sizeof(r.stable_id), "bt:%.*s", 58, inst_utf8);
        snprintf(r.source_tag, sizeof(r.source_tag), "bt_devpkey");
        readings[rcount++] = r;
    }

    /* XInput Gamepads (Xbox / GameSir / FlyDigi / Beitong / 8BitDo XInput) */
    octans_xinput_battery_t xdevs[OCTANS_MAX_XINPUT_SLOTS];
    size_t x_count = octans_xinput_enumerate(xdevs, OCTANS_MAX_XINPUT_SLOTS);
    for (size_t i = 0; i < x_count && rcount < 64; ++i) {
        octans_proto_reading_t r;
        octans_reading_init(&r);
        r.valid = 1;
        r.kind = OCTANS_DEV_GAMEPAD;
        r.conn = xdevs[i].is_wired ? OCTANS_CONN_WIRED : OCTANS_CONN_DONGLE_24G;
        r.power_state = OCTANS_PWR_DISCHARGING;
        r.battery_percent = xdevs[i].battery_percent;
        r.is_estimated = 1;
        wcsncpy(r.brand, L"Xbox / XInput", 31);
        _snwprintf(r.name, 64, L"XInput Controller #%u", (unsigned)(xdevs[i].slot + 1u));
        snprintf(r.stable_id, sizeof(r.stable_id), "xinput:slot%u", (unsigned)xdevs[i].slot);
        snprintf(r.source_tag, sizeof(r.source_tag), "xinput");
        readings[rcount++] = r;
    }

    /* Broad Discovery: probe unhit vendor-HID groups once and cache hits to devices.ini */
    if (rcount < 64) {
        size_t broad_n = octans_broad_scan_unknowns(
            eng->ifaces, eng->iface_count, group_hit, &eng->broad_cache,
            eng->devices_ini_path, eng->cfg.debug_log,
            readings + rcount, 64u - rcount);
        if (broad_n > 0) {
            rcount += broad_n;
            load_user_rules(eng);
        }
    }

    octans_merge_apply(&eng->merge, readings, rcount, GetTickCount64());

    if (eng->cfg.ui_notify) {
        octans_device_view_t views[OCTANS_MAX_DEVICES];
        size_t n = octans_merge_export(&eng->merge, views, OCTANS_MAX_DEVICES);
        eng->cfg.ui_notify(views, n);
    }

    return OCTANS_OK;
}

size_t octans_engine_get_devices(const octans_engine_t *eng,
                                 octans_device_view_t *out,
                                 size_t max_count) {
    if (!eng) return 0;
    return octans_merge_export(&eng->merge, out, max_count);
}

static int json_append(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
    if (*pos >= cap) return -1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos) return -1;
    *pos += (size_t)n;
    return 0;
}

static int json_append_escaped(char *buf, size_t cap, size_t *pos, const char *s) {
    if (json_append(buf, cap, pos, "\"") < 0) return -1;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        unsigned char c = *p;
        if (c == '\"' || c == '\\') {
            if (json_append(buf, cap, pos, "\\%c", (int)c) < 0) return -1;
        } else if (c == '\b') {
            if (json_append(buf, cap, pos, "\\b") < 0) return -1;
        } else if (c == '\f') {
            if (json_append(buf, cap, pos, "\\f") < 0) return -1;
        } else if (c == '\n') {
            if (json_append(buf, cap, pos, "\\n") < 0) return -1;
        } else if (c == '\r') {
            if (json_append(buf, cap, pos, "\\r") < 0) return -1;
        } else if (c == '\t') {
            if (json_append(buf, cap, pos, "\\t") < 0) return -1;
        } else if (c < 0x20u) {
            if (json_append(buf, cap, pos, "\\u%04x", (unsigned)c) < 0) return -1;
        } else {
            if (*pos + 1 >= cap) return -1;
            buf[(*pos)++] = (char)c;
        }
    }
    return json_append(buf, cap, pos, "\"");
}

int octans_engine_format_json(const octans_engine_t *eng, char *buf, size_t buf_cap) {
    if (!eng || !buf || buf_cap < 4) return OCTANS_ERR_INVALID_ARG;

    octans_device_view_t views[OCTANS_MAX_DEVICES];
    size_t count = octans_merge_export(&eng->merge, views, OCTANS_MAX_DEVICES);
    size_t pos = 0;

    if (json_append(buf, buf_cap, &pos, "{\"api_version\":%d,\"count\":%zu,\"devices\":[",
                    OCTANS_UI_API_VERSION, count) < 0) {
        return OCTANS_ERR_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < count; ++i) {
        const octans_device_view_t *v = &views[i];
        char name_utf8[192];
        char brand_utf8[96];
        octans_wide_to_utf8(v->name, name_utf8, sizeof(name_utf8));
        octans_wide_to_utf8(v->brand, brand_utf8, sizeof(brand_utf8));

        if (i > 0 && json_append(buf, buf_cap, &pos, ",") < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append(buf, buf_cap, &pos, "{\"id\":") < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append_escaped(buf, buf_cap, &pos, v->id) < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append(buf, buf_cap, &pos, ",\"name\":") < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append_escaped(buf, buf_cap, &pos, name_utf8) < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append(buf, buf_cap, &pos, ",\"brand\":") < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append_escaped(buf, buf_cap, &pos, brand_utf8) < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
        if (json_append(buf,
                        buf_cap,
                        &pos,
                        ",\"vid\":\"0x%04X\",\"pid\":\"0x%04X\",\"kind\":\"%s\",\"conn\":\"%s\","
                        "\"power_state\":\"%s\",\"battery_percent\":%d,\"voltage_mv\":%u,"
                        "\"signal_percent\":%d,\"is_low_battery\":%s,\"is_estimated\":%s,"
                        "\"updated_ms\":%llu}",
                        (unsigned)v->vid,
                        (unsigned)v->pid,
                        kind_to_str(v->kind),
                        conn_to_str(v->conn),
                        power_to_str(v->power_state),
                        (int)v->battery_percent,
                        (unsigned)v->voltage_mv,
                        (int)v->signal_percent,
                        v->is_low_battery ? "true" : "false",
                        v->is_estimated ? "true" : "false",
                        (unsigned long long)v->updated_ms) < 0) {
            return OCTANS_ERR_BUFFER_TOO_SMALL;
        }
    }

    if (json_append(buf, buf_cap, &pos, "]}") < 0) return OCTANS_ERR_BUFFER_TOO_SMALL;
    buf[pos] = '\0';
    return (int)pos;
}

static void ops_request_refresh(void *ctx) {
    octans_engine_t *eng = (octans_engine_t *)ctx;
    if (eng) octans_engine_poll(eng, 1);
}

static int ops_set_pinned_device(void *ctx, const char *device_id) {
    octans_engine_t *eng = (octans_engine_t *)ctx;
    if (!eng) return -1;
    octans_merge_set_pin(&eng->merge, device_id);
    wchar_t wpin[64];
    wpin[0] = L'\0';
    if (device_id && device_id[0]) {
        MultiByteToWideChar(CP_UTF8, 0, device_id, -1, wpin, 64);
        wpin[63] = L'\0';
    }
    WritePrivateProfileStringW(L"General", L"PinnedDevice", wpin, eng->ini_path);
    return 0;
}

/* Ensure config.ini uses UTF-16LE BOM so WritePrivateProfileStringW preserves Chinese aliases. */
static void ensure_ini_utf16le(const wchar_t *path) {
    if (!path || !path[0]) return;
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE) {
        CloseHandle(h);
        return;
    }
    if (sz == 0) {
        const uint8_t bom[2] = {0xFFu, 0xFEu};
        DWORD wr = 0;
        WriteFile(h, bom, 2, &wr, NULL);
        CloseHandle(h);
        return;
    }
    uint8_t head[2] = {0, 0};
    DWORD rd = 0;
    if (ReadFile(h, head, 2, &rd, NULL) && rd == 2 && head[0] == 0xFFu && head[1] == 0xFEu) {
        CloseHandle(h);
        return; /* Already UTF-16LE */
    }
    if (sz > 65536u) {
        CloseHandle(h);
        return;
    }
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    char *ansi = (char *)malloc((size_t)sz + 1u);
    if (ansi) {
        if (ReadFile(h, ansi, sz, &rd, NULL) && rd == sz) {
            ansi[sz] = '\0';
            int wlen = MultiByteToWideChar(CP_ACP, 0, ansi, (int)sz, NULL, 0);
            if (wlen > 0) {
                wchar_t *wbuf = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
                if (wbuf) {
                    MultiByteToWideChar(CP_ACP, 0, ansi, (int)sz, wbuf, wlen);
                    SetFilePointer(h, 0, NULL, FILE_BEGIN);
                    SetEndOfFile(h);
                    const uint8_t bom[2] = {0xFFu, 0xFEu};
                    DWORD wr = 0;
                    WriteFile(h, bom, 2, &wr, NULL);
                    WriteFile(h, wbuf, (DWORD)((size_t)wlen * sizeof(wchar_t)), &wr, NULL);
                    free(wbuf);
                }
            }
        }
        free(ansi);
    }
    CloseHandle(h);
}

static int ops_set_device_alias(void *ctx, const char *device_id, const wchar_t *alias) {
    octans_engine_t *eng = (octans_engine_t *)ctx;
    if (!eng || !device_id || !device_id[0]) return -1;
    int rc = octans_merge_set_alias(&eng->merge, device_id, alias);
    if (rc == 0) {
        ensure_ini_utf16le(eng->ini_path);
        wchar_t w_id[64];
        w_id[0] = L'\0';
        MultiByteToWideChar(CP_UTF8, 0, device_id, -1, w_id, 64);
        w_id[63] = L'\0';
        if (w_id[0]) {
            WritePrivateProfileStringW(L"Aliases", w_id, (alias && alias[0]) ? alias : NULL, eng->ini_path);
        }
    }
    return rc;
}

static int ops_get_autostart(void *ctx) {
    (void)ctx;
    HKEY hkey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0,
                      KEY_READ,
                      &hkey) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD type = 0;
    DWORD sz = 0;
    LONG rc = RegQueryValueExW(hkey, L"Octans", NULL, &type, NULL, &sz);
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS && type == REG_SZ && sz > sizeof(wchar_t)) ? 1 : 0;
}

static int ops_set_autostart(void *ctx, int enabled) {
    (void)ctx;
    HKEY hkey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0,
                      KEY_SET_VALUE,
                      &hkey) != ERROR_SUCCESS) {
        return -1;
    }
    LONG rc;
    if (enabled) {
        wchar_t exe[MAX_PATH];
        DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            RegCloseKey(hkey);
            return -1;
        }
        wchar_t cmd[MAX_PATH + 16];
        _snwprintf(cmd, MAX_PATH + 15, L"\"%ls\" --silent", exe);
        cmd[MAX_PATH + 15] = L'\0';
        rc = RegSetValueExW(hkey, L"Octans", 0, REG_SZ,
                            (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1u) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(hkey, L"Octans");
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(hkey);
    return (rc == ERROR_SUCCESS) ? 0 : -1;
}

static void ops_request_quit(void *ctx) {
    octans_engine_t *eng = (octans_engine_t *)ctx;
    if (eng) eng->quit_requested = 1;
}

void octans_engine_bind_ui(octans_engine_t *eng, octans_backend_ops_t *out_ops) {
    (void)eng;
    if (!out_ops) return;
    out_ops->request_refresh = ops_request_refresh;
    out_ops->set_pinned_device = ops_set_pinned_device;
    out_ops->set_device_alias = ops_set_device_alias;
    out_ops->set_autostart = ops_set_autostart;
    out_ops->get_autostart = ops_get_autostart;
    out_ops->request_quit = ops_request_quit;
}

int octans_engine_quit_requested(const octans_engine_t *eng) {
    return eng ? eng->quit_requested : 0;
}

static void print_hex_line(FILE *out, const uint8_t *data, size_t len) {
    size_t limit = len > 48 ? 48 : len;
    for (size_t i = 0; i < limit; ++i) {
        fprintf(out, "%02X ", data[i]);
    }
    if (len > limit) fprintf(out, "...");
}

int octans_probe_run(const octans_probe_opts_t *opts, FILE *out) {
    if (!out) out = stdout;
    octans_probe_opts_t def = {1, 0, 1};
    const octans_probe_opts_t *o = opts ? opts : &def;

    octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
    size_t n = octans_hid_enumerate(ifaces, OCTANS_MAX_HID_IFACES);
    fprintf(out, "Octans HID Probe: %zu interface(s) enumerated\n", n);

    for (size_t i = 0; i < n; ++i) {
        const octans_hid_info_t *it = &ifaces[i];
        char prod[192], mfr[192];
        octans_wide_to_utf8(it->product, prod, sizeof(prod));
        octans_wide_to_utf8(it->manufacturer, mfr, sizeof(mfr));

        fprintf(out,
                "[%02zu] VID=%04X PID=%04X MI=%02d UP=%04X:%04X IN=%u OUT=%u FEAT=%u %s%s MFR=\"%s\" PROD=\"%s\"\n",
                i,
                (unsigned)it->vid,
                (unsigned)it->pid,
                it->interface_num,
                (unsigned)it->usage_page,
                (unsigned)it->usage,
                (unsigned)it->input_len,
                (unsigned)it->output_len,
                (unsigned)it->feature_len,
                it->is_system_exclusive ? "EXCL " : "",
                it->is_bluetooth ? "BT " : "",
                mfr,
                prod);

        if (o->dump_caps || o->raw_feature_dump) {
            octans_hid_dev_t dev;
            /* Strict read-only probe: never send writes on probe! */
            int want_read = (o->raw_feature_dump && !it->is_system_exclusive &&
                             (it->usage_page >= 0xFF00u || it->usage_page == 0x000Au) &&
                             it->input_len > 0) ? 1 : 0;
            if (octans_hid_open(it->path, 0, want_read, &dev) == 0) {
                if (o->dump_caps) {
                    octans_hid_dump_caps(&dev, out);
                }
                if (o->raw_feature_dump && dev.feature_len > 0) {
                    uint8_t feat_buf[256];
                    size_t flen = dev.feature_len > sizeof(feat_buf) ? sizeof(feat_buf) : dev.feature_len;
                    /* Try report ID 0x00 and any Feature ValueCap report IDs safely. */
                    uint8_t seen_rids[64];
                    size_t rcount = octans_hid_collect_feature_rids(&dev, seen_rids, 64u);

                    for (size_t k = 0; k < rcount; ++k) {
                        if (octans_hid_get_feature(&dev, seen_rids[k], feat_buf, flen) >= 0) {
                            fprintf(out, "    FEAT[0x%02X] (%zuB): ", (unsigned)seen_rids[k], flen);
                            print_hex_line(out, feat_buf, flen);
                            fprintf(out, "\n");
                        }
                    }
                }
                if (want_read && dev.can_read) {
                    uint8_t in_buf[128];
                    int rn = octans_hid_read_input(&dev, in_buf, sizeof(in_buf), 120);
                    if (rn > 0) {
                        fprintf(out, "    PASSIVE_IN (%dB): ", rn);
                        print_hex_line(out, in_buf, (size_t)rn);
                        fprintf(out, "\n");
                    }
                }
                octans_hid_close(&dev);
            }
        }
    }

    if (o->include_bluetooth) {
        octans_bt_battery_t bts[OCTANS_MAX_BT_DEVICES];
        size_t bt_n = octans_bt_enumerate(bts, OCTANS_MAX_BT_DEVICES);
        fprintf(out, "\nOctans Bluetooth Probe: %zu device(s) with DEVPKEY_Bluetooth_Battery\n", bt_n);
        for (size_t i = 0; i < bt_n; ++i) {
            char name_u8[192], id_u8[256];
            octans_wide_to_utf8(bts[i].name, name_u8, sizeof(name_u8));
            octans_wide_to_utf8(bts[i].instance_id, id_u8, sizeof(id_u8));
            fprintf(out, "  [BT %zu] name=\"%s\" batt=%d%% hf=%d id=%s\n",
                    i, name_u8, (int)bts[i].battery_percent, bts[i].is_handsfree, id_u8);
        }
    }

    return OCTANS_OK;
}

int octans_probe_dump(int raw_feature_dump, FILE *out) {
    octans_probe_opts_t opts = {1, raw_feature_dump, 1};
    return octans_probe_run(&opts, out);
}
