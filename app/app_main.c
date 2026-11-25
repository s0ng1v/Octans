#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <dbt.h>
#include <hidsdi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "octans_core.h"
#include "octans_ui_api.h"

#define TIMER_HOTPLUG_DEBOUNCE 1
#define TIMER_RESUME_DEBOUNCE  2
#define TIMER_SMOKE_EXIT       3

typedef struct pending_alias_cmd {
    char id[64];
    wchar_t alias[64];
} pending_alias_cmd_t;

typedef struct octans_app {
    octans_engine_t *eng;
    octans_backend_ops_t backend_ops;

    HWND ctrl_hwnd;
    HDEVNOTIFY dev_notify;
    UINT msg_show_panel;
    UINT msg_reload_rules;

    HANDLE wake_event;
    HANDLE worker_thread;
    volatile LONG quit_flag;
    volatile LONG force_rescan;
    volatile LONG want_reload_rules;

    SRWLOCK cmd_lock;
    int has_pending_pin;
    char pending_pin[64];
    pending_alias_cmd_t pending_aliases[4];
    size_t pending_alias_count;

    int silent_mode;
    int smoke_ms;
} octans_app_t;

static octans_app_t g_app;

static void enable_per_monitor_dpi_v2(void) {
    typedef BOOL(WINAPI *set_dpi_ctx_fn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        set_dpi_ctx_fn fn = (set_dpi_ctx_fn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn) {
            fn((HANDLE)(intptr_t)-4);
        }
    }
}

static int has_arg_flag(const wchar_t *cmdline, const wchar_t *flag) {
    if (!cmdline || !flag) return 0;
    const wchar_t *p = wcsstr(cmdline, flag);
    if (!p) return 0;
    size_t flen = wcslen(flag);
    wchar_t before = (p == cmdline) ? L' ' : p[-1];
    wchar_t after = p[flen];
    return ((before == L' ' || before == L'\t' || before == L'"') &&
            (after == L'\0' || after == L' ' || after == L'\t'));
}

static int parse_smoke_ms(const wchar_t *cmdline) {
    if (!cmdline) return 0;
    const wchar_t *p = wcsstr(cmdline, L"--smoke");
    if (!p) return 0;
    p += 7;
    while (*p == L' ' || *p == L'\t') ++p;
    int ms = _wtoi(p);
    return (ms > 0) ? ms : 1500;
}

static void get_config_ini_path(wchar_t dir_out[MAX_PATH], wchar_t ini_out[MAX_PATH]) {
    wchar_t appdata[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH - 32) {
        _snwprintf(dir_out, MAX_PATH - 1, L"%ls\\Octans", appdata);
        dir_out[MAX_PATH - 1] = L'\0';
        _snwprintf(ini_out, MAX_PATH - 1, L"%ls\\Octans\\config.ini", appdata);
        ini_out[MAX_PATH - 1] = L'\0';
    } else {
        dir_out[0] = L'\0';
        wcscpy(ini_out, L"octans.ini");
    }
}

static void init_default_autostart(octans_app_t *app) {
    if (!app || app->smoke_ms > 0) return;

    wchar_t dir[MAX_PATH];
    wchar_t ini[MAX_PATH];
    get_config_ini_path(dir, ini);

    UINT inited = GetPrivateProfileIntW(L"General", L"AutostartInitialized", 0, ini);
    if (inited == 0) {
        if (app->backend_ops.set_autostart && app->backend_ops.set_autostart(app->eng, 1) == 0) {
            if (dir[0]) CreateDirectoryW(dir, NULL);
            WritePrivateProfileStringW(L"General", L"AutostartInitialized", L"1", ini);
        }
    } else if (app->backend_ops.get_autostart && app->backend_ops.get_autostart(app->eng) == 1) {
        /* Keep Run path synchronized if portable Octans.exe was moved. */
        if (app->backend_ops.set_autostart) {
            app->backend_ops.set_autostart(app->eng, 1);
        }
    }
}

/* Non-blocking UI -> Backend callbacks (safe on Win32 UI thread). */
static void ui_op_request_refresh(void *ctx) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (!app) return;
    InterlockedExchange(&app->force_rescan, 1);
    if (app->wake_event) SetEvent(app->wake_event);
}

static int ui_op_set_pinned_device(void *ctx, const char *device_id) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (!app) return -1;
    AcquireSRWLockExclusive(&app->cmd_lock);
    if (device_id && device_id[0]) {
        strncpy(app->pending_pin, device_id, sizeof(app->pending_pin) - 1);
        app->pending_pin[sizeof(app->pending_pin) - 1] = '\0';
    } else {
        app->pending_pin[0] = '\0';
    }
    app->has_pending_pin = 1;
    ReleaseSRWLockExclusive(&app->cmd_lock);
    if (app->wake_event) SetEvent(app->wake_event);
    return 0;
}

static int ui_op_set_device_alias(void *ctx, const char *device_id, const wchar_t *alias) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (!app || !device_id || !device_id[0]) return -1;

    AcquireSRWLockExclusive(&app->cmd_lock);
    size_t idx = app->pending_alias_count;
    for (size_t i = 0; i < app->pending_alias_count && i < 4; ++i) {
        if (strcmp(app->pending_aliases[i].id, device_id) == 0) {
            idx = i;
            break;
        }
    }
    if (idx == app->pending_alias_count) {
        if (app->pending_alias_count < 4) {
            idx = app->pending_alias_count++;
        } else {
            idx = 0;
        }
        strncpy(app->pending_aliases[idx].id, device_id, sizeof(app->pending_aliases[idx].id) - 1);
        app->pending_aliases[idx].id[sizeof(app->pending_aliases[idx].id) - 1] = '\0';
    }
    if (alias) {
        wcsncpy(app->pending_aliases[idx].alias, alias, 63);
        app->pending_aliases[idx].alias[63] = L'\0';
    } else {
        app->pending_aliases[idx].alias[0] = L'\0';
    }
    ReleaseSRWLockExclusive(&app->cmd_lock);

    if (app->wake_event) SetEvent(app->wake_event);
    return 0;
}

static int ui_op_set_autostart(void *ctx, int enabled) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (!app || !app->backend_ops.set_autostart) return -1;
    return app->backend_ops.set_autostart(app->eng, enabled);
}

static int ui_op_get_autostart(void *ctx) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (!app || !app->backend_ops.get_autostart) return 0;
    return app->backend_ops.get_autostart(app->eng);
}

static void ui_op_request_quit(void *ctx) {
    octans_app_t *app = (octans_app_t *)ctx;
    if (app) {
        InterlockedExchange(&app->quit_flag, 1);
        if (app->wake_event) SetEvent(app->wake_event);
    }
    PostQuitMessage(0);
}

static void flush_pending_commands_on_worker(octans_app_t *app) {
    int do_pin = 0;
    char pin_id[64] = {0};
    pending_alias_cmd_t aliases[4];
    size_t alias_n = 0;

    AcquireSRWLockExclusive(&app->cmd_lock);
    if (app->has_pending_pin) {
        do_pin = 1;
        strncpy(pin_id, app->pending_pin, sizeof(pin_id) - 1);
        app->has_pending_pin = 0;
    }
    if (app->pending_alias_count > 0) {
        alias_n = app->pending_alias_count;
        memcpy(aliases, app->pending_aliases, alias_n * sizeof(pending_alias_cmd_t));
        app->pending_alias_count = 0;
    }
    ReleaseSRWLockExclusive(&app->cmd_lock);

    if (do_pin && app->backend_ops.set_pinned_device) {
        app->backend_ops.set_pinned_device(app->eng, pin_id);
    }
    for (size_t i = 0; i < alias_n; ++i) {
        if (app->backend_ops.set_device_alias) {
            app->backend_ops.set_device_alias(app->eng, aliases[i].id, aliases[i].alias);
        }
    }
}

static DWORD WINAPI backend_worker_proc(LPVOID param) {
    octans_app_t *app = (octans_app_t *)param;
    if (!app || !app->eng) return 0;

    /* Initial discovery + telemetry poll */
    octans_engine_poll(app->eng, 1);

    while (!InterlockedCompareExchange(&app->quit_flag, 0, 0)) {
        DWORD wait_rc = WaitForSingleObject(app->wake_event, 15000u);
        if (InterlockedCompareExchange(&app->quit_flag, 0, 0)) {
            break;
        }

        flush_pending_commands_on_worker(app);
        int reload = (int)InterlockedExchange(&app->want_reload_rules, 0);
        if (reload) {
            octans_engine_reload_rules(app->eng);
        }
        int force = (int)InterlockedExchange(&app->force_rescan, 0);
        if (wait_rc == WAIT_TIMEOUT || force || reload) {
            octans_engine_poll(app->eng, force || reload);
        }
    }
    return 0;
}

static LRESULT CALLBACK app_ctrl_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app.msg_show_panel != 0 && msg == g_app.msg_show_panel) {
        octans_ui_show_panel(-1, -1);
        return 0;
    }
    if (g_app.msg_reload_rules != 0 && msg == g_app.msg_reload_rules) {
        InterlockedExchange(&g_app.want_reload_rules, 1);
        InterlockedExchange(&g_app.force_rescan, 1);
        if (g_app.wake_event) SetEvent(g_app.wake_event);
        return 0;
    }

    switch (msg) {
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL ||
                wp == DBT_DEVICEREMOVECOMPLETE ||
                wp == DBT_DEVNODES_CHANGED) {
                SetTimer(hwnd, TIMER_HOTPLUG_DEBOUNCE, 600u, NULL);
            }
            return TRUE;

        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
                SetTimer(hwnd, TIMER_RESUME_DEBOUNCE, 1500u, NULL);
            }
            return TRUE;

        case WM_TIMER:
            if (wp == TIMER_HOTPLUG_DEBOUNCE || wp == TIMER_RESUME_DEBOUNCE) {
                KillTimer(hwnd, wp);
                InterlockedExchange(&g_app.force_rescan, 1);
                if (g_app.wake_event) SetEvent(g_app.wake_event);
                return 0;
            }
            if (wp == TIMER_SMOKE_EXIT) {
                KillTimer(hwnd, TIMER_SMOKE_EXIT);
                octans_ui_hide_panel();
                ui_op_request_quit(&g_app);
                return 0;
            }
            break;

        case WM_CLOSE:
            ui_op_request_quit(&g_app);
            return 0;

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (wp != 0) {
                ui_op_request_quit(&g_app);
            }
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE prev, PWSTR cmdline, int show) {
    (void)prev;
    (void)show;

    enable_per_monitor_dpi_v2();

    memset(&g_app, 0, sizeof(g_app));
    InitializeSRWLock(&g_app.cmd_lock);
    g_app.silent_mode = has_arg_flag(cmdline, L"--silent") ||
                        has_arg_flag(cmdline, L"--autostart");
    g_app.smoke_ms = parse_smoke_ms(cmdline);
    g_app.msg_show_panel = RegisterWindowMessageW(L"OctansShowPanelMsg");
    g_app.msg_reload_rules = RegisterWindowMessageW(L"OctansReloadRulesMsg");

    /* Single-instance check (skip in automated --smoke runs if desired, or keep strict) */
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\OctansBatteryTraySingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!g_app.silent_mode && g_app.smoke_ms == 0) {
            HWND existing = FindWindowW(L"OctansAppCtrlWnd", NULL);
            if (existing && g_app.msg_show_panel != 0) {
                PostMessageW(existing, g_app.msg_show_panel, 0, 0);
            }
        }
        CloseHandle(mutex);
        return 0;
    }

    octans_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.poll_interval_ms = 15000u;
    cfg.passive_listen_ms = 250u;
    cfg.debug_log = 0;
    cfg.ui_notify = octans_ui_on_devices_changed;

    g_app.eng = octans_engine_create(&cfg);
    if (!g_app.eng) {
        if (mutex) CloseHandle(mutex);
        return 1;
    }
    octans_engine_bind_ui(g_app.eng, &g_app.backend_ops);

    /* Enable autostart by default on first run (writes quoted path + --silent). */
    init_default_autostart(&g_app);

    octans_backend_ops_t ui_ops;
    memset(&ui_ops, 0, sizeof(ui_ops));
    ui_ops.request_refresh = ui_op_request_refresh;
    ui_ops.set_pinned_device = ui_op_set_pinned_device;
    ui_ops.set_device_alias = ui_op_set_device_alias;
    ui_ops.set_autostart = ui_op_set_autostart;
    ui_ops.get_autostart = ui_op_get_autostart;
    ui_ops.request_quit = ui_op_request_quit;

    if (octans_ui_init(&ui_ops, &g_app) != 0) {
        octans_engine_destroy(g_app.eng);
        if (mutex) CloseHandle(mutex);
        return 2;
    }

    static const wchar_t *kCtrlClass = L"OctansAppCtrlWnd";
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = app_ctrl_wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = kCtrlClass;
    RegisterClassExW(&wc);

    g_app.ctrl_hwnd = CreateWindowExW(
        0,
        kCtrlClass,
        L"OctansCtrl",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        NULL, NULL, hinst, NULL
    );

    if (g_app.ctrl_hwnd) {
        GUID hid_guid;
        HidD_GetHidGuid(&hid_guid);
        DEV_BROADCAST_DEVICEINTERFACE_W dbi;
        memset(&dbi, 0, sizeof(dbi));
        dbi.dbcc_size = sizeof(dbi);
        dbi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        dbi.dbcc_classguid = hid_guid;
        g_app.dev_notify = RegisterDeviceNotificationW(
            g_app.ctrl_hwnd, &dbi, DEVICE_NOTIFY_WINDOW_HANDLE);

        if (g_app.smoke_ms > 0) {
            SetTimer(g_app.ctrl_hwnd, TIMER_SMOKE_EXIT, (UINT)g_app.smoke_ms, NULL);
        }
    }

    g_app.wake_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_app.wake_event) {
        g_app.worker_thread = CreateThread(
            NULL, 65536u, backend_worker_proc, &g_app, 0, NULL);
    }

    /* Manual launch opens the flyout panel (triggers Wordmark entrance animation);
     * --silent (autostart) starts quietly in the system tray. */
    if (!g_app.silent_mode) {
        octans_ui_show_panel(-1, -1);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Clean shutdown: wake worker, wait for poll cycle exit, free engine & UI. */
    InterlockedExchange(&g_app.quit_flag, 1);
    if (g_app.wake_event) SetEvent(g_app.wake_event);
    int worker_joined = 1;
    if (g_app.worker_thread) {
        worker_joined = (WaitForSingleObject(g_app.worker_thread, 5000u) == WAIT_OBJECT_0);
        CloseHandle(g_app.worker_thread);
        g_app.worker_thread = NULL;
    }

    if (g_app.dev_notify) {
        UnregisterDeviceNotification(g_app.dev_notify);
        g_app.dev_notify = NULL;
    }
    if (g_app.ctrl_hwnd) {
        DestroyWindow(g_app.ctrl_hwnd);
        g_app.ctrl_hwnd = NULL;
    }
    if (g_app.wake_event) {
        CloseHandle(g_app.wake_event);
        g_app.wake_event = NULL;
    }

    if (worker_joined) {
        octans_engine_destroy(g_app.eng);
    }
    g_app.eng = NULL;
    octans_ui_shutdown();

    if (mutex) CloseHandle(mutex);
    return 0;
}
