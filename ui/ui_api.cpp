#include "ui_internal.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "ui_tray.h"
#include "ui_panel.h"
#include "ui_menu.h"
#include "ui_rename_dlg.h"
#include "ui_about_dlg.h"
#include "ui_updater.h"
#include "octans_version.h"
#include <stdio.h>
#include <stdlib.h>

namespace octans_ui {

static UiState g_state;
static UINT g_msg_taskbar_created = 0;
static constexpr UINT_PTR TIMER_SCAN_SETTLE      = 2;
static constexpr UINT_PTR TIMER_SCAN_DONE_FADE   = 3;
static constexpr UINT_PTR TIMER_STARTUP_TRIM     = 4;
static constexpr UINT_PTR TIMER_AUTO_UPDATE_BOOT = 5;
static constexpr UINT_PTR TIMER_AUTO_UPDATE_POLL = 6;
static constexpr uint64_t kAutoUpdateIntervalSec = 86400ULL;
static octans_device_view_t g_deferred_devices[MAX_DEVICES] = {};
static size_t g_deferred_count = 0;
static bool g_has_deferred = false;

static UpdateInfo g_cached_update = {};
static UpdateInfo g_bg_update_result = {};
static bool g_bg_update_ok = false;
static SRWLOCK g_bg_update_lock = SRWLOCK_INIT;
static HANDLE g_bg_update_thread = nullptr;
static volatile LONG g_bg_cancel = 0;

static void GetUiIniPath(wchar_t out_dir[MAX_PATH], wchar_t out_ini[MAX_PATH]) {
    wchar_t appdata[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 32) {
        _snwprintf(out_dir, MAX_PATH - 1, L"%ls\\Octans", appdata);
        out_dir[MAX_PATH - 1] = L'\0';
        _snwprintf(out_ini, MAX_PATH - 1, L"%ls\\Octans\\config.ini", appdata);
        out_ini[MAX_PATH - 1] = L'\0';
    } else {
        out_dir[0] = L'\0';
        wcscpy(out_ini, L"octans.ini");
    }
}

static void LoadUiConfig() {
    wchar_t dir[MAX_PATH] = {};
    wchar_t ini[MAX_PATH] = {};
    GetUiIniPath(dir, ini);

    int style_v = static_cast<int>(GetPrivateProfileIntW(L"UI", L"TrayStyle", 0, ini));
    if (style_v == 1) g_state.tray_style = TRAY_STYLE_NEBULA;
    else if (style_v == 2) g_state.tray_style = TRAY_STYLE_PULSAR;
    else g_state.tray_style = TRAY_STYLE_OCTANS;

    int scale_pct = static_cast<int>(GetPrivateProfileIntW(L"UI", L"TrayScalePct", 50, ini));
    g_state.tray_scale = ClampF(static_cast<float>(scale_pct) / 100.0f, 0.0f, kMaxTrayScale);

    g_state.has_custom_panel_pos = (GetPrivateProfileIntW(L"UI", L"PanelPosCustom", 0, ini) != 0);
    wchar_t pos_w[32] = {};
    GetPrivateProfileStringW(L"UI", L"PanelPosX", L"0", pos_w, 32, ini);
    g_state.custom_panel_x = _wtoi(pos_w);
    GetPrivateProfileStringW(L"UI", L"PanelPosY", L"0", pos_w, 32, ini);
    g_state.custom_panel_y = _wtoi(pos_w);

    g_state.auto_check_updates = (GetPrivateProfileIntW(L"UI", L"AutoCheckUpdates", 1, ini) != 0);
    wchar_t chk_w[32] = {};
    GetPrivateProfileStringW(L"UI", L"LastUpdateCheckSec", L"0", chk_w, 32, ini);
    g_state.last_update_check_sec = static_cast<uint64_t>(_wcstoui64(chk_w, nullptr, 10));

    wchar_t pins_w[1024] = {};
    GetPrivateProfileStringW(L"UI", L"PinnedIds", L"", pins_w, 1024, ini);
    g_state.pinned_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) g_state.pinned_ids[i][0] = '\0';

    if (pins_w[0] != L'\0') {
        char pins_u8[1024] = {};
        WideCharToMultiByte(CP_UTF8, 0, pins_w, -1, pins_u8, sizeof(pins_u8), nullptr, nullptr);
        const char *p = pins_u8;
        while (*p && g_state.pinned_count < MAX_DEVICES) {
            const char *comma = strchr(p, ',');
            size_t len = comma ? static_cast<size_t>(comma - p) : strlen(p);
            if (len > 0) {
                if (len >= sizeof(g_state.pinned_ids[0])) len = sizeof(g_state.pinned_ids[0]) - 1;
                memcpy(g_state.pinned_ids[g_state.pinned_count], p, len);
                g_state.pinned_ids[g_state.pinned_count][len] = '\0';
                g_state.pinned_count++;
            }
            if (!comma) break;
            p = comma + 1;
        }
    }

    wchar_t hid_w[1024] = {};
    GetPrivateProfileStringW(L"UI", L"HiddenIds", L"", hid_w, 1024, ini);
    g_state.hidden_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) g_state.hidden_ids[i][0] = '\0';

    if (hid_w[0] != L'\0') {
        char hid_u8[1024] = {};
        WideCharToMultiByte(CP_UTF8, 0, hid_w, -1, hid_u8, sizeof(hid_u8), nullptr, nullptr);
        const char *p = hid_u8;
        while (*p && g_state.hidden_count < MAX_DEVICES) {
            const char *comma = strchr(p, ',');
            size_t len = comma ? static_cast<size_t>(comma - p) : strlen(p);
            if (len > 0) {
                if (len >= sizeof(g_state.hidden_ids[0])) len = sizeof(g_state.hidden_ids[0]) - 1;
                memcpy(g_state.hidden_ids[g_state.hidden_count], p, len);
                g_state.hidden_ids[g_state.hidden_count][len] = '\0';
                g_state.hidden_count++;
            }
            if (!comma) break;
            p = comma + 1;
        }
    }

    g_state.kind_override_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        g_state.kind_overrides[i].id[0] = '\0';
        g_state.kind_overrides[i].kind = OCTANS_DEV_UNKNOWN;
    }
    wchar_t icon_sec[2048] = {};
    if (GetPrivateProfileSectionW(L"DeviceIcons", icon_sec, 2048, ini) > 0) {
        const wchar_t *p = icon_sec;
        while (*p != L'\0' && g_state.kind_override_count < MAX_DEVICES) {
            const wchar_t *eq = wcschr(p, L'=');
            if (eq && eq > p && eq[1] != L'\0') {
                int kv = _wtoi(eq + 1);
                if (kv >= static_cast<int>(OCTANS_DEV_MOUSE) && kv <= static_cast<int>(OCTANS_DEV_OTHER)) {
                    wchar_t wid[64] = {};
                    size_t wlen = static_cast<size_t>(eq - p);
                    if (wlen >= 64) wlen = 63;
                    wmemcpy(wid, p, wlen);
                    wid[wlen] = L'\0';
                    char id_u8[64] = {};
                    WideCharToMultiByte(CP_UTF8, 0, wid, -1, id_u8, sizeof(id_u8), nullptr, nullptr);
                    if (id_u8[0]) {
                        strncpy(g_state.kind_overrides[g_state.kind_override_count].id, id_u8, 63);
                        g_state.kind_overrides[g_state.kind_override_count].id[63] = '\0';
                        g_state.kind_overrides[g_state.kind_override_count].kind = static_cast<octans_device_kind_t>(kv);
                        g_state.kind_override_count++;
                    }
                }
            }
            p += wcslen(p) + 1;
        }
    }
}

static void SaveUiConfig() {
    if (g_state.demo_hooks.on_action != nullptr) return;

    wchar_t dir[MAX_PATH] = {};
    wchar_t ini[MAX_PATH] = {};
    GetUiIniPath(dir, ini);
    if (dir[0]) CreateDirectoryW(dir, nullptr);

    wchar_t buf[32] = {};
    _snwprintf(buf, 31, L"%d", static_cast<int>(g_state.tray_style));
    WritePrivateProfileStringW(L"UI", L"TrayStyle", buf, ini);

    int pct = static_cast<int>(roundf(ClampF(g_state.tray_scale, 0.0f, kMaxTrayScale) * 100.0f));
    _snwprintf(buf, 31, L"%d", pct);
    WritePrivateProfileStringW(L"UI", L"TrayScalePct", buf, ini);

    WritePrivateProfileStringW(L"UI", L"PanelPosCustom", g_state.has_custom_panel_pos ? L"1" : L"0", ini);
    _snwprintf(buf, 31, L"%d", g_state.custom_panel_x);
    WritePrivateProfileStringW(L"UI", L"PanelPosX", buf, ini);
    _snwprintf(buf, 31, L"%d", g_state.custom_panel_y);
    WritePrivateProfileStringW(L"UI", L"PanelPosY", buf, ini);

    WritePrivateProfileStringW(L"UI", L"AutoCheckUpdates", g_state.auto_check_updates ? L"1" : L"0", ini);
    _snwprintf(buf, 31, L"%llu", static_cast<unsigned long long>(g_state.last_update_check_sec));
    WritePrivateProfileStringW(L"UI", L"LastUpdateCheckSec", buf, ini);

    char pins_u8[1024] = {};
    size_t pos = 0;
    for (size_t i = 0; i < g_state.pinned_count && i < MAX_DEVICES; ++i) {
        if (!g_state.pinned_ids[i][0]) continue;
        size_t len = strlen(g_state.pinned_ids[i]);
        if (pos + len + 2 >= sizeof(pins_u8)) break;
        if (pos > 0) pins_u8[pos++] = ',';
        memcpy(pins_u8 + pos, g_state.pinned_ids[i], len);
        pos += len;
    }
    pins_u8[pos] = '\0';

    wchar_t pins_w[1024] = {};
    if (pos > 0) {
        MultiByteToWideChar(CP_UTF8, 0, pins_u8, -1, pins_w, 1024);
    }
    WritePrivateProfileStringW(L"UI", L"PinnedIds", pins_w, ini);

    char hid_u8[1024] = {};
    size_t hpos = 0;
    for (size_t i = 0; i < g_state.hidden_count && i < MAX_DEVICES; ++i) {
        if (!g_state.hidden_ids[i][0]) continue;
        size_t len = strlen(g_state.hidden_ids[i]);
        if (hpos + len + 2 >= sizeof(hid_u8)) break;
        if (hpos > 0) hid_u8[hpos++] = ',';
        memcpy(hid_u8 + hpos, g_state.hidden_ids[i], len);
        hpos += len;
    }
    hid_u8[hpos] = '\0';

    wchar_t hid_w[1024] = {};
    if (hpos > 0) {
        MultiByteToWideChar(CP_UTF8, 0, hid_u8, -1, hid_w, 1024);
    }
    WritePrivateProfileStringW(L"UI", L"HiddenIds", hid_w, ini);
}

UiState &GetState() {
    return g_state;
}

bool IsDevicePinned(const UiState &st, const char *device_id) {
    if (!device_id || !device_id[0]) return false;
    for (size_t i = 0; i < st.pinned_count && i < MAX_DEVICES; ++i) {
        if (strcmp(st.pinned_ids[i], device_id) == 0) {
            return true;
        }
    }
    return false;
}

bool IsDeviceHidden(const UiState &st, const char *device_id) {
    if (!device_id || !device_id[0]) return false;
    for (size_t i = 0; i < st.hidden_count && i < MAX_DEVICES; ++i) {
        if (strcmp(st.hidden_ids[i], device_id) == 0) {
            return true;
        }
    }
    return false;
}

bool HasDeviceKindOverride(const UiState &st, const char *device_id) {
    return GetDeviceKindOverride(st, device_id) != OCTANS_DEV_UNKNOWN;
}

octans_device_kind_t GetDeviceKindOverride(const UiState &st, const char *device_id) {
    if (!device_id || !device_id[0]) return OCTANS_DEV_UNKNOWN;
    for (size_t i = 0; i < st.kind_override_count && i < MAX_DEVICES; ++i) {
        if (strcmp(st.kind_overrides[i].id, device_id) == 0) {
            return st.kind_overrides[i].kind;
        }
    }
    return OCTANS_DEV_UNKNOWN;
}

size_t CountActivePinnedDevices(const UiState &st) {
    size_t n = 0;
    for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
        if (IsDevicePinned(st, st.devices[i].id)) {
            ++n;
        }
    }
    return n;
}

int SelectFallbackTrayDeviceIndex(const octans_device_view_t *devices, size_t count) {
    if (!devices || count == 0) return -1;

    /* 1. Lowest-battery active low-battery device (attention priority) */
    int low_idx = -1;
    int low_pct = 101;
    for (size_t i = 0; i < count; ++i) {
        if (devices[i].power_state == OCTANS_PWR_OFFLINE) continue;
        bool is_low = devices[i].is_low_battery ||
                      (devices[i].battery_percent >= 0 && devices[i].battery_percent <= 20);
        if (is_low && devices[i].battery_percent >= 0 && devices[i].battery_percent < low_pct) {
            low_pct = devices[i].battery_percent;
            low_idx = static_cast<int>(i);
        }
    }
    if (low_idx >= 0) return low_idx;

    /* 2. First active non-offline device */
    for (size_t i = 0; i < count; ++i) {
        if (devices[i].power_state != OCTANS_PWR_OFFLINE) {
            return static_cast<int>(i);
        }
    }

    return 0;
}

static void FilterAndStoreDevices(const octans_device_view_t *devices, size_t count) {
    if (count > MAX_DEVICES) count = MAX_DEVICES;
    size_t out_n = 0;
    if (devices) {
        for (size_t i = 0; i < count && out_n < MAX_DEVICES; ++i) {
            if (IsDeviceHidden(g_state, devices[i].id)) continue;
            g_state.raw_kinds[out_n] = devices[i].kind;
            g_state.devices[out_n] = devices[i];
            octans_device_kind_t ov = GetDeviceKindOverride(g_state, devices[i].id);
            if (ov != OCTANS_DEV_UNKNOWN) {
                g_state.devices[out_n].kind = ov;
            }
            out_n++;
        }
    }
    g_state.device_count = out_n;
}

static void CompleteScanTransition() {
    if (g_state.host_hwnd) {
        KillTimer(g_state.host_hwnd, TIMER_SCAN_SETTLE);
    }
    g_state.is_scanning = false;
    g_state.initial_scan_done = true;

    if (PanelIsVisible() && g_state.demo_hooks.on_action == nullptr && g_state.host_hwnd) {
        g_state.scan_phase = SCAN_COMPLETE;
        g_state.scan_completed_ms = GetTickCount64();
        PanelOnScanStateChanged();
        SetTimer(g_state.host_hwnd, TIMER_SCAN_DONE_FADE, 1150, nullptr);
    } else {
        g_state.scan_phase = SCAN_IDLE;
        PanelOnScanStateChanged();
        if (g_state.host_hwnd) {
            SetTimer(g_state.host_hwnd, TIMER_STARTUP_TRIM, 1500u, nullptr);
        }
    }
}

static void CommitDeviceSnapshotNow(const octans_device_view_t *devices, size_t count) {
    bool was_scanning = (g_state.scan_phase == SCAN_ACTIVE || g_state.is_scanning);
    bool was_refreshing = g_state.refresh_pending;
    g_state.refresh_pending = false;

    FilterAndStoreDevices(devices, count);
    PanelOnDevicesChanged(g_state.devices, g_state.device_count);
    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);

    if (was_scanning) {
        CompleteScanTransition();
    } else if (was_refreshing) {
        PanelOnRefreshCompleted();
    }
}

static void ApplyDeviceSnapshotOnUiThread(const octans_device_view_t *devices, size_t count) {
    if (count > MAX_DEVICES) count = MAX_DEVICES;

    /* During active scan while panel is visible: immediately reflect discovered devices behind
     * the frosted scan veil so the user sees them appearing live, while keeping the scan overlay
     * active for at least 1150ms before smoothly playing the "扫描完成" confirmation bloom. */
    if (g_state.scan_phase == SCAN_ACTIVE && PanelIsVisible() && g_state.host_hwnd &&
        g_state.demo_hooks.on_action == nullptr) {
        FilterAndStoreDevices(devices, count);
        PanelOnDevicesChanged(g_state.devices, g_state.device_count);
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);

        uint64_t now = GetTickCount64();
        uint64_t elapsed = (now >= g_state.scan_started_ms) ? (now - g_state.scan_started_ms) : 1150ULL;
        if (elapsed < 1150ULL) {
            if (devices && count > 0) {
                memcpy(g_deferred_devices, devices, count * sizeof(octans_device_view_t));
            }
            g_deferred_count = count;
            g_has_deferred = true;
            SetTimer(g_state.host_hwnd, TIMER_SCAN_SETTLE, static_cast<UINT>(1150ULL - elapsed), nullptr);
            return;
        }
        CompleteScanTransition();
        return;
    }

    if (g_state.host_hwnd) {
        KillTimer(g_state.host_hwnd, TIMER_SCAN_SETTLE);
    }
    g_has_deferred = false;
    CommitDeviceSnapshotNow(devices, count);
}

void ActionTriggerRefresh() {
    g_state.refresh_pending = true;
    PanelTriggerRefreshSpin();
    if (g_state.ops.request_refresh) {
        g_state.ops.request_refresh(g_state.backend_ctx);
    }
}

void ActionTriggerFullScan() {
    /* Full scan clears hidden device exclusions so previously deleted devices can be rediscovered. */
    if (g_state.hidden_count > 0) {
        g_state.hidden_count = 0;
        for (size_t i = 0; i < MAX_DEVICES; ++i) g_state.hidden_ids[i][0] = '\0';
        SaveUiConfig();
    }
    if (g_state.host_hwnd) {
        KillTimer(g_state.host_hwnd, TIMER_SCAN_SETTLE);
        KillTimer(g_state.host_hwnd, TIMER_SCAN_DONE_FADE);
    }
    g_has_deferred = false;
    g_state.is_scanning = true;
    g_state.scan_phase = SCAN_ACTIVE;
    g_state.scan_started_ms = GetTickCount64();
    g_state.refresh_pending = false;

    if (!PanelIsVisible() && g_state.host_hwnd) {
        PostMessageW(g_state.host_hwnd, WM_APP_SHOW_PANEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
    }
    PanelTriggerRefreshSpin();
    PanelOnScanStateChanged();
    if (g_state.ops.request_refresh) {
        g_state.ops.request_refresh(g_state.backend_ctx);
    }
}

void ActionRemoveDevice(const char *device_id) {
    if (!device_id || !device_id[0]) return;

    /* Record in hidden list so routine background polls do not resurrect it until full scan */
    if (!IsDeviceHidden(g_state, device_id) && g_state.hidden_count < MAX_DEVICES) {
        strncpy(g_state.hidden_ids[g_state.hidden_count], device_id, 63);
        g_state.hidden_ids[g_state.hidden_count][63] = '\0';
        g_state.hidden_count++;
    }

    /* Unpin if currently pinned */
    for (size_t i = 0; i < g_state.pinned_count && i < MAX_DEVICES; ++i) {
        if (strcmp(g_state.pinned_ids[i], device_id) == 0) {
            for (size_t j = i; j + 1 < g_state.pinned_count; ++j) {
                memcpy(g_state.pinned_ids[j], g_state.pinned_ids[j + 1], sizeof(g_state.pinned_ids[j]));
            }
            g_state.pinned_count--;
            g_state.pinned_ids[g_state.pinned_count][0] = '\0';
            if (g_state.ops.set_pinned_device) {
                g_state.ops.set_pinned_device(
                    g_state.backend_ctx,
                    (g_state.pinned_count > 0) ? g_state.pinned_ids[0] : ""
                );
            }
            break;
        }
    }

    /* Remove immediately from live devices list */
    for (size_t i = 0; i < g_state.device_count; ++i) {
        if (strcmp(g_state.devices[i].id, device_id) == 0) {
            for (size_t j = i; j + 1 < g_state.device_count; ++j) {
                g_state.devices[j] = g_state.devices[j + 1];
                g_state.raw_kinds[j] = g_state.raw_kinds[j + 1];
            }
            g_state.device_count--;
            break;
        }
    }

    SaveUiConfig();
    PanelOnDevicesChanged(g_state.devices, g_state.device_count);
    PanelOnPinnedChanged();
    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
}

void ActionSetDeviceKind(const char *device_id, octans_device_kind_t kind) {
    if (!device_id || !device_id[0]) return;

    int found = -1;
    for (size_t i = 0; i < g_state.kind_override_count && i < MAX_DEVICES; ++i) {
        if (strcmp(g_state.kind_overrides[i].id, device_id) == 0) {
            found = static_cast<int>(i);
            break;
        }
    }

    if (kind == OCTANS_DEV_UNKNOWN) {
        if (found >= 0) {
            for (size_t j = static_cast<size_t>(found); j + 1 < g_state.kind_override_count; ++j) {
                g_state.kind_overrides[j] = g_state.kind_overrides[j + 1];
            }
            g_state.kind_override_count--;
            g_state.kind_overrides[g_state.kind_override_count].id[0] = '\0';
            g_state.kind_overrides[g_state.kind_override_count].kind = OCTANS_DEV_UNKNOWN;
        }
    } else {
        if (found >= 0) {
            g_state.kind_overrides[found].kind = kind;
        } else {
            size_t slot = (g_state.kind_override_count < MAX_DEVICES) ? g_state.kind_override_count++ : 0;
            strncpy(g_state.kind_overrides[slot].id, device_id, 63);
            g_state.kind_overrides[slot].id[63] = '\0';
            g_state.kind_overrides[slot].kind = kind;
        }
    }

    if (g_state.demo_hooks.on_action == nullptr) {
        wchar_t dir[MAX_PATH] = {};
        wchar_t ini[MAX_PATH] = {};
        GetUiIniPath(dir, ini);
        if (dir[0]) CreateDirectoryW(dir, nullptr);
        wchar_t wid[64] = {};
        MultiByteToWideChar(CP_UTF8, 0, device_id, -1, wid, 64);
        if (wid[0]) {
            if (kind == OCTANS_DEV_UNKNOWN) {
                WritePrivateProfileStringW(L"DeviceIcons", wid, nullptr, ini);
            } else {
                wchar_t vbuf[16] = {};
                _snwprintf(vbuf, 15, L"%d", static_cast<int>(kind));
                WritePrivateProfileStringW(L"DeviceIcons", wid, vbuf, ini);
            }
        }
    }

    for (size_t i = 0; i < g_state.device_count && i < MAX_DEVICES; ++i) {
        if (strcmp(g_state.devices[i].id, device_id) == 0) {
            g_state.devices[i].kind = (kind == OCTANS_DEV_UNKNOWN) ? g_state.raw_kinds[i] : kind;
            break;
        }
    }

    PanelOnDevicesChanged(g_state.devices, g_state.device_count);
    PanelOnPinnedChanged();
    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
}

void ActionTogglePinDevice(const char *device_id) {
    if (!device_id || !device_id[0]) return;

    int found = -1;
    for (size_t i = 0; i < g_state.pinned_count && i < MAX_DEVICES; ++i) {
        if (strcmp(g_state.pinned_ids[i], device_id) == 0) {
            found = static_cast<int>(i);
            break;
        }
    }

    if (found >= 0) {
        /* Unpin: allow unpinning down to 0 so the last taskbar slot cleanly returns to Octans Logo! */
        for (size_t i = static_cast<size_t>(found); i + 1 < g_state.pinned_count; ++i) {
            memcpy(g_state.pinned_ids[i], g_state.pinned_ids[i + 1], sizeof(g_state.pinned_ids[i]));
        }
        g_state.pinned_count--;
        g_state.pinned_ids[g_state.pinned_count][0] = '\0';
    } else if (g_state.pinned_count < MAX_DEVICES) {
        /* Pin: add dedicated tray icon for this device */
        strncpy(g_state.pinned_ids[g_state.pinned_count], device_id, 63);
        g_state.pinned_ids[g_state.pinned_count][63] = '\0';
        g_state.pinned_count++;
    }

    /* Notify backend via stable C ABI (first pinned ID or empty string) */
    if (g_state.ops.set_pinned_device) {
        g_state.ops.set_pinned_device(
            g_state.backend_ctx,
            (g_state.pinned_count > 0) ? g_state.pinned_ids[0] : ""
        );
    }

    SaveUiConfig();
    PanelOnPinnedChanged();
    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
}

void ActionPromptRenameDevice(int device_index) {
    if (device_index < 0 || static_cast<size_t>(device_index) >= g_state.device_count) return;
    const octans_device_view_t &dev = g_state.devices[device_index];

    char id_copy[64] = {};
    strncpy(id_copy, dev.id, sizeof(id_copy) - 1);
    wchar_t cur_name[64] = {};
    wcsncpy(cur_name, dev.name, 63);

    wchar_t new_alias[64] = {};
    g_state.modal_open = true;
    bool ok = ShowRenameAliasDialog(PanelGetHwnd(), g_state.is_dark, cur_name, new_alias, 64);
    g_state.modal_open = false;

    if (ok && new_alias[0] != L'\0') {
        if (g_state.ops.set_device_alias) {
            g_state.ops.set_device_alias(g_state.backend_ctx, id_copy, new_alias);
        }
        /* Reflect locally right away in case backend updates asynchronously */
        for (size_t i = 0; i < g_state.device_count; ++i) {
            if (strcmp(g_state.devices[i].id, id_copy) == 0) {
                wcsncpy(g_state.devices[i].name, new_alias, 63);
                g_state.devices[i].name[63] = L'\0';
                break;
            }
        }
        PanelOnDevicesChanged(g_state.devices, g_state.device_count);
        PanelOnPinnedChanged(); /* Synchronous immediate repaint */
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
    if (PanelIsVisible()) {
        SetForegroundWindow(PanelGetHwnd());
    }
}

void ActionToggleTrayStyle(TrayStyle style) {
    if (g_state.tray_style != style) {
        g_state.tray_style = style;
        SaveUiConfig();
        PanelOnStyleChanged();
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
}

void ActionSetTrayScale(float scale) {
    float clamped = ClampF(scale, 0.0f, kMaxTrayScale);
    if (fabsf(g_state.tray_scale - clamped) >= 0.005f) {
        g_state.tray_scale = clamped;
        SaveUiConfig();
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
}

void ActionSavePanelPosition(int x, int y) {
    g_state.has_custom_panel_pos = true;
    g_state.custom_panel_x = x;
    g_state.custom_panel_y = y;
    SaveUiConfig();
}

static DWORD WINAPI BgUpdateWorkerProc(LPVOID param) {
    (void)param;
    UpdateInfo info = {};
    bool ok = CheckForUpdatesSync(&info);
    if (InterlockedCompareExchange(&g_bg_cancel, 0, 0) == 0 && g_state.host_hwnd) {
        AcquireSRWLockExclusive(&g_bg_update_lock);
        g_bg_update_result = info;
        g_bg_update_ok = ok;
        ReleaseSRWLockExclusive(&g_bg_update_lock);
        PostMessageW(g_state.host_hwnd, WM_APP_BG_UPDATE_DONE, 0, 0);
    }
    return 0;
}

static void MaybeStartBackgroundUpdateCheck() {
    if (!g_state.auto_check_updates || g_state.demo_hooks.on_action != nullptr ||
        g_state.update_check_running || !g_state.host_hwnd) {
        return;
    }
    uint64_t now_sec = GetCurrentUnixTimeSec();
    if (g_state.last_update_check_sec > 0 && now_sec >= g_state.last_update_check_sec &&
        (now_sec - g_state.last_update_check_sec) < kAutoUpdateIntervalSec) {
        return;
    }
    if (g_bg_update_thread) {
        CloseHandle(g_bg_update_thread);
        g_bg_update_thread = nullptr;
    }
    g_state.update_check_running = true;
    g_bg_update_thread = CreateThread(nullptr, 65536u, BgUpdateWorkerProc, nullptr, 0, nullptr);
    if (!g_bg_update_thread) {
        g_state.update_check_running = false;
    }
}

void ActionToggleAutostart() {
    if (!g_state.ops.set_autostart) return;
    int cur = g_state.ops.get_autostart ? g_state.ops.get_autostart(g_state.backend_ctx) : 0;
    g_state.ops.set_autostart(g_state.backend_ctx, cur ? 0 : 1);
}

void ActionToggleAutoCheckUpdates() {
    g_state.auto_check_updates = !g_state.auto_check_updates;
    SaveUiConfig();
    if (g_state.auto_check_updates && !g_state.update_available) {
        MaybeStartBackgroundUpdateCheck();
    }
}

void ActionShowAboutDialog() {
    HWND owner = PanelIsVisible() ? PanelGetHwnd() : g_state.host_hwnd;
    g_state.modal_open = true;
    const UpdateInfo *prefetched = (g_state.update_available && g_cached_update.has_update) ? &g_cached_update : nullptr;
    bool should_quit = ShowAboutDialog(owner, g_state.is_dark, prefetched);
    g_state.modal_open = false;

    if (should_quit) {
        ActionRequestQuit();
        return;
    }
    if (PanelIsVisible()) {
        SetForegroundWindow(PanelGetHwnd());
    }
}

void ActionRunDemoAction(int action_id) {
    if (g_state.demo_hooks.on_action) {
        g_state.demo_hooks.on_action(action_id, g_state.demo_hooks.user_ctx);
    }
}

void ActionRequestQuit() {
    PanelHide();
    if (g_state.ops.request_quit) {
        g_state.ops.request_quit(g_state.backend_ctx);
    } else {
        PostQuitMessage(0);
    }
}

void ActionSetDemoHooks(const DemoMenuHooks *hooks) {
    if (hooks) {
        g_state.demo_hooks = *hooks;
        g_state.is_scanning = false;
        g_state.scan_phase = SCAN_IDLE;
        g_state.initial_scan_done = true;
        g_state.tray_style = TRAY_STYLE_OCTANS;
        g_state.tray_scale = kDefaultTrayScale;
        g_state.has_custom_panel_pos = false;
        g_state.pinned_count = 0;
        g_state.hidden_count = 0;
        g_state.kind_override_count = 0;
        for (size_t i = 0; i < MAX_DEVICES; ++i) {
            g_state.pinned_ids[i][0] = '\0';
            g_state.hidden_ids[i][0] = '\0';
            g_state.kind_overrides[i].id[0] = '\0';
            g_state.kind_overrides[i].kind = OCTANS_DEV_UNKNOWN;
        }
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    } else {
        g_state.demo_hooks = {};
    }
}

void ActionShowContextMenu(int screen_x, int screen_y, int clicked_device_index, MenuMode mode) {
    g_state.modal_open = true;
    HWND owner = PanelIsVisible() ? PanelGetHwnd() : g_state.host_hwnd;
    ShowApplePopoverMenu(owner, screen_x, screen_y, clicked_device_index, mode);
    g_state.modal_open = false;
}

static void ResolveDefaultAnchor(int *ax, int *ay) {
    if (*ax >= 0 && *ay >= 0) return;
    POINT pt = TrayGetAnchorPoint(0);
    /* If icon is hidden in overflow, TrayGetAnchorPoint falls back to cursor; prefer taskbar tray corner on programmatic show. */
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT tb_rc = {};
    if (taskbar && GetWindowRect(taskbar, &tb_rc)) {
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfoW(mon, &mi);
        bool on_taskbar = (pt.y >= tb_rc.top - 8 && pt.y <= tb_rc.bottom + 8 &&
                           pt.x >= tb_rc.left && pt.x <= tb_rc.right);
        if (!on_taskbar) {
            *ax = mi.rcWork.right - 140;
            *ay = (tb_rc.top < (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2)
                ? mi.rcWork.top + 8
                : mi.rcWork.bottom - 4;
            return;
        }
    }
    *ax = pt.x;
    *ay = pt.y;
}

static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_msg_taskbar_created != 0 && msg == g_msg_taskbar_created) {
        TrayHandleTaskbarCreated();
        return 0;
    }

    switch (msg) {
        case WM_TIMER:
            if (wp == TIMER_SCAN_SETTLE) {
                KillTimer(hwnd, TIMER_SCAN_SETTLE);
                if (g_has_deferred) {
                    g_has_deferred = false;
                    FilterAndStoreDevices(g_deferred_devices, g_deferred_count);
                    PanelOnDevicesChanged(g_state.devices, g_state.device_count);
                    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
                }
                CompleteScanTransition();
                return 0;
            }
            if (wp == TIMER_SCAN_DONE_FADE) {
                KillTimer(hwnd, TIMER_SCAN_DONE_FADE);
                if (g_state.scan_phase == SCAN_COMPLETE) {
                    g_state.scan_phase = SCAN_IDLE;
                    PanelOnScanStateChanged();
                }
                return 0;
            }
            if (wp == TIMER_STARTUP_TRIM) {
                KillTimer(hwnd, TIMER_STARTUP_TRIM);
                if (!PanelIsVisible() && !g_state.modal_open) {
                    TrayReleaseRenderCache();
                    TrimIdleWorkingSet();
                }
                return 0;
            }
            if (wp == TIMER_AUTO_UPDATE_BOOT) {
                KillTimer(hwnd, TIMER_AUTO_UPDATE_BOOT);
                MaybeStartBackgroundUpdateCheck();
                return 0;
            }
            if (wp == TIMER_AUTO_UPDATE_POLL) {
                MaybeStartBackgroundUpdateCheck();
                return 0;
            }
            break;

        case WM_APP_BG_UPDATE_DONE: {
            if (g_bg_update_thread) {
                CloseHandle(g_bg_update_thread);
                g_bg_update_thread = nullptr;
            }
            g_state.update_check_running = false;

            UpdateInfo info = {};
            bool ok = false;
            AcquireSRWLockExclusive(&g_bg_update_lock);
            info = g_bg_update_result;
            ok = g_bg_update_ok;
            ReleaseSRWLockExclusive(&g_bg_update_lock);

            if (ok) {
                g_state.last_update_check_sec = GetCurrentUnixTimeSec();
                SaveUiConfig();
                if (info.has_update && CompareSemver(info.latest_version, OCTANS_VERSION_STR) > 0) {
                    g_cached_update = info;
                    g_state.update_available = true;
                    strncpy(g_state.update_version, info.latest_version, sizeof(g_state.update_version) - 1);
                    g_state.update_version[sizeof(g_state.update_version) - 1] = '\0';
                    PanelOnUpdateStateChanged();
                } else if (g_state.update_available) {
                    g_state.update_available = false;
                    g_state.update_version[0] = '\0';
                    PanelOnUpdateStateChanged();
                }
            }
            if (!PanelIsVisible() && !g_state.modal_open) {
                SetTimer(hwnd, TIMER_STARTUP_TRIM, 800u, nullptr);
            }
            return 0;
        }

        case WM_APP_TRAY_CALLBACK: {
            UINT ev = LOWORD(lp);
            UINT clicked_uid = HIWORD(lp);
            /* NOTIFYICON_VERSION_4 emits both WM_LBUTTONUP and NIN_SELECT on one click;
             * handle only the canonical v4 NIN_SELECT / NIN_KEYSELECT to avoid instant open+close. */
            if (ev == NIN_SELECT || ev == NIN_KEYSELECT) {
                uint64_t now = GetTickCount64();
                if (!PanelIsVisible() && (now - g_state.last_inactive_hide_ms) < 260ULL) {
                    return 0;
                }
                POINT anchor = TrayGetAnchorPoint(clicked_uid);
                PanelShow(anchor.x, anchor.y, true);
                return 0;
            }
            if (ev == WM_CONTEXTMENU) {
                POINT cur = {};
                GetCursorPos(&cur);
                int dev_idx = TrayFindDeviceIndexForUid(clicked_uid);
                ActionShowContextMenu(cur.x, cur.y, dev_idx, MENU_MODE_TRAY);
                return 0;
            }
            return 0;
        }

        case WM_APP_DEVICES_UPDATED: {
            octans_device_view_t local[MAX_DEVICES];
            size_t count = 0;
            bool has = false;
            AcquireSRWLockExclusive(&g_state.pending_lock);
            if (g_state.has_pending_devices) {
                count = g_state.pending_count;
                if (count > 0) {
                    memcpy(local, g_state.pending_devices, count * sizeof(octans_device_view_t));
                }
                g_state.has_pending_devices = false;
                has = true;
            }
            ReleaseSRWLockExclusive(&g_state.pending_lock);
            if (has) {
                ApplyDeviceSnapshotOnUiThread(local, count);
            }
            return 0;
        }

        case WM_APP_SHOW_PANEL: {
            int ax = static_cast<int>(static_cast<intptr_t>(wp));
            int ay = static_cast<int>(static_cast<intptr_t>(lp));
            ResolveDefaultAnchor(&ax, &ay);
            PanelShow(ax, ay, true);
            return 0;
        }

        case WM_APP_HIDE_PANEL:
            PanelHide();
            return 0;

        case WM_APP_THEME_CHANGED: {
            bool dark = (wp != 0);
            g_state.is_dark = dark;
            PanelOnThemeChanged(dark);
            TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
            return 0;
        }

        case WM_SETTINGCHANGE: {
            if (lp && wcscmp(reinterpret_cast<const wchar_t *>(lp), L"ImmersiveColorSet") == 0) {
                bool dark = DetectSystemDarkMode();
                g_state.is_dark = dark;
                PanelOnThemeChanged(dark);
                TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
            }
            return 0;
        }

        case WM_DPICHANGED:
            TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} /* namespace octans_ui */

/* ============================================================================
 * Public extern "C" ABI (include/octans_ui_api.h — OCTANS_UI_API_VERSION == 1)
 * ============================================================================ */

extern "C" int octans_ui_init(const octans_backend_ops_t *ops, void *backend_ctx) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (st.initialized) return 0;

    EnablePerMonitorDpiV2();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    st.com_initialized = SUCCEEDED(hr);

    st.ui_thread_id = GetCurrentThreadId();
    if (ops) {
        st.ops = *ops;
    } else {
        st.ops = {};
    }
    st.backend_ctx = backend_ctx;
    st.is_dark = DetectSystemDarkMode();
    st.tray_style = TRAY_STYLE_OCTANS;
    st.tray_scale = kDefaultTrayScale;
    st.has_custom_panel_pos = false;
    st.custom_panel_x = 0;
    st.custom_panel_y = 0;
    st.auto_check_updates = true;
    st.last_update_check_sec = 0;
    st.update_available = false;
    st.update_check_running = false;
    st.update_version[0] = '\0';
    g_cached_update = {};
    InterlockedExchange(&g_bg_cancel, 0);
    st.device_count = 0;
    st.pinned_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) st.pinned_ids[i][0] = '\0';
    st.modal_open = false;
    st.is_scanning = true;
    st.scan_phase = SCAN_ACTIVE;
    st.initial_scan_done = false;
    st.scan_started_ms = GetTickCount64();
    st.scan_completed_ms = 0;
    st.refresh_pending = false;
    st.hidden_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) st.hidden_ids[i][0] = '\0';
    st.last_inactive_hide_ms = 0;
    g_has_deferred = false;
    LoadUiConfig();

    g_msg_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    static const wchar_t *kHostClass = L"OctansUiHostWnd";
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);

    /* Top-level invisible host window (not HWND_MESSAGE) so it receives WM_SETTINGCHANGE & TaskbarCreated. */
    st.host_hwnd = CreateWindowExW(
        0,
        kHostClass,
        L"OctansHost",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        nullptr,
        nullptr,
        hinst,
        nullptr
    );
    if (!st.host_hwnd) {
        if (st.com_initialized) {
            CoUninitialize();
            st.com_initialized = false;
        }
        return -1;
    }
    DisableWindowIme(st.host_hwnd);

    FontsInit();

    if (!PanelInit()) {
        FontsShutdown();
        DestroyWindow(st.host_hwnd);
        st.host_hwnd = nullptr;
        if (st.com_initialized) {
            CoUninitialize();
            st.com_initialized = false;
        }
        return -2;
    }

    TrayInit(st.host_hwnd);
    if (st.demo_hooks.on_action == nullptr) {
        SetTimer(st.host_hwnd, TIMER_AUTO_UPDATE_BOOT, 30000u, nullptr);
        SetTimer(st.host_hwnd, TIMER_AUTO_UPDATE_POLL, 21600000u, nullptr);
    }
    st.initialized = true;
    return 0;
}

extern "C" void octans_ui_shutdown(void) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;

    InterlockedExchange(&g_bg_cancel, 1);
    if (st.host_hwnd) {
        KillTimer(st.host_hwnd, TIMER_AUTO_UPDATE_BOOT);
        KillTimer(st.host_hwnd, TIMER_AUTO_UPDATE_POLL);
    }
    if (g_bg_update_thread) {
        WaitForSingleObject(g_bg_update_thread, 1500u);
        CloseHandle(g_bg_update_thread);
        g_bg_update_thread = nullptr;
    }

    TrayShutdown();
    PanelShutdown();
    FontsShutdown();

    if (st.host_hwnd) {
        DestroyWindow(st.host_hwnd);
        st.host_hwnd = nullptr;
    }
    if (st.com_initialized) {
        CoUninitialize();
        st.com_initialized = false;
    }
    st.initialized = false;
}

extern "C" void octans_ui_on_devices_changed(const octans_device_view_t *devices, size_t count) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;
    if (count > MAX_DEVICES) count = MAX_DEVICES;

    if (GetCurrentThreadId() == st.ui_thread_id) {
        ApplyDeviceSnapshotOnUiThread(devices, count);
    } else if (st.host_hwnd) {
        AcquireSRWLockExclusive(&st.pending_lock);
        if (devices && count > 0) {
            memcpy(st.pending_devices, devices, count * sizeof(octans_device_view_t));
        }
        st.pending_count = count;
        st.has_pending_devices = true;
        ReleaseSRWLockExclusive(&st.pending_lock);
        PostMessageW(st.host_hwnd, WM_APP_DEVICES_UPDATED, 0, 0);
    }
}

extern "C" void octans_ui_show_panel(int anchor_x, int anchor_y) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;

    if (GetCurrentThreadId() == st.ui_thread_id) {
        ResolveDefaultAnchor(&anchor_x, &anchor_y);
        PanelShow(anchor_x, anchor_y, true);
    } else if (st.host_hwnd) {
        PostMessageW(
            st.host_hwnd,
            WM_APP_SHOW_PANEL,
            static_cast<WPARAM>(static_cast<intptr_t>(anchor_x)),
            static_cast<LPARAM>(static_cast<intptr_t>(anchor_y))
        );
    }
}

extern "C" void octans_ui_hide_panel(void) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;

    if (GetCurrentThreadId() == st.ui_thread_id) {
        PanelHide();
    } else if (st.host_hwnd) {
        PostMessageW(st.host_hwnd, WM_APP_HIDE_PANEL, 0, 0);
    }
}

extern "C" void octans_ui_on_theme_changed(int is_dark_mode) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;

    if (GetCurrentThreadId() == st.ui_thread_id) {
        st.is_dark = (is_dark_mode != 0);
        PanelOnThemeChanged(st.is_dark);
        TraySyncAll(st.devices, st.device_count, st.pinned_ids, st.pinned_count, st.tray_style, st.tray_scale);
    } else if (st.host_hwnd) {
        PostMessageW(st.host_hwnd, WM_APP_THEME_CHANGED, is_dark_mode ? 1 : 0, 0);
    }
}
