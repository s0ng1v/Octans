#include "ui_internal.h"
#include "ui_theme.h"
#include "ui_fonts.h"
#include "ui_tray.h"
#include "ui_panel.h"
#include "ui_menu.h"
#include "ui_rename_dlg.h"
#include <stdio.h>

namespace octans_ui {

static UiState g_state;
static UINT g_msg_taskbar_created = 0;

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
    g_state.tray_style = (style_v == 1) ? TRAY_STYLE_ICON_NUMBER : TRAY_STYLE_RING_GLYPH;

    int scale_pct = static_cast<int>(GetPrivateProfileIntW(L"UI", L"TrayScalePct", 50, ini));
    g_state.tray_scale = ClampF(static_cast<float>(scale_pct) / 100.0f, 0.0f, 1.0f);

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
}

static void SaveUiConfig() {
    if (g_state.demo_hooks.on_action != nullptr) return;

    wchar_t dir[MAX_PATH] = {};
    wchar_t ini[MAX_PATH] = {};
    GetUiIniPath(dir, ini);
    if (dir[0]) CreateDirectoryW(dir, nullptr);

    wchar_t buf[32] = {};
    _snwprintf(buf, 31, L"%d", (g_state.tray_style == TRAY_STYLE_ICON_NUMBER) ? 1 : 0);
    WritePrivateProfileStringW(L"UI", L"TrayStyle", buf, ini);

    int pct = static_cast<int>(roundf(ClampF(g_state.tray_scale, 0.0f, 1.0f) * 100.0f));
    _snwprintf(buf, 31, L"%d", pct);
    WritePrivateProfileStringW(L"UI", L"TrayScalePct", buf, ini);

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

static void ApplyDeviceSnapshotOnUiThread(const octans_device_view_t *devices, size_t count) {
    if (count > MAX_DEVICES) count = MAX_DEVICES;
    if (devices && count > 0) {
        memcpy(g_state.devices, devices, count * sizeof(octans_device_view_t));
    }
    g_state.device_count = count;

    /* Default to pinning the first device so the single startup tray icon matches Card #1 1:1. */
    if (g_state.pinned_count == 0 && g_state.device_count > 0 && g_state.devices[0].id[0] != '\0') {
        strncpy(g_state.pinned_ids[0], g_state.devices[0].id, 63);
        g_state.pinned_ids[0][63] = '\0';
        g_state.pinned_count = 1;
        SaveUiConfig();
        if (g_state.ops.set_pinned_device) {
            g_state.ops.set_pinned_device(g_state.backend_ctx, g_state.pinned_ids[0]);
        }
    }

    PanelOnDevicesChanged(g_state.devices, g_state.device_count);
    TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
}

void ActionTriggerRefresh() {
    PanelTriggerRefreshSpin();
    if (g_state.ops.request_refresh) {
        g_state.ops.request_refresh(g_state.backend_ctx);
    }
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
        /* Always keep at least 1 pinned device so tray icon <-> pinned card stay strictly 1:1 */
        if (g_state.pinned_count <= 1) {
            PanelOnPinnedChanged();
            return;
        }
        /* Unpin: remove from pinned_ids preserving order */
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
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
}

void ActionToggleTrayStyle(TrayStyle style) {
    if (g_state.tray_style != style) {
        g_state.tray_style = style;
        SaveUiConfig();
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
}

void ActionSetTrayScale(float scale) {
    float clamped = ClampF(scale, 0.0f, 1.0f);
    if (fabsf(g_state.tray_scale - clamped) >= 0.005f) {
        g_state.tray_scale = clamped;
        SaveUiConfig();
        TraySyncAll(g_state.devices, g_state.device_count, g_state.pinned_ids, g_state.pinned_count, g_state.tray_style, g_state.tray_scale);
    }
}

void ActionToggleAutostart() {
    if (!g_state.ops.set_autostart) return;
    int cur = g_state.ops.get_autostart ? g_state.ops.get_autostart(g_state.backend_ctx) : 0;
    g_state.ops.set_autostart(g_state.backend_ctx, cur ? 0 : 1);
}

void ActionLaunchDeviceHelper() {
    wchar_t exe_dir[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return;
    wchar_t *slash = wcsrchr(exe_dir, L'\\');
    if (slash) *slash = L'\0';

    wchar_t helper[MAX_PATH] = {};
    _snwprintf(helper, MAX_PATH - 1, L"%ls\\octans-add.exe", exe_dir);
    helper[MAX_PATH - 1] = L'\0';

    if (GetFileAttributesW(helper) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(nullptr, L"open", helper, nullptr, exe_dir, SW_SHOWNORMAL);
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
        g_state.tray_style = TRAY_STYLE_RING_GLYPH;
        g_state.tray_scale = kDefaultTrayScale;
        g_state.pinned_count = 0;
        for (size_t i = 0; i < MAX_DEVICES; ++i) g_state.pinned_ids[i][0] = '\0';
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
    st.tray_style = TRAY_STYLE_RING_GLYPH;
    st.tray_scale = kDefaultTrayScale;
    st.device_count = 0;
    st.pinned_count = 0;
    for (size_t i = 0; i < MAX_DEVICES; ++i) st.pinned_ids[i][0] = '\0';
    st.modal_open = false;
    st.last_inactive_hide_ms = 0;
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
    st.initialized = true;
    return 0;
}

extern "C" void octans_ui_shutdown(void) {
    using namespace octans_ui;
    UiState &st = GetState();
    if (!st.initialized) return;

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
