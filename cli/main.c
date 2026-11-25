#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "octans_core.h"
#include "octans_version.h"
#include "../core/hid_enum.h"
#include "../engine/broad_scan.h"
#include "../engine/family_request.h"
#include "../engine/hidpp.h"
#include "../engine/prototable.h"
#include "../engine/user_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile LONG g_stop_watch = 0;

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_stop_watch, 1);
        return TRUE;
    }
    return FALSE;
}

static const char *kind_label(octans_device_kind_t k) {
    switch (k) {
    case OCTANS_DEV_MOUSE: return "Mouse";
    case OCTANS_DEV_KEYBOARD: return "Keyboard";
    case OCTANS_DEV_HEADSET: return "Headset";
    case OCTANS_DEV_EARBUDS: return "Earbuds";
    case OCTANS_DEV_GAMEPAD: return "Gamepad";
    case OCTANS_DEV_STYLUS: return "Stylus";
    case OCTANS_DEV_SPEAKER: return "Speaker";
    case OCTANS_DEV_OTHER: return "Other";
    default: return "Unknown";
    }
}

static const char *conn_label(octans_conn_kind_t c) {
    switch (c) {
    case OCTANS_CONN_DONGLE_24G: return "2.4G";
    case OCTANS_CONN_BLUETOOTH: return "BT";
    case OCTANS_CONN_WIRED: return "Wired";
    default: return "-";
    }
}

static const char *power_label(octans_power_state_t p) {
    switch (p) {
    case OCTANS_PWR_DISCHARGING: return "Discharging";
    case OCTANS_PWR_CHARGING: return "Charging";
    case OCTANS_PWR_FULL: return "Full";
    case OCTANS_PWR_SLEEPING: return "Sleeping";
    case OCTANS_PWR_OFFLINE: return "Offline";
    default: return "Unknown";
    }
}

static void print_usage(void) {
    printf(
        "Octans Backend CLI v%s (UI ABI v%d)\n"
        "Usage:\n"
        "  octans-cli.exe probe [--dump] [--raw]          Enumerate HID/BT interfaces safely\n"
        "  octans-cli.exe list [--json] [--debug]         Query all peripherals once\n"
        "  octans-cli.exe watch [--interval MS] [--json]  Poll continuously on hotplug/timer\n"
        "  octans-cli.exe discover                        Broad-scan unknown vendor HIDs\n"
        "  octans-cli.exe test-rule <ini_path>            Test external devices.ini rule file\n"
        "  octans-cli.exe --once [--json] [--debug]       Alias for `list`\n"
        "  octans-cli.exe --version                       Print version\n",
        OCTANS_VERSION_STR,
        OCTANS_UI_API_VERSION);
}

static void print_devices_table(const octans_device_view_t *views, size_t count) {
    if (count == 0) {
        printf("No supported wireless peripheral battery readings found.\n");
        return;
    }
    printf("Found %zu peripheral(s):\n", count);
    for (size_t i = 0; i < count; ++i) {
        const octans_device_view_t *v = &views[i];
        char name_u8[192], brand_u8[96];
        octans_wide_to_utf8(v->name, name_u8, sizeof(name_u8));
        octans_wide_to_utf8(v->brand, brand_u8, sizeof(brand_u8));

        char batt_str[16];
        if (v->battery_percent >= 0) {
            snprintf(batt_str, sizeof(batt_str), "%s%d%%",
                     v->is_estimated ? "~" : "", (int)v->battery_percent);
        } else {
            snprintf(batt_str, sizeof(batt_str), "--");
        }

        printf("  %-24s [%s | %s] %5s  %-11s  VID:PID=%04X:%04X",
               name_u8,
               kind_label(v->kind),
               conn_label(v->conn),
               batt_str,
               power_label(v->power_state),
               (unsigned)v->vid,
               (unsigned)v->pid);
        if (v->voltage_mv > 0) {
            printf("  %umV", (unsigned)v->voltage_mv);
        }
        if (v->is_low_battery) {
            printf("  [LOW]");
        }
        if (brand_u8[0]) {
            printf("  (%s)", brand_u8);
        }
        printf("\n");
    }
}

static int run_list(octans_engine_t *eng, int json_mode) {
    int rc = octans_engine_poll(eng, 1);
    if (rc != OCTANS_OK) return 1;

    if (json_mode) {
        char json_buf[16384];
        int n = octans_engine_format_json(eng, json_buf, sizeof(json_buf));
        if (n < 0) {
            fprintf(stderr, "JSON formatting failed (%d)\n", n);
            return 1;
        }
        printf("%s\n", json_buf);
    } else {
        octans_device_view_t views[OCTANS_MAX_DEVICES];
        size_t n = octans_engine_get_devices(eng, views, OCTANS_MAX_DEVICES);
        print_devices_table(views, n);
    }
    return 0;
}

static int run_discover(int debug_mode) {
    octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
    size_t n = octans_hid_enumerate(ifaces, OCTANS_MAX_HID_IFACES);
    int group_hit[OCTANS_MAX_HID_IFACES];
    memset(group_hit, 0, sizeof(group_hit));

    for (size_t i = 0; i < n; ++i) {
        if (ifaces[i].vid == OCTANS_LOGITECH_VID) {
            group_hit[i] = 1;
            continue;
        }
        for (size_t p = 0; p < g_protocols_count; ++p) {
            if (octans_proto_matches_iface(&g_protocols[p], &ifaces[i])) {
                group_hit[i] = 1;
                break;
            }
        }
    }

    octans_broad_cache_t cache;
    octans_broad_cache_clear(&cache);
    octans_proto_reading_t hits[16];
    size_t hit_n = octans_broad_scan_unknowns(ifaces, n, group_hit, &cache, NULL, debug_mode, hits, 16);

    if (hit_n > 0) {
        printf("Broad scan matched %zu unknown device(s):\n", hit_n);
        for (size_t i = 0; i < hit_n; ++i) {
            char nu8[192];
            octans_wide_to_utf8(hits[i].name, nu8, sizeof(nu8));
            printf("  %s  VID:PID=%04X:%04X  %d%%  [%s]\n",
                   nu8, (unsigned)hits[i].vid, (unsigned)hits[i].pid,
                   (int)hits[i].battery_percent, hits[i].source_tag);
        }
    } else {
        printf("Broad scan found 0 new matches.\n");
    }

    printf("\nRemaining unrecognized vendor HID interfaces (UP >= 0xFF00):\n");
    size_t rem = 0;
    for (size_t i = 0; i < n; ++i) {
        const octans_hid_info_t *it = &ifaces[i];
        if (group_hit[i] || it->vid == 0u || it->usage_page < 0xFF00u) continue;
        char pu8[192];
        octans_wide_to_utf8(it->product, pu8, sizeof(pu8));
        printf("  [%zu] VID=%04X PID=%04X MI=%02d UP=%04X:%04X IN=%u OUT=%u FEAT=%u PROD=\"%s\"\n",
               i, (unsigned)it->vid, (unsigned)it->pid, it->interface_num,
               (unsigned)it->usage_page, (unsigned)it->usage,
               (unsigned)it->input_len, (unsigned)it->output_len, (unsigned)it->feature_len, pu8);
        ++rem;
    }
    if (rem == 0) printf("  (none)\n");
    return 0;
}

static int run_test_rule(const char *path_u8) {
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path_u8, -1, wpath, MAX_PATH);
    wpath[MAX_PATH - 1] = L'\0';

    octans_user_rule_t rules[OCTANS_MAX_USER_RULES];
    size_t rcount = octans_user_rules_load_file(wpath, rules, OCTANS_MAX_USER_RULES);
    if (rcount == 0) {
        fprintf(stderr, "No valid rules loaded from %s\n", path_u8);
        return 1;
    }

    octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
    size_t n = octans_hid_enumerate(ifaces, OCTANS_MAX_HID_IFACES);
    int any_ok = 0;

    for (size_t u = 0; u < rcount; ++u) {
        int matched = 0;
        int ok_this = 0;
        int saw_raw = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!octans_user_rule_matches_iface(&rules[u], &ifaces[i])) continue;
            matched = 1;
            octans_proto_reading_t r;
            uint8_t raw[128];
            size_t raw_len = 0;
            if (octans_user_rule_query_iface(&rules[u], &ifaces[i], 300u, &r, raw, sizeof(raw), &raw_len) && r.valid) {
                char nu8[192];
                octans_wide_to_utf8(r.name, nu8, sizeof(nu8));
                printf("[OK] %s -> %s (%04X:%04X): %d%% (%s)\n",
                       rules[u].section, nu8, (unsigned)r.vid, (unsigned)r.pid,
                       (int)r.battery_percent, power_label(r.power_state));
                ok_this = 1;
                any_ok = 1;
                break;
            } else if (raw_len > 0) {
                saw_raw = 1;
                printf("[RAW] %s (%04X:%04X) got %zuB:",
                       rules[u].section, (unsigned)ifaces[i].vid, (unsigned)ifaces[i].pid, raw_len);
                for (size_t b = 0; b < raw_len && b < 24; ++b) printf(" %02X", raw[b]);
                printf("\n");
            }
        }
        if (!matched) {
            printf("[NO_IFACE] %s matched 0 connected interfaces\n", rules[u].section);
        } else if (!ok_this && !saw_raw) {
            printf("[NO_REPLY] %s matched interface(s) but got no reply\n", rules[u].section);
        }
    }
    return any_ok ? 0 : 1;
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);

    const char *cmd = NULL;
    const char *rule_arg = NULL;
    int dump_caps = 0;
    int raw_dump = 0;
    int json_mode = 0;
    int debug_mode = 0;
    uint32_t interval_ms = 15000u;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("Octans v%s\n", OCTANS_VERSION_STR);
            return 0;
        } else if (strcmp(arg, "--once") == 0) {
            cmd = "list";
        } else if (strcmp(arg, "--dump") == 0) {
            dump_caps = 1;
        } else if (strcmp(arg, "--raw") == 0) {
            raw_dump = 1;
        } else if (strcmp(arg, "--json") == 0) {
            json_mode = 1;
        } else if (strcmp(arg, "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(arg, "--interval") == 0 && i + 1 < argc) {
            interval_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
            if (interval_ms < 1000u) interval_ms = 1000u;
        } else if (arg[0] != '-' && cmd == NULL) {
            cmd = arg;
        } else if (arg[0] != '-' && cmd != NULL && strcmp(cmd, "test-rule") == 0 && rule_arg == NULL) {
            rule_arg = arg;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg);
            print_usage();
            return 2;
        }
    }

    if (!cmd) cmd = "list";

    if (strcmp(cmd, "probe") == 0) {
        octans_probe_opts_t opts = {dump_caps || !raw_dump, raw_dump, 1};
        return (octans_probe_run(&opts, stdout) == OCTANS_OK) ? 0 : 1;
    }
    if (strcmp(cmd, "discover") == 0) {
        return run_discover(debug_mode);
    }
    if (strcmp(cmd, "test-rule") == 0) {
        if (!rule_arg) {
            fprintf(stderr, "Usage: octans-cli.exe test-rule <ini_path>\n");
            return 2;
        }
        return run_test_rule(rule_arg);
    }

    octans_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.poll_interval_ms = interval_ms;
    cfg.passive_listen_ms = 250u;
    cfg.debug_log = debug_mode;

    octans_engine_t *eng = octans_engine_create(&cfg);
    if (!eng) {
        fprintf(stderr, "Failed to initialize Octans engine\n");
        return 1;
    }

    int exit_code = 0;
    if (strcmp(cmd, "list") == 0) {
        exit_code = run_list(eng, json_mode);
    } else if (strcmp(cmd, "watch") == 0) {
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
        char prev_json[16384] = {0};
        while (!InterlockedCompareExchange(&g_stop_watch, 0, 0) &&
               !octans_engine_quit_requested(eng)) {
            octans_engine_poll(eng, 0);
            char cur_json[16384];
            if (octans_engine_format_json(eng, cur_json, sizeof(cur_json)) >= 0) {
                if (strcmp(cur_json, prev_json) != 0) {
                    if (json_mode) {
                        printf("%s\n", cur_json);
                    } else {
                        octans_device_view_t views[OCTANS_MAX_DEVICES];
                        size_t n = octans_engine_get_devices(eng, views, OCTANS_MAX_DEVICES);
                        print_devices_table(views, n);
                    }
                    fflush(stdout);
                    strncpy(prev_json, cur_json, sizeof(prev_json) - 1);
                }
            }
            for (uint32_t elapsed = 0;
                 elapsed < interval_ms && !InterlockedCompareExchange(&g_stop_watch, 0, 0);
                 elapsed += 200u) {
                Sleep(200u);
            }
        }
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage();
        exit_code = 2;
    }

    octans_engine_destroy(eng);
    return exit_code;
}
