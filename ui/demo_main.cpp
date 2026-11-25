#include "ui_internal.h"
#include "ui_tray.h"
#include "ui_theme.h"
#include <stdlib.h>

using namespace octans_ui;

static constexpr size_t kMockPoolSize = 7;

struct MockBackend {
    octans_device_view_t pool[kMockPoolSize] = {};
    size_t active_count = 4; /* Cycles: 4 (主力全套) -> 7 (全品类7种) -> 2 -> 1 -> 0 -> 4 */
    int autostart = 1;
    bool is_dark = true;
    bool sim_phase = false;
    char pinned_id[64] = {};
};

static MockBackend g_mock;

/* Realistic single-PC wireless desk setup aligned with Octans backend supported device matrix:
 * Default 4 cards (each a distinct peripheral category — zero duplicate mouse/keyboard):
 *   1. Mouse    : Logitech GPW4 鹰眼 (HID++ 2.0, 2.4G, 78%, normal)
 *   2. Keyboard : Keychron Q1 Pro    (Keychron 2.4G, 92%, charging)
 *   3. Headset  : HyperX Cloud III S (HyperX Gen3 2.4G, 16%, low battery alert)
 *   4. Gamepad  : Xbox Elite 2 手柄   (XInput/BT, ~64%, estimated)
 * Press 'C' to expand to 7 devices covering all Bluetooth / Standard HID fallback kinds:
 *   5. Earbuds  : AirPods Pro 2      (Bluetooth HFP/BLE, 85%, normal)
 *   6. Stylus   : Surface Slim Pen 2 (Standard HID 0x0D, 46%, sleeping)
 *   7. Speaker  : JBL Flip 6 音箱     (Bluetooth, offline) */
static void FillMockDevices(MockBackend &m) {
    uint64_t now = GetTickCount64();

    /* 1. 无线鼠标: Logitech GPW4 鹰眼 (后端实机已测通 HID++ 2.0) */
    {
        octans_device_view_t &d = m.pool[0];
        d = {};
        strncpy(d.id, "logi-gpw4-046d-c54d", sizeof(d.id) - 1);
        wcsncpy(d.name, L"Logitech GPW4 \u9e70\u773c", 63);
        wcsncpy(d.brand, L"Logitech", 31);
        d.vid = 0x046D;
        d.pid = 0xC54D;
        d.kind = OCTANS_DEV_MOUSE;
        d.conn = OCTANS_CONN_DONGLE_24G;
        d.power_state = OCTANS_PWR_DISCHARGING;
        d.battery_percent = 78;
        d.voltage_mv = 4050;
        d.signal_percent = 98;
        d.is_low_battery = 0;
        d.is_estimated = 0;
        d.updated_ms = now;
    }

    /* 2. 无线键盘: Keychron Q1 Pro (后端内置 Keychron 2.4G, 充电中) */
    {
        octans_device_view_t &d = m.pool[1];
        d = {};
        strncpy(d.id, "keychron-q1pro-3434-d028", sizeof(d.id) - 1);
        wcsncpy(d.name, L"Keychron Q1 Pro", 63);
        wcsncpy(d.brand, L"Keychron", 31);
        d.vid = 0x3434;
        d.pid = 0xD028;
        d.kind = OCTANS_DEV_KEYBOARD;
        d.conn = OCTANS_CONN_DONGLE_24G;
        d.power_state = OCTANS_PWR_CHARGING;
        d.battery_percent = 92;
        d.voltage_mv = 4180;
        d.signal_percent = 95;
        d.is_low_battery = 0;
        d.is_estimated = 0;
        d.updated_ms = now;
    }

    /* 3. 无线耳机: HyperX Cloud III S (后端内置 HyperX 2.4G, 低电量警示) */
    {
        octans_device_view_t &d = m.pool[2];
        d = {};
        strncpy(d.id, "hyperx-cloud3s-03f0-098d", sizeof(d.id) - 1);
        wcsncpy(d.name, L"HyperX Cloud III S", 63);
        wcsncpy(d.brand, L"HyperX", 31);
        d.vid = 0x03F0;
        d.pid = 0x098D;
        d.kind = OCTANS_DEV_HEADSET;
        d.conn = OCTANS_CONN_DONGLE_24G;
        d.power_state = OCTANS_PWR_DISCHARGING;
        d.battery_percent = 16;
        d.voltage_mv = 3580;
        d.signal_percent = 91;
        d.is_low_battery = 1;
        d.is_estimated = 0;
        d.updated_ms = now;
    }

    /* 4. 无线手柄: Xbox Elite 2 (后端内置 XInput/蓝牙, 估算电量) */
    {
        octans_device_view_t &d = m.pool[3];
        d = {};
        strncpy(d.id, "xbox-elite2-045e-0b05", sizeof(d.id) - 1);
        wcsncpy(d.name, L"Xbox Elite 2 \u624b\u67c4", 63);
        wcsncpy(d.brand, L"Microsoft", 31);
        d.vid = 0x045E;
        d.pid = 0x0B05;
        d.kind = OCTANS_DEV_GAMEPAD;
        d.conn = OCTANS_CONN_BLUETOOTH;
        d.power_state = OCTANS_PWR_DISCHARGING;
        d.battery_percent = 64;
        d.voltage_mv = 3920;
        d.signal_percent = -1;
        d.is_low_battery = 0;
        d.is_estimated = 1;
        d.updated_ms = now;
    }

    /* 5. 真无线耳机: AirPods Pro 2 (Windows 系统蓝牙 DEVPKEY 通道) */
    {
        octans_device_view_t &d = m.pool[4];
        d = {};
        strncpy(d.id, "bt-airpods-pro2", sizeof(d.id) - 1);
        wcsncpy(d.name, L"AirPods Pro 2", 63);
        wcsncpy(d.brand, L"Apple", 31);
        d.kind = OCTANS_DEV_EARBUDS;
        d.conn = OCTANS_CONN_BLUETOOTH;
        d.power_state = OCTANS_PWR_DISCHARGING;
        d.battery_percent = 85;
        d.voltage_mv = 0;
        d.signal_percent = -1;
        d.is_low_battery = 0;
        d.is_estimated = 0;
        d.updated_ms = now - 15000ULL;
    }

    /* 6. 无线触控笔: Surface Slim Pen 2 (标准 HID 0x0D 通道, 休眠态) */
    {
        octans_device_view_t &d = m.pool[5];
        d = {};
        strncpy(d.id, "hid-surface-pen2", sizeof(d.id) - 1);
        wcsncpy(d.name, L"Surface Slim Pen 2", 63);
        wcsncpy(d.brand, L"Microsoft", 31);
        d.kind = OCTANS_DEV_STYLUS;
        d.conn = OCTANS_CONN_BLUETOOTH;
        d.power_state = OCTANS_PWR_SLEEPING;
        d.battery_percent = 46;
        d.voltage_mv = 0;
        d.signal_percent = -1;
        d.is_low_battery = 0;
        d.is_estimated = 0;
        d.updated_ms = now - 120000ULL;
    }

    /* 7. 蓝牙音箱: JBL Flip 6 (系统蓝牙通道, 离线态) */
    {
        octans_device_view_t &d = m.pool[6];
        d = {};
        strncpy(d.id, "bt-jbl-flip6", sizeof(d.id) - 1);
        wcsncpy(d.name, L"JBL Flip 6 \u97f3\u7bb1", 63);
        wcsncpy(d.brand, L"JBL", 31);
        d.kind = OCTANS_DEV_SPEAKER;
        d.conn = OCTANS_CONN_BLUETOOTH;
        d.power_state = OCTANS_PWR_OFFLINE;
        d.battery_percent = -1;
        d.voltage_mv = 0;
        d.signal_percent = -1;
        d.is_low_battery = 0;
        d.is_estimated = 0;
        d.updated_ms = now - 360000ULL;
    }
}

static void PushMockSnapshot() {
    octans_ui_on_devices_changed(g_mock.pool, g_mock.active_count);
}

static void MockRequestRefresh(void *ctx) {
    auto *m = static_cast<MockBackend *>(ctx);
    uint64_t now = GetTickCount64();
    for (size_t i = 0; i < 4; ++i) {
        m->pool[i].updated_ms = now;
    }
    PushMockSnapshot();
}

static int MockSetPinnedDevice(void *ctx, const char *device_id) {
    auto *m = static_cast<MockBackend *>(ctx);
    if (device_id) {
        strncpy(m->pinned_id, device_id, sizeof(m->pinned_id) - 1);
        m->pinned_id[sizeof(m->pinned_id) - 1] = '\0';
    } else {
        m->pinned_id[0] = '\0';
    }
    return 0;
}

static int MockSetDeviceAlias(void *ctx, const char *device_id, const wchar_t *alias) {
    auto *m = static_cast<MockBackend *>(ctx);
    if (!device_id || !alias) return -1;
    for (size_t i = 0; i < kMockPoolSize; ++i) {
        if (strcmp(m->pool[i].id, device_id) == 0) {
            wcsncpy(m->pool[i].name, alias, 63);
            m->pool[i].name[63] = L'\0';
            PushMockSnapshot();
            return 0;
        }
    }
    return -1;
}

static int MockSetAutostart(void *ctx, int enabled) {
    auto *m = static_cast<MockBackend *>(ctx);
    m->autostart = enabled ? 1 : 0;
    return 0;
}

static int MockGetAutostart(void *ctx) {
    auto *m = static_cast<MockBackend *>(ctx);
    return m->autostart;
}

static void MockRequestQuit(void *ctx) {
    (void)ctx;
    PostQuitMessage(0);
}

static void DemoSimulateDrainCharge() {
    g_mock.sim_phase = !g_mock.sim_phase;
    uint64_t now = GetTickCount64();
    if (g_mock.sim_phase) {
        g_mock.pool[0].battery_percent = 41;
        g_mock.pool[0].voltage_mv = 3780;
        g_mock.pool[0].is_low_battery = 0;

        g_mock.pool[1].battery_percent = 100;
        g_mock.pool[1].power_state = OCTANS_PWR_FULL;
        g_mock.pool[1].voltage_mv = 4200;

        g_mock.pool[2].battery_percent = 9;
        g_mock.pool[2].voltage_mv = 3460;
        g_mock.pool[2].is_low_battery = 1;

        g_mock.pool[3].battery_percent = 18;
        g_mock.pool[3].voltage_mv = 3600;
        g_mock.pool[3].is_low_battery = 1;
    } else {
        g_mock.pool[0].battery_percent = 78;
        g_mock.pool[0].voltage_mv = 4050;
        g_mock.pool[0].is_low_battery = 0;

        g_mock.pool[1].battery_percent = 92;
        g_mock.pool[1].power_state = OCTANS_PWR_CHARGING;
        g_mock.pool[1].voltage_mv = 4180;

        g_mock.pool[2].battery_percent = 16;
        g_mock.pool[2].voltage_mv = 3580;
        g_mock.pool[2].is_low_battery = 1;

        g_mock.pool[3].battery_percent = 64;
        g_mock.pool[3].voltage_mv = 3920;
        g_mock.pool[3].is_low_battery = 0;
    }
    for (size_t i = 0; i < 4; ++i) g_mock.pool[i].updated_ms = now;
    PushMockSnapshot();
}

static void DemoToggleCharging() {
    auto &kb = g_mock.pool[1];
    if (kb.power_state == OCTANS_PWR_CHARGING) {
        kb.power_state = OCTANS_PWR_DISCHARGING;
    } else {
        kb.power_state = OCTANS_PWR_CHARGING;
    }
    kb.updated_ms = GetTickCount64();
    PushMockSnapshot();
}

static void DemoToggleTheme() {
    g_mock.is_dark = !g_mock.is_dark;
    octans_ui_on_theme_changed(g_mock.is_dark ? 1 : 0);
}

static void DemoCycleCount() {
    if (g_mock.active_count == 4) g_mock.active_count = 7;
    else if (g_mock.active_count == 7) g_mock.active_count = 2;
    else if (g_mock.active_count == 2) g_mock.active_count = 1;
    else if (g_mock.active_count == 1) g_mock.active_count = 0;
    else g_mock.active_count = 4;
    PushMockSnapshot();
}

static void DemoOnAction(int action_id, void *ctx) {
    (void)ctx;
    switch (action_id) {
        case 1: DemoSimulateDrainCharge(); break;
        case 2: DemoToggleCharging();      break;
        case 3: DemoToggleTheme();         break;
        case 4: DemoCycleCount();          break;
        default: break;
    }
}

static bool DemoHandleKey(WPARAM vk, void *ctx) {
    (void)ctx;
    if (vk == 'D') { DemoSimulateDrainCharge(); return true; }
    if (vk == 'T') { DemoToggleTheme();         return true; }
    if (vk == 'C') { DemoCycleCount();          return true; }
    if (vk == 'R') { ActionTriggerRefresh();    return true; }
    if (vk == 'S') {
        int next_i = (static_cast<int>(GetState().tray_style) + 1) % 3;
        ActionToggleTrayStyle(static_cast<TrayStyle>(next_i));
        return true;
    }
    return false;
}

static int ParseSmokeMs(const wchar_t *cmdline) {
    if (!cmdline) return 0;
    const wchar_t *p = wcsstr(cmdline, L"--smoke");
    if (!p) return 0;
    p += 7;
    while (*p == L' ') ++p;
    int ms = _wtoi(p);
    return (ms > 0) ? ms : 1200;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR lpCmdLine, int) {
    EnablePerMonitorDpiV2();
    FillMockDevices(g_mock);
    g_mock.is_dark = DetectSystemDarkMode();

    octans_backend_ops_t ops = {};
    ops.request_refresh   = MockRequestRefresh;
    ops.set_pinned_device = MockSetPinnedDevice;
    ops.set_device_alias  = MockSetDeviceAlias;
    ops.set_autostart     = MockSetAutostart;
    ops.get_autostart     = MockGetAutostart;
    ops.request_quit      = MockRequestQuit;

    if (octans_ui_init(&ops, &g_mock) != 0) {
        return 1;
    }

    DemoMenuHooks hooks = {};
    hooks.on_action  = DemoOnAction;
    hooks.handle_key = DemoHandleKey;
    hooks.user_ctx   = &g_mock;
    ActionSetDemoHooks(&hooks);

    PushMockSnapshot();

    /* Immediately display the floating panel near the tray icon for instant visual preview. */
    POINT anchor = TrayGetAnchorPoint();
    if (anchor.x == 0 && anchor.y == 0) {
        anchor.x = GetSystemMetrics(SM_CXSCREEN) - 120;
        anchor.y = GetSystemMetrics(SM_CYSCREEN) - 48;
    }
    octans_ui_show_panel(anchor.x, anchor.y);

    int smoke_ms = ParseSmokeMs(lpCmdLine);
    UINT_PTR smoke_timer = 0;
    if (smoke_ms > 0) {
        smoke_timer = SetTimer(nullptr, 0, static_cast<UINT>(smoke_ms), nullptr);
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (smoke_timer != 0 && msg.message == WM_TIMER && msg.hwnd == nullptr && msg.wParam == smoke_timer) {
            KillTimer(nullptr, smoke_timer);
            octans_ui_hide_panel();
            PostQuitMessage(0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    octans_ui_shutdown();
    return 0;
}
