#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <mmsystem.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../include/octans_version.h"
#include "../include/octans_core.h"
#include "../core/hid_enum.h"
#include "../engine/broad_scan.h"
#include "../engine/user_rules.h"
#include "../ui/ui_internal.h"
#include "../ui/ui_theme.h"
#include "../ui/ui_fonts.h"
#include "../ui/ui_brand.h"
#include "../ui/ui_glyphs.h"
#include "usb_etw_learn.h"

namespace {

using namespace octans_ui;

static constexpr float kWinW = 580.0f;
static constexpr float kWinH = 548.0f;
static constexpr float kPadX = 22.0f;
static constexpr float kHeaderH = 52.0f;
static constexpr float kStepperH = 44.0f;
static constexpr float kFooterH = 62.0f;

static constexpr UINT_PTR TIMER_ANIM  = 1;
static constexpr UINT_PTR TIMER_SMOKE = 2;

static constexpr UINT WM_APP_SCAN_DONE   = WM_APP + 10;
static constexpr UINT WM_APP_VERIFY_DONE = WM_APP + 11;
static constexpr UINT WM_APP_REFINE_DONE = WM_APP + 12;

enum WizardStep {
    STEP_SELECT = 0,
    STEP_LEARN  = 1,
    STEP_VERIFY = 2
};

enum HitId {
    HIT_NONE = 0,
    HIT_CLOSE,
    HIT_RESCAN,
    HIT_OPEN_DIR,
    HIT_PRIMARY,
    HIT_BACK,
    HIT_INPUT_PCT,
    HIT_REFINE_INPUT,
    HIT_REFINE_CHG,
    HIT_REFINE_BTN,
    HIT_CARD_BASE = 100
};

struct DeviceGroup {
    uint16_t vid;
    uint16_t pid;
    uint32_t vendor_iface_mask;
    octans_device_kind_t kind;
    wchar_t name[64];
    int live_pct;          /* >= 0 if live engine read valid battery, else -1 */
    uint8_t live_pwr;
    int is_system_controller;
};

struct WizardState {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    bool is_dark = false;
    bool backdrop_ok = false;

    /* D3D11 / DXGI / DComp / D2D / DWrite */
    ComPtr<ID3D11Device> d3d_dev;
    ComPtr<IDXGIDevice> dxgi_dev;
    ComPtr<IDXGISwapChain1> swap_chain;
    ComPtr<IDCompositionDevice> dcomp_dev;
    ComPtr<IDCompositionTarget> dcomp_target;
    ComPtr<IDCompositionVisual> dcomp_visual;
    ComPtr<ID2D1Factory1> d2d_factory;
    ComPtr<ID2D1Device> d2d_dev;
    ComPtr<ID2D1DeviceContext> d2d_ctx;
    ComPtr<ID2D1Bitmap1> d2d_bmp;
    ComPtr<ID2D1StrokeStyle> round_stroke;
    ComPtr<IDWriteFactory> dwrite;

    ComPtr<IDWriteTextFormat> fmt_sub;
    ComPtr<IDWriteTextFormat> fmt_step;
    ComPtr<IDWriteTextFormat> fmt_card_title;
    ComPtr<IDWriteTextFormat> fmt_card_sub;
    ComPtr<IDWriteTextFormat> fmt_pill;
    ComPtr<IDWriteTextFormat> fmt_body;
    ComPtr<IDWriteTextFormat> fmt_btn;
    ComPtr<IDWriteTextFormat> fmt_digits;
    ComPtr<IDWriteTextFormat> fmt_digits_lg;
    ComPtr<IDWriteTextFormat> fmt_hero_title;

    UINT swap_w = 0;
    UINT swap_h = 0;

    /* Animation timing */
    float anim_phase = 0.0f;
    float brand_t = 0.0f;
    uint64_t last_tick_ms = 0;

    /* Wizard logical state */
    WizardStep step = STEP_SELECT;
    bool scanning = false;
    bool verifying = false;
    bool refining = false;
    bool saved_done = false;
    int auto_found_count = 0;

    octans_hid_info_t all_ifaces[OCTANS_MAX_HID_IFACES];
    size_t all_iface_count = 0;

    DeviceGroup groups[32];
    size_t group_count = 0;
    int selected_group = -1;
    float list_scroll = 0.0f;
    float list_max_scroll = 0.0f;

    /* Step 2: Learning target & live ETW session */
    uint16_t target_vid = 0;
    uint16_t target_pid = 0;
    uint32_t target_iface_mask = 0;
    octans_device_kind_t target_kind = OCTANS_DEV_MOUSE;
    wchar_t target_name[64] = {};

    octans_etw_session_t *etw = nullptr;
    DWORD etw_start_err = 0;
    uint32_t live_tx = 0;
    uint32_t live_rx = 0;
    int live_dev_seen = 0;

    wchar_t pct_buf[8] = {};
    int pct_len = 0;
    bool pct_focused = true;

    /* Step 3: Captured packets, inferred & verified candidates */
    octans_usb_pkt_t *captured_pkts = nullptr;
    size_t captured_count = 0;

    octans_learn_candidate_t cands[OCTANS_ETW_MAX_CANDS];
    size_t cand_count = 0;
    size_t verified_count = 0;
    int best_idx = -1;
    int ambiguous_count = 0;
    int expected_pct = 0;

    /* Optional refine controls on Step 3 */
    wchar_t refine_pct_buf[8] = {};
    int refine_pct_len = 0;
    bool refine_focused = false;
    bool refine_charging = false;
    wchar_t refine_msg[160] = {};

    wchar_t banner_msg[256] = {};
    wchar_t toast_msg[200] = {};
    uint64_t toast_until_ms = 0;

    /* Hit test geometry */
    int hover_hit = HIT_NONE;
    D2D1_RECT_F rc_close = {};
    D2D1_RECT_F rc_rescan = {};
    D2D1_RECT_F rc_open_dir = {};
    D2D1_RECT_F rc_primary = {};
    D2D1_RECT_F rc_back = {};
    D2D1_RECT_F rc_list_clip = {};
    D2D1_RECT_F rc_cards[32] = {};
    D2D1_RECT_F rc_input_pct = {};
    D2D1_RECT_F rc_refine_input = {};
    D2D1_RECT_F rc_refine_chg = {};
    D2D1_RECT_F rc_refine_btn = {};

    /* Startup CLI args */
    bool launch_learn = false;
    int smoke_ms = 0;
};

static WizardState g_w;

static bool PtInRectF(const D2D1_RECT_F &r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static void GetDevicesIniPath(wchar_t *out, size_t cap) {
    wchar_t appdata[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0) {
        _snwprintf(out, cap - 1, L"%ls\\Octans\\devices.ini", appdata);
    } else {
        wcsncpy(out, L"devices.ini", cap - 1);
    }
    out[cap - 1] = L'\0';
}

static void NotifyOctansReload() {
    UINT msg = RegisterWindowMessageW(L"OctansReloadRulesMsg");
    if (msg != 0) {
        PostMessageW(HWND_BROADCAST, msg, 0, 0);
    }
}

static void ShowToast(const wchar_t *msg, uint64_t dur_ms = 3200) {
    wcsncpy(g_w.toast_msg, msg ? msg : L"", 199);
    g_w.toast_msg[199] = L'\0';
    g_w.toast_until_ms = GetTickCount64() + dur_ms;
}

static bool ContainsNoCase(const wchar_t *hay, const wchar_t *needle) {
    if (!hay || !needle || !needle[0]) return false;
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = hay; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) return true;
    }
    return false;
}

static octans_device_kind_t GuessDeviceKind(uint16_t vid,
                                            uint16_t pid,
                                            const wchar_t *prod,
                                            const octans_hid_info_t *all,
                                            size_t count) {
    if (prod && prod[0]) {
        if (ContainsNoCase(prod, L"headset") || ContainsNoCase(prod, L"audio") ||
            ContainsNoCase(prod, L"ear") || ContainsNoCase(prod, L"v9") ||
            ContainsNoCase(prod, L"cloud") || ContainsNoCase(prod, L"barracuda")) {
            return OCTANS_DEV_HEADSET;
        }
        if (ContainsNoCase(prod, L"keyboard") || ContainsNoCase(prod, L"kbd") ||
            ContainsNoCase(prod, L"evo80") || ContainsNoCase(prod, L"evo75") ||
            ContainsNoCase(prod, L"keychron") || ContainsNoCase(prod, L"ace")) {
            return OCTANS_DEV_KEYBOARD;
        }
        if (ContainsNoCase(prod, L"mouse") || ContainsNoCase(prod, L"superlight") ||
            ContainsNoCase(prod, L"ultra") || ContainsNoCase(prod, L"viper")) {
            return OCTANS_DEV_MOUSE;
        }
    }
    bool has_mouse = false, has_kb = false, has_audio = false;
    for (size_t i = 0; i < count; ++i) {
        if (all[i].vid != vid || all[i].pid != pid) continue;
        if (all[i].usage_page == 0x0001u && all[i].usage == 0x0002u) has_mouse = true;
        if (all[i].usage_page == 0x0001u && all[i].usage == 0x0006u) has_kb = true;
        if (all[i].usage_page == 0x000Bu || all[i].usage_page == 0x000Cu) has_audio = true;
    }
    if (has_mouse && !has_kb) return OCTANS_DEV_MOUSE;
    if (has_kb && !has_mouse) return OCTANS_DEV_KEYBOARD;
    if (has_mouse) return OCTANS_DEV_MOUSE;
    if (has_kb) return OCTANS_DEV_KEYBOARD;
    if (has_audio) return OCTANS_DEV_HEADSET;
    return OCTANS_DEV_MOUSE;
}

static bool IsLikelyBoardController(uint16_t vid, const wchar_t *prod) {
    /* ITE motherboard RGB/EC controllers (0x048D) or ASUS Aura motherboard LEDs */
    if (vid == 0x048Du) return true;
    if (prod && ContainsNoCase(prod, L"ITE Device")) return true;
    return false;
}

static const wchar_t *KindLabel(octans_device_kind_t k) {
    switch (k) {
    case OCTANS_DEV_MOUSE:    return L"无线鼠标";
    case OCTANS_DEV_KEYBOARD: return L"无线键盘";
    case OCTANS_DEV_HEADSET:  return L"无线耳机";
    case OCTANS_DEV_EARBUDS:  return L"无线耳塞";
    case OCTANS_DEV_GAMEPAD:  return L"无线手柄";
    default:                  return L"无线外设";
    }
}

/* ============================================================================
 * Worker Threads (Full Init Scan / Verify / Second-Sample Refine)
 * ============================================================================ */

struct ScanResult {
    octans_hid_info_t all_ifaces[OCTANS_MAX_HID_IFACES];
    size_t all_iface_count;
    DeviceGroup groups[32];
    size_t group_count;
    int auto_found;
};

static DWORD WINAPI ScanWorkerThread(LPVOID) {
    ScanResult *res = (ScanResult *)calloc(1, sizeof(ScanResult));
    if (!res) return 0;

    /* 1. Count existing auto rules before poll */
    wchar_t ini_path[MAX_PATH];
    GetDevicesIniPath(ini_path, MAX_PATH);
    octans_user_rule_t before_rules[OCTANS_MAX_USER_RULES];
    size_t before_cnt = octans_user_rules_load_file(ini_path, before_rules, OCTANS_MAX_USER_RULES);

    /* 2. Run full Octans engine initialization scan (built-in + user rules + broad discovery) */
    octans_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.poll_interval_ms = 15000u;
    cfg.passive_listen_ms = 250u;
    octans_engine_t *eng = octans_engine_create(&cfg);
    octans_device_view_t views[OCTANS_MAX_DEVICES];
    size_t vcount = 0;
    if (eng) {
        octans_engine_poll(eng, 1);
        vcount = octans_engine_get_devices(eng, views, OCTANS_MAX_DEVICES);
        octans_engine_destroy(eng);
    }

    octans_user_rule_t after_rules[OCTANS_MAX_USER_RULES];
    size_t after_cnt = octans_user_rules_load_file(ini_path, after_rules, OCTANS_MAX_USER_RULES);
    if (after_cnt > before_cnt) {
        res->auto_found = (int)(after_cnt - before_cnt);
        NotifyOctansReload();
    }

    /* 3. Enumerate all connected HID interfaces and group Vendor HIDs cleanly by VID:PID */
    res->all_iface_count = octans_hid_enumerate(res->all_ifaces, OCTANS_MAX_HID_IFACES);

    for (size_t i = 0; i < res->all_iface_count; ++i) {
        const octans_hid_info_t *it = &res->all_ifaces[i];
        if (it->vid == 0u || it->is_system_exclusive || it->usage_page < 0xFF00u) continue;

        /* Find or create DeviceGroup for (vid, pid) */
        DeviceGroup *dg = nullptr;
        for (size_t g = 0; g < res->group_count; ++g) {
            if (res->groups[g].vid == it->vid && res->groups[g].pid == it->pid) {
                dg = &res->groups[g];
                break;
            }
        }
        if (!dg) {
            if (res->group_count >= 32u) continue;
            dg = &res->groups[res->group_count++];
            memset(dg, 0, sizeof(*dg));
            dg->vid = it->vid;
            dg->pid = it->pid;
            dg->live_pct = -1;
            if (it->product[0]) {
                wcsncpy(dg->name, it->product, 63);
                dg->name[63] = L'\0';
            } else {
                _snwprintf(dg->name, 63, L"无线设备 %04X:%04X", (unsigned)it->vid, (unsigned)it->pid);
            }
            dg->kind = GuessDeviceKind(it->vid, it->pid, dg->name, res->all_ifaces, res->all_iface_count);
            dg->is_system_controller = IsLikelyBoardController(it->vid, dg->name) ? 1 : 0;
        }
        if (it->interface_num >= 0 && it->interface_num < 32) {
            dg->vendor_iface_mask |= (1u << (unsigned)it->interface_num);
        }
        if (dg->name[0] == L'\0' && it->product[0]) {
            wcsncpy(dg->name, it->product, 63);
            dg->name[63] = L'\0';
        }
    }

    /* Attach live battery results from engine poll */
    for (size_t g = 0; g < res->group_count; ++g) {
        for (size_t v = 0; v < vcount; ++v) {
            if (views[v].vid == res->groups[g].vid &&
                (views[v].pid == res->groups[g].pid || views[v].pid == 0)) {
                if (views[v].battery_percent >= 0) {
                    res->groups[g].live_pct = views[v].battery_percent;
                    res->groups[g].live_pwr = views[v].power_state;
                    res->groups[g].kind = (octans_device_kind_t)views[v].kind;
                    if (views[v].name[0]) {
                        wcsncpy(res->groups[g].name, views[v].name, 63);
                        res->groups[g].name[63] = L'\0';
                    }
                }
                break;
            }
        }
    }

    /* Sort: unrecognized real peripherals first, then recognized peripherals, then motherboard controllers */
    for (size_t i = 0; i < res->group_count; ++i) {
        for (size_t j = i + 1; j < res->group_count; ++j) {
            auto rank = [](const DeviceGroup &d) -> int {
                if (d.is_system_controller) return 3;
                if (d.live_pct < 0) return 0;
                return 1;
            };
            if (rank(res->groups[j]) < rank(res->groups[i])) {
                DeviceGroup tmp = res->groups[i];
                res->groups[i] = res->groups[j];
                res->groups[j] = tmp;
            }
        }
    }

    PostMessageW(g_w.hwnd, WM_APP_SCAN_DONE, 0, (LPARAM)res);
    return 0;
}

static void StartFullScan() {
    if (g_w.scanning) return;
    g_w.scanning = true;
    wcsncpy(g_w.banner_msg, L"正在执行全面初始化扫描，自动探测所有 2.4G 无线设备...", 255);
    HANDLE h = CreateThread(nullptr, 0, ScanWorkerThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    InvalidateRect(g_w.hwnd, nullptr, FALSE);
}

struct VerifyJob {
    octans_usb_pkt_t *pkts;
    size_t pkt_count;
    uint16_t vid;
    uint16_t pid;
    octans_device_kind_t kind;
    wchar_t name[64];
    int expected_pct;
};

struct VerifyResult {
    octans_learn_candidate_t cands[OCTANS_ETW_MAX_CANDS];
    size_t cand_count;
    size_t verified_count;
    int best_idx;
    int ambiguous_count;
    octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
    size_t iface_count;
};

static DWORD WINAPI VerifyWorkerThread(LPVOID param) {
    VerifyJob *job = (VerifyJob *)param;
    VerifyResult *res = (VerifyResult *)calloc(1, sizeof(VerifyResult));
    if (job && res) {
        res->iface_count = octans_hid_enumerate(res->ifaces, OCTANS_MAX_HID_IFACES);
        res->cand_count = octans_etw_infer_candidates(
            job->pkts, job->pkt_count, job->vid, job->pid, job->kind, job->name,
            res->ifaces, res->iface_count, job->expected_pct,
            res->cands, OCTANS_ETW_MAX_CANDS);
        if (res->cand_count > 0) {
            res->verified_count = octans_etw_verify_candidates(
                res->cands, res->cand_count, res->ifaces, res->iface_count,
                job->expected_pct, &res->best_idx, &res->ambiguous_count);
        }
    }
    if (job) {
        free(job->pkts);
        free(job);
    }
    PostMessageW(g_w.hwnd, WM_APP_VERIFY_DONE, 0, (LPARAM)res);
    return 0;
}

struct RefineResult {
    int remaining;
    int best_idx;
};

static DWORD WINAPI RefineWorkerThread(LPVOID param) {
    int packed = (int)(intptr_t)param;
    int new_pct = packed & 0xFF;
    int is_chg = (packed >> 8) & 1;
    RefineResult *rr = (RefineResult *)calloc(1, sizeof(RefineResult));
    if (rr) {
        octans_hid_info_t ifaces[OCTANS_MAX_HID_IFACES];
        size_t icount = octans_hid_enumerate(ifaces, OCTANS_MAX_HID_IFACES);
        rr->remaining = octans_etw_refine_with_second_sample(
            g_w.cands, g_w.cand_count, ifaces, icount,
            new_pct, is_chg, &rr->best_idx);
    }
    PostMessageW(g_w.hwnd, WM_APP_REFINE_DONE, 0, (LPARAM)rr);
    return 0;
}

/* ============================================================================
 * Step Transitions & Elevation
 * ============================================================================ */

static void StopEtwIfRunning(bool keep_packets) {
    if (!g_w.etw) return;
    if (keep_packets) {
        free(g_w.captured_pkts);
        g_w.captured_pkts = (octans_usb_pkt_t *)calloc(OCTANS_ETW_MAX_PKTS, sizeof(octans_usb_pkt_t));
        g_w.captured_count = g_w.captured_pkts
            ? octans_etw_stop_and_copy(g_w.etw, g_w.captured_pkts, OCTANS_ETW_MAX_PKTS)
            : octans_etw_stop_and_copy(g_w.etw, nullptr, 0);
    } else {
        octans_etw_stop_and_copy(g_w.etw, nullptr, 0);
    }
    g_w.etw = nullptr;
}

static void BeginStep2Learn() {
    g_w.step = STEP_LEARN;
    g_w.pct_len = 0;
    g_w.pct_buf[0] = L'\0';
    g_w.pct_focused = true;
    g_w.live_tx = 0;
    g_w.live_rx = 0;
    g_w.live_dev_seen = 0;
    g_w.etw_start_err = 0;
    g_w.saved_done = false;

    /* Compute vendor interface mask if not already populated */
    if (g_w.target_iface_mask == 0) {
        octans_hid_info_t tmp[OCTANS_MAX_HID_IFACES];
        size_t n = octans_hid_enumerate(tmp, OCTANS_MAX_HID_IFACES);
        for (size_t i = 0; i < n; ++i) {
            if (tmp[i].vid == g_w.target_vid && tmp[i].pid == g_w.target_pid &&
                tmp[i].usage_page >= 0xFF00u &&
                tmp[i].interface_num >= 0 && tmp[i].interface_num < 32) {
                g_w.target_iface_mask |= (1u << (unsigned)tmp[i].interface_num);
            }
        }
    }

    StopEtwIfRunning(false);
    DWORD err = 0;
    g_w.etw = octans_etw_start(g_w.target_vid, g_w.target_pid, g_w.target_iface_mask, &err);
    g_w.etw_start_err = err;
    InvalidateRect(g_w.hwnd, nullptr, FALSE);
}

static void LaunchLearnForSelected() {
    if (g_w.selected_group < 0 || (size_t)g_w.selected_group >= g_w.group_count) {
        ShowToast(L"请先点选一台需要学习的设备卡片");
        return;
    }
    const DeviceGroup &dg = g_w.groups[g_w.selected_group];
    g_w.target_vid = dg.vid;
    g_w.target_pid = dg.pid;
    g_w.target_iface_mask = dg.vendor_iface_mask;
    g_w.target_kind = dg.kind;
    wcsncpy(g_w.target_name, dg.name, 63);
    g_w.target_name[63] = L'\0';

    if (octans_etw_is_elevated()) {
        BeginStep2Learn();
        return;
    }

    /* Relaunch elevated directly into Step 2 for this device */
    wchar_t self_path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    wchar_t params[512];
    _snwprintf(params, 511, L"--learn %04X %04X %d \"%ls\"",
               (unsigned)dg.vid, (unsigned)dg.pid, (int)dg.kind, dg.name);
    params[511] = L'\0';

    HINSTANCE res = ShellExecuteW(g_w.hwnd, L"runas", self_path, params, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)res > 32) {
        PostQuitMessage(0);
    } else {
        ShowToast(L"需要临时管理员权限才能读取系统 USB 通信，请点“是”授权");
    }
}

static void BeginStep3Verify() {
    int pct = _wtoi(g_w.pct_buf);
    if (pct < 1 || pct > 100) {
        ShowToast(L"请输入 1 到 100 之间的电量百分比整数");
        g_w.pct_focused = true;
        return;
    }

    StopEtwIfRunning(true);
    g_w.step = STEP_VERIFY;
    g_w.verifying = true;
    g_w.expected_pct = pct;
    g_w.cand_count = 0;
    g_w.verified_count = 0;
    g_w.best_idx = -1;
    g_w.ambiguous_count = 0;
    g_w.refine_pct_len = 0;
    g_w.refine_pct_buf[0] = L'\0';
    g_w.refine_msg[0] = L'\0';
    g_w.saved_done = false;

    VerifyJob *job = (VerifyJob *)calloc(1, sizeof(VerifyJob));
    if (!job) {
        g_w.verifying = false;
        return;
    }
    job->pkt_count = g_w.captured_count;
    if (g_w.captured_count > 0 && g_w.captured_pkts) {
        job->pkts = (octans_usb_pkt_t *)malloc(g_w.captured_count * sizeof(octans_usb_pkt_t));
        if (job->pkts) {
            memcpy(job->pkts, g_w.captured_pkts, g_w.captured_count * sizeof(octans_usb_pkt_t));
        } else {
            job->pkt_count = 0;
        }
    }
    job->vid = g_w.target_vid;
    job->pid = g_w.target_pid;
    job->kind = g_w.target_kind;
    wcsncpy(job->name, g_w.target_name, 63);
    job->expected_pct = pct;

    HANDLE h = CreateThread(nullptr, 0, VerifyWorkerThread, job, 0, nullptr);
    if (h) CloseHandle(h);
    InvalidateRect(g_w.hwnd, nullptr, FALSE);
}

static void SaveVerifiedAndFinish() {
    if (g_w.best_idx < 0 || (size_t)g_w.best_idx >= g_w.cand_count) return;
    wchar_t ini_text[1024];
    if (octans_user_rule_format_ini(&g_w.cands[g_w.best_idx].rule, ini_text, 1024) != 0) {
        ShowToast(L"生成设备规则失败");
        return;
    }
    wchar_t ini_path[MAX_PATH];
    GetDevicesIniPath(ini_path, MAX_PATH);
    if (octans_user_rules_save_section(ini_path, ini_text) == 0) {
        NotifyOctansReload();
        g_w.saved_done = true;
        ShowToast(L"已保存并热加载到 Octans！托盘面板将立即显示该设备", 2600);
        InvalidateRect(g_w.hwnd, nullptr, FALSE);
    } else {
        ShowToast(L"写入配置文件失败，请检查 %APPDATA%\\Octans 权限");
    }
}

/* ============================================================================
 * Direct2D / DComp Graphics Setup & Drawing
 * ============================================================================ */

static void ReleaseRenderTarget() {
    if (g_w.d2d_ctx) g_w.d2d_ctx->SetTarget(nullptr);
    g_w.d2d_bmp.Reset();
    g_w.swap_chain.Reset();
}

static bool EnsureGraphics() {
    if (!g_w.d2d_factory) {
        D2D1_FACTORY_OPTIONS opts = {};
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     __uuidof(ID2D1Factory1), &opts,
                                     reinterpret_cast<void **>(g_w.d2d_factory.Put())))) {
            return false;
        }
        D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        g_w.d2d_factory->CreateStrokeStyle(&sp, nullptr, 0, g_w.round_stroke.Put());
    }

    if (!g_w.dwrite) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown **>(g_w.dwrite.Put())))) {
            return false;
        }
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_NORMAL, 12.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_sub);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 12.0f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_step);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_STRETCH_NORMAL, true, g_w.fmt_card_title);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_NORMAL, 11.5f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_STRETCH_NORMAL, true, g_w.fmt_card_sub);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.5f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_pill);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_NORMAL, 13.0f, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_body);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_btn);
        CreateOctansDigitFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_BOLD, 28.0f, g_w.fmt_digits);
        CreateOctansDigitFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_BOLD, 22.0f, g_w.fmt_digits_lg);
        CreateOctansTextFormat(g_w.dwrite.Get(), DWRITE_FONT_WEIGHT_BOLD, 17.5f, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_STRETCH_NORMAL, false, g_w.fmt_hero_title);
    }

    if (!g_w.d3d_dev) {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                       fls, 3, D3D11_SDK_VERSION, g_w.d3d_dev.Put(), nullptr, nullptr);
        if (FAILED(hr)) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                   fls, 3, D3D11_SDK_VERSION, g_w.d3d_dev.Put(), nullptr, nullptr);
        }
        if (FAILED(hr)) return false;
        if (FAILED(g_w.d3d_dev.Get()->QueryInterface(__uuidof(IDXGIDevice),
                                                    reinterpret_cast<void **>(g_w.dxgi_dev.Put())))) return false;
        if (FAILED(g_w.d2d_factory->CreateDevice(g_w.dxgi_dev.Get(), g_w.d2d_dev.Put()))) return false;
        if (FAILED(g_w.d2d_dev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, g_w.d2d_ctx.Put()))) return false;
        g_w.d2d_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        if (FAILED(DCompositionCreateDevice(g_w.dxgi_dev.Get(), __uuidof(IDCompositionDevice),
                                            reinterpret_cast<void **>(g_w.dcomp_dev.Put())))) return false;
        if (FAILED(g_w.dcomp_dev->CreateTargetForHwnd(g_w.hwnd, TRUE, g_w.dcomp_target.Put()))) return false;
        if (FAILED(g_w.dcomp_dev->CreateVisual(g_w.dcomp_visual.Put()))) return false;
        g_w.dcomp_target->SetRoot(g_w.dcomp_visual.Get());
    }

    RECT cr = {};
    GetClientRect(g_w.hwnd, &cr);
    UINT pw = (UINT)(cr.right > 0 ? cr.right : 1);
    UINT ph = (UINT)(cr.bottom > 0 ? cr.bottom : 1);

    if (!g_w.swap_chain || g_w.swap_w != pw || g_w.swap_h != ph) {
        ReleaseRenderTarget();
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> dxgi_fac;
        if (FAILED(g_w.dxgi_dev->GetAdapter(adapter.Put()))) return false;
        if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(dxgi_fac.Put())))) return false;

        DXGI_SWAP_CHAIN_DESC1 scd = {};
        scd.Width = pw;
        scd.Height = ph;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.Scaling = DXGI_SCALING_STRETCH;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(dxgi_fac->CreateSwapChainForComposition(g_w.dxgi_dev.Get(), &scd, nullptr, g_w.swap_chain.Put()))) return false;

        ComPtr<IDXGISurface> surf;
        if (FAILED(g_w.swap_chain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void **>(surf.Put())))) return false;
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            (float)g_w.dpi, (float)g_w.dpi);
        if (FAILED(g_w.d2d_ctx->CreateBitmapFromDxgiSurface(surf.Get(), &bp, g_w.d2d_bmp.Put()))) return false;
        g_w.d2d_ctx->SetTarget(g_w.d2d_bmp.Get());
        g_w.d2d_ctx->SetDpi((float)g_w.dpi, (float)g_w.dpi);
        g_w.dcomp_visual->SetContent(g_w.swap_chain.Get());
        g_w.dcomp_dev->Commit();
        g_w.swap_w = pw;
        g_w.swap_h = ph;
    }

    return true;
}

static void FillRoundRect(ID2D1DeviceContext *ctx, const D2D1_RECT_F &r, float rad, D2D1_COLOR_F col) {
    ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(col, br.Put()))) {
        ctx->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get());
    }
}

static void DrawRoundRect(ID2D1DeviceContext *ctx, const D2D1_RECT_F &r, float rad, D2D1_COLOR_F col, float sw = 1.0f) {
    ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(col, br.Put()))) {
        ctx->DrawRoundedRectangle(D2D1::RoundedRect(r, rad, rad), br.Get(), sw);
    }
}

static void DrawTextStr(ID2D1DeviceContext *ctx, IDWriteTextFormat *fmt,
                        const wchar_t *txt, const D2D1_RECT_F &r, D2D1_COLOR_F col) {
    if (!fmt || !txt || !txt[0]) return;
    ComPtr<ID2D1SolidColorBrush> br;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(col, br.Put()))) {
        ctx->DrawTextW(txt, (UINT32)wcslen(txt), fmt, r, br.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

static void DrawButton(ID2D1DeviceContext *ctx, const ThemePalette &pal,
                       const D2D1_RECT_F &r, const wchar_t *label,
                       bool primary, bool enabled, bool hovered) {
    float rad = (r.bottom - r.top) * 0.5f;
    D2D1_COLOR_F bg, fg, bd;
    if (primary) {
        if (!enabled) {
            bg = pal.is_dark ? MakeColor(1, 1, 1, 0.08f) : MakeColor(0, 0, 0, 0.07f);
            fg = pal.text_muted;
            bd = MakeColor(0, 0, 0, 0);
        } else {
            bg = pal.ring_normal;
            if (hovered) {
                bg.r = (bg.r * 0.92f);
                bg.g = (bg.g * 0.92f);
                bg.b = (bg.b * 0.92f);
            }
            fg = MakeColor(1.0f, 1.0f, 1.0f, 0.98f);
            bd = MakeColor(1, 1, 1, 0.14f);
        }
    } else {
        bg = hovered ? pal.card_bg_hover : pal.card_bg;
        fg = enabled ? pal.text_primary : pal.text_muted;
        bd = pal.card_border;
    }
    FillRoundRect(ctx, r, rad, bg);
    if (bd.a > 0.001f) DrawRoundRect(ctx, r, rad, bd, 1.0f);
    DrawTextStr(ctx, g_w.fmt_btn.Get(), label, r, fg);
}

static void RenderWizard() {
    if (!EnsureGraphics()) return;
    auto *ctx = g_w.d2d_ctx.Get();
    ThemePalette pal = GetThemePalette(g_w.is_dark);

    g_w.rc_close = g_w.rc_rescan = g_w.rc_open_dir = g_w.rc_primary = g_w.rc_back = {};
    g_w.rc_input_pct = g_w.rc_refine_input = g_w.rc_refine_chg = g_w.rc_refine_btn = {};
    for (int i = 0; i < 32; ++i) g_w.rc_cards[i] = {};

    ctx->BeginDraw();
    ctx->Clear(D2D1::ColorF(0, 0, 0, 0));

    /* Background frosted panel */
    D2D1_RECT_F full = D2D1::RectF(0.5f, 0.5f, kWinW - 0.5f, kWinH - 0.5f);
    FillRoundRect(ctx, full, 10.0f, g_w.backdrop_ok ? pal.panel_tint : pal.panel_fallback_bg);
    DrawRoundRect(ctx, full, 10.0f, pal.panel_outer_border, 1.0f);

    /* ---- Header ---- */
    D2D1_RECT_F wm_rc = D2D1::RectF(kPadX, 14.0f, kPadX + 94.0f, 38.0f);
    DrawOctansWordmark(g_w.d2d_factory.Get(), ctx, wm_rc, g_w.is_dark, g_w.brand_t, 1.0f);

    /* Subtle divider + title */
    {
        ComPtr<ID2D1SolidColorBrush> br;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(pal.card_border, br.Put()))) {
            ctx->DrawLine(D2D1::Point2F(kPadX + 104.0f, 18.0f), D2D1::Point2F(kPadX + 104.0f, 34.0f), br.Get(), 1.0f);
        }
    }
    DrawTextStr(ctx, g_w.fmt_sub.Get(), L"无线设备发现与学习",
                D2D1::RectF(kPadX + 114.0f, 16.0f, 380.0f, 36.0f), pal.text_secondary);

    /* Close button */
    g_w.rc_close = D2D1::RectF(kWinW - 44.0f, 12.0f, kWinW - 16.0f, 40.0f);
    if (g_w.hover_hit == HIT_CLOSE) {
        FillRoundRect(ctx, g_w.rc_close, 14.0f, pal.btn_hover_bg);
    }
    {
        ComPtr<ID2D1SolidColorBrush> br;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(
                g_w.hover_hit == HIT_CLOSE ? pal.btn_icon_hover : pal.btn_icon, br.Put()))) {
            float cx = (g_w.rc_close.left + g_w.rc_close.right) * 0.5f;
            float cy = (g_w.rc_close.top + g_w.rc_close.bottom) * 0.5f;
            ctx->DrawLine(D2D1::Point2F(cx - 5.0f, cy - 5.0f), D2D1::Point2F(cx + 5.0f, cy + 5.0f), br.Get(), 1.5f, g_w.round_stroke.Get());
            ctx->DrawLine(D2D1::Point2F(cx + 5.0f, cy - 5.0f), D2D1::Point2F(cx - 5.0f, cy + 5.0f), br.Get(), 1.5f, g_w.round_stroke.Get());
        }
    }

    /* ---- 3-Step Horizontal Stepper ---- */
    float sy = kHeaderH + 4.0f;
    const wchar_t *step_names[3] = { L"1  全面扫描与选设备", L"2  跟着官方驱动学", L"3  真机验证并启用" };
    float pill_w = (kWinW - kPadX * 2.0f - 20.0f) / 3.0f;
    for (int s = 0; s < 3; ++s) {
        float x0 = kPadX + s * (pill_w + 10.0f);
        D2D1_RECT_F sr = D2D1::RectF(x0, sy, x0 + pill_w, sy + 30.0f);
        bool active = ((int)g_w.step == s);
        bool done = ((int)g_w.step > s);
        D2D1_COLOR_F bg = active
            ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pal.is_dark ? 0.22f : 0.14f)
            : pal.card_bg;
        D2D1_COLOR_F bd = active ? pal.card_border_pinned : pal.card_border;
        D2D1_COLOR_F fg = active ? pal.ring_normal : (done ? pal.text_primary : pal.text_muted);
        FillRoundRect(ctx, sr, 15.0f, bg);
        DrawRoundRect(ctx, sr, 15.0f, bd, 1.0f);
        wchar_t lbl[48];
        if (done) _snwprintf(lbl, 47, L"✓ %ls", step_names[s] + 3);
        else wcsncpy(lbl, step_names[s], 47);
        lbl[47] = L'\0';
        DrawTextStr(ctx, g_w.fmt_step.Get(), lbl, sr, fg);
    }

    float body_top = kHeaderH + kStepperH + 8.0f;
    float body_bot = kWinH - kFooterH - 8.0f;

    /* ================================================================
     * STEP 1: Full Initialization Scan & Grouped Device Card Selector
     * ================================================================ */
    if (g_w.step == STEP_SELECT) {
        /* Banner card */
        D2D1_RECT_F brc = D2D1::RectF(kPadX, body_top, kWinW - kPadX, body_top + 48.0f);
        D2D1_COLOR_F bbg = (g_w.auto_found_count > 0)
            ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pal.is_dark ? 0.16f : 0.12f)
            : pal.card_bg;
        FillRoundRect(ctx, brc, 12.0f, bbg);
        DrawRoundRect(ctx, brc, 12.0f, g_w.auto_found_count > 0 ? pal.card_border_pinned : pal.card_border, 1.0f);

        float text_l = brc.left + 14.0f;
        if (g_w.scanning) {
            ComPtr<ID2D1SolidColorBrush> br;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(pal.ring_normal, br.Put()))) {
                DrawRefreshIcon(g_w.d2d_factory.Get(), ctx, br.Get(), g_w.round_stroke.Get(),
                                brc.left + 24.0f, (brc.top + brc.bottom) * 0.5f, 8.0f, g_w.anim_phase * 360.0f);
            }
            text_l = brc.left + 44.0f;
        }
        DrawTextStr(ctx, g_w.fmt_body.Get(), g_w.banner_msg,
                    D2D1::RectF(text_l, brc.top + 6.0f, brc.right - 14.0f, brc.bottom - 6.0f),
                    pal.text_primary);

        /* Scrollable Device Card List */
        g_w.rc_list_clip = D2D1::RectF(kPadX, brc.bottom + 12.0f, kWinW - kPadX, body_bot);
        float card_h = 64.0f;
        float gap = 10.0f;
        float total_h = g_w.group_count > 0 ? (float)g_w.group_count * (card_h + gap) - gap : 0.0f;
        float view_h = g_w.rc_list_clip.bottom - g_w.rc_list_clip.top;
        g_w.list_max_scroll = (total_h > view_h) ? (total_h - view_h) : 0.0f;
        if (g_w.list_scroll > g_w.list_max_scroll) g_w.list_scroll = g_w.list_max_scroll;
        if (g_w.list_scroll < 0.0f) g_w.list_scroll = 0.0f;

        ctx->PushAxisAlignedClip(g_w.rc_list_clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (g_w.group_count == 0 && !g_w.scanning) {
            DrawOctansLogo(g_w.d2d_factory.Get(), ctx,
                           (g_w.rc_list_clip.left + g_w.rc_list_clip.right) * 0.5f,
                           g_w.rc_list_clip.top + view_h * 0.38f, 34.0f, g_w.is_dark, false, 1.0f, 0.72f);
            DrawTextStr(ctx, g_w.fmt_hero_title.Get(), L"未发现可配置的 2.4G 私有接收器",
                        D2D1::RectF(kPadX, g_w.rc_list_clip.top + view_h * 0.58f, kWinW - kPadX, g_w.rc_list_clip.top + view_h * 0.72f),
                        pal.text_secondary);
            DrawTextStr(ctx, g_w.fmt_pill.Get(), L"请确认无线接收器已插入电脑并唤醒设备后，点击左下角“重新全面扫描”",
                        D2D1::RectF(kPadX + 20.0f, g_w.rc_list_clip.top + view_h * 0.72f, kWinW - kPadX - 20.0f, g_w.rc_list_clip.bottom),
                        pal.text_muted);
        } else {
            for (size_t i = 0; i < g_w.group_count && i < 32u; ++i) {
                const DeviceGroup &dg = g_w.groups[i];
                float y0 = g_w.rc_list_clip.top + (float)i * (card_h + gap) - g_w.list_scroll;
                D2D1_RECT_F cr = D2D1::RectF(g_w.rc_list_clip.left, y0, g_w.rc_list_clip.right, y0 + card_h);
                g_w.rc_cards[i] = cr;
                if (cr.bottom < g_w.rc_list_clip.top - 4.0f || cr.top > g_w.rc_list_clip.bottom + 4.0f) continue;

                bool sel = ((int)i == g_w.selected_group);
                bool hov = (g_w.hover_hit == HIT_CARD_BASE + (int)i);
                D2D1_COLOR_F cbg = sel
                    ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pal.is_dark ? 0.14f : 0.09f)
                    : (hov ? pal.card_bg_hover : pal.card_bg);
                D2D1_COLOR_F cbd = sel ? pal.card_border_pinned : pal.card_border;

                FillRoundRect(ctx, cr, 14.0f, cbg);
                DrawRoundRect(ctx, cr, 14.0f, cbd, sel ? 1.6f : 1.0f);

                /* Device vector icon badge */
                D2D1_RECT_F icon_bg = D2D1::RectF(cr.left + 14.0f, cr.top + 12.0f, cr.left + 54.0f, cr.top + 52.0f);
                FillRoundRect(ctx, icon_bg, 20.0f,
                              pal.is_dark ? MakeColor(1, 1, 1, 0.08f) : MakeColor(0, 0, 0, 0.045f));
                {
                    ComPtr<ID2D1SolidColorBrush> br;
                    if (SUCCEEDED(ctx->CreateSolidColorBrush(
                            dg.live_pct >= 0 ? pal.ring_normal : pal.glyph_active, br.Put()))) {
                        DrawDeviceGlyph(g_w.d2d_factory.Get(), ctx, br.Get(), dg.kind,
                                        (icon_bg.left + icon_bg.right) * 0.5f,
                                        (icon_bg.top + icon_bg.bottom) * 0.5f, 12.0f, false);
                    }
                }

                /* Title & subtle subtext */
                DrawTextStr(ctx, g_w.fmt_card_title.Get(), dg.name,
                            D2D1::RectF(cr.left + 68.0f, cr.top + 11.0f, cr.right - 134.0f, cr.top + 34.0f),
                            pal.text_primary);
                wchar_t sub[96];
                _snwprintf(sub, 95, L"%ls  ·  %04X:%04X%ls",
                           dg.is_system_controller ? L"主板/系统控制器" : KindLabel(dg.kind),
                           (unsigned)dg.vid, (unsigned)dg.pid,
                           sel ? L"  ·  已选中" : L"");
                sub[95] = L'\0';
                DrawTextStr(ctx, g_w.fmt_card_sub.Get(), sub,
                            D2D1::RectF(cr.left + 68.0f, cr.top + 35.0f, cr.right - 134.0f, cr.top + 54.0f),
                            pal.text_secondary);

                /* Status pill */
                D2D1_RECT_F prc = D2D1::RectF(cr.right - 122.0f, cr.top + 19.0f, cr.right - 14.0f, cr.top + 45.0f);
                wchar_t ptxt[32];
                D2D1_COLOR_F pbg, pfg;
                if (dg.live_pct >= 0) {
                    _snwprintf(ptxt, 31, L"已识别 · %d%%", dg.live_pct);
                    pbg = MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pal.is_dark ? 0.22f : 0.14f);
                    pfg = pal.ring_normal;
                } else {
                    wcsncpy(ptxt, L"未识别 · 可学习", 31);
                    pbg = pal.is_dark ? MakeColor(1.0f, 0.62f, 0.04f, 0.20f) : MakeColor(1.0f, 0.58f, 0.0f, 0.14f);
                    pfg = pal.is_dark ? HexColor(0xFF9F0A, 1.0f) : HexColor(0xC96F00, 1.0f);
                }
                ptxt[31] = L'\0';
                FillRoundRect(ctx, prc, 13.0f, pbg);
                DrawTextStr(ctx, g_w.fmt_pill.Get(), ptxt, prc, pfg);
            }
        }
        ctx->PopAxisAlignedClip();

        /* Footer buttons */
        float fy = kWinH - kFooterH + 10.0f;
        g_w.rc_rescan = D2D1::RectF(kPadX, fy, kPadX + 126.0f, fy + 38.0f);
        DrawButton(ctx, pal, g_w.rc_rescan, L"重新全面扫描", false, !g_w.scanning, g_w.hover_hit == HIT_RESCAN);

        g_w.rc_open_dir = D2D1::RectF(g_w.rc_rescan.right + 10.0f, fy + 6.0f, g_w.rc_rescan.right + 102.0f, fy + 32.0f);
        DrawTextStr(ctx, g_w.fmt_pill.Get(), L"规则目录", g_w.rc_open_dir,
                    g_w.hover_hit == HIT_OPEN_DIR ? pal.text_primary : pal.text_muted);

        g_w.rc_primary = D2D1::RectF(kWinW - kPadX - 196.0f, fy, kWinW - kPadX, fy + 38.0f);
        bool can_learn = (!g_w.scanning && g_w.selected_group >= 0 && (size_t)g_w.selected_group < g_w.group_count);
        DrawButton(ctx, pal, g_w.rc_primary, L"跟着官方驱动学一次 →", true, can_learn, g_w.hover_hit == HIT_PRIMARY);
    }

    /* ================================================================
     * STEP 2: Live Windows Native USB ETW Capture ("跟着官方驱动学")
     * ================================================================ */
    else if (g_w.step == STEP_LEARN) {
        /* Live capture status card */
        D2D1_RECT_F status_rc = D2D1::RectF(kPadX, body_top, kWinW - kPadX, body_top + 68.0f);
        FillRoundRect(ctx, status_rc, 14.0f, pal.card_bg);
        DrawRoundRect(ctx, status_rc, 14.0f, g_w.etw ? pal.card_border_pinned : pal.ring_low, 1.2f);

        float pulse = 0.55f + 0.45f * sinf(g_w.anim_phase * 6.28318f);
        D2D1_COLOR_F dot_col = g_w.etw
            ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pulse)
            : pal.ring_low;
        {
            ComPtr<ID2D1SolidColorBrush> br;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(dot_col, br.Put()))) {
                ctx->FillEllipse(D2D1::Ellipse(D2D1::Point2F(status_rc.left + 26.0f, status_rc.top + 34.0f), 6.0f, 6.0f), br.Get());
            }
        }

        wchar_t st_title[128];
        if (g_w.etw) {
            _snwprintf(st_title, 127, L"正在监听「%ls」的 2.4G 通信", g_w.target_name);
        } else {
            _snwprintf(st_title, 127, L"USB 监听启动失败（错误码 %lu）", (unsigned long)g_w.etw_start_err);
        }
        st_title[127] = L'\0';
        DrawTextStr(ctx, g_w.fmt_card_title.Get(), st_title,
                    D2D1::RectF(status_rc.left + 44.0f, status_rc.top + 12.0f, status_rc.right - 148.0f, status_rc.top + 36.0f),
                    pal.text_primary);

        wchar_t st_sub[128];
        if (g_w.etw) {
            _snwprintf(st_sub, 127, L"%ls  ·  全程保持 2.4G 无线即可，无需插有线",
                       g_w.live_dev_seen ? L"已锁定 2.4G 接收器" : L"正在连接 2.4G 接收器通道");
        } else {
            wcsncpy(st_sub, L"请返回上一步重新点“跟着官方驱动学一次”并允许管理员授权", 127);
        }
        st_sub[127] = L'\0';
        DrawTextStr(ctx, g_w.fmt_card_sub.Get(), st_sub,
                    D2D1::RectF(status_rc.left + 44.0f, status_rc.top + 38.0f, status_rc.right - 148.0f, status_rc.top + 58.0f),
                    pal.text_secondary);

        /* Live packet count badge */
        uint32_t total_pkts = g_w.live_tx + g_w.live_rx;
        D2D1_RECT_F badge = D2D1::RectF(status_rc.right - 138.0f, status_rc.top + 20.0f, status_rc.right - 14.0f, status_rc.top + 48.0f);
        wchar_t btxt[40];
        if (total_pkts > 0) _snwprintf(btxt, 39, L"已捕获 %u 条", (unsigned)total_pkts);
        else wcsncpy(btxt, L"等待驱动通信...", 39);
        btxt[39] = L'\0';
        FillRoundRect(ctx, badge, 14.0f, total_pkts > 0
                      ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, pal.is_dark ? 0.22f : 0.14f)
                      : (pal.is_dark ? MakeColor(1, 1, 1, 0.08f) : MakeColor(0, 0, 0, 0.05f)));
        DrawTextStr(ctx, g_w.fmt_pill.Get(), btxt, badge, total_pkts > 0 ? pal.ring_normal : pal.text_secondary);

        /* 3-Bullet Plain-Language Guide Card */
        D2D1_RECT_F guide_rc = D2D1::RectF(kPadX, status_rc.bottom + 14.0f, kWinW - kPadX, status_rc.bottom + 150.0f);
        FillRoundRect(ctx, guide_rc, 14.0f, pal.card_bg);
        DrawRoundRect(ctx, guide_rc, 14.0f, pal.card_border, 1.0f);

        const wchar_t *lines[3] = {
            L"①  确保设备已开机唤醒（动一下鼠标 / 敲一下键盘 / 打开耳机开关）。",
            L"②  打开该设备的 官方桌面驱动（或网页驱动），看到电量后等 2 秒钟。",
            L"③  完全关闭官方驱动（含右下角托盘图标），然后在下方填入你看到的电量："
        };
        for (int i = 0; i < 3; ++i) {
            float ly = guide_rc.top + 16.0f + i * 38.0f;
            DrawTextStr(ctx, g_w.fmt_body.Get(), lines[i],
                        D2D1::RectF(guide_rc.left + 18.0f, ly, guide_rc.right - 18.0f, ly + 32.0f),
                        pal.text_primary);
        }

        /* Centered Focused Battery Percentage Pill Input */
        float in_y = guide_rc.bottom + 24.0f;
        DrawTextStr(ctx, g_w.fmt_step.Get(), L"官方驱动刚才显示的电量是",
                    D2D1::RectF(kPadX, in_y, kWinW - kPadX, in_y + 22.0f), pal.text_secondary);

        g_w.rc_input_pct = D2D1::RectF(kWinW * 0.5f - 78.0f, in_y + 30.0f, kWinW * 0.5f + 54.0f, in_y + 84.0f);
        FillRoundRect(ctx, g_w.rc_input_pct, 16.0f, pal.card_bg_hover);
        DrawRoundRect(ctx, g_w.rc_input_pct, 16.0f,
                      g_w.pct_focused ? pal.ring_normal : pal.card_border,
                      g_w.pct_focused ? 2.0f : 1.0f);

        wchar_t disp_pct[16];
        bool caret_on = g_w.pct_focused && (((int)(g_w.anim_phase * 2.0f) % 2) == 0);
        if (g_w.pct_len > 0) {
            _snwprintf(disp_pct, 15, L"%ls%ls", g_w.pct_buf, caret_on ? L"|" : L"");
        } else {
            wcsncpy(disp_pct, caret_on ? L"|" : L"85", 15);
        }
        disp_pct[15] = L'\0';
        DrawTextStr(ctx, g_w.fmt_digits.Get(), disp_pct, g_w.rc_input_pct,
                    g_w.pct_len > 0 ? pal.text_primary : pal.text_muted);

        DrawTextStr(ctx, g_w.fmt_digits.Get(), L"%",
                    D2D1::RectF(g_w.rc_input_pct.right + 6.0f, in_y + 30.0f, g_w.rc_input_pct.right + 44.0f, in_y + 84.0f),
                    pal.text_secondary);

        DrawTextStr(ctx, g_w.fmt_pill.Get(), L"直接用键盘输入 1–100 的整数，按回车即可验证",
                    D2D1::RectF(kPadX, g_w.rc_input_pct.bottom + 10.0f, kWinW - kPadX, g_w.rc_input_pct.bottom + 30.0f),
                    pal.text_muted);

        /* Footer */
        float fy = kWinH - kFooterH + 10.0f;
        g_w.rc_back = D2D1::RectF(kPadX, fy, kPadX + 118.0f, fy + 38.0f);
        DrawButton(ctx, pal, g_w.rc_back, L"← 返回重选", false, true, g_w.hover_hit == HIT_BACK);

        int cur_pct = _wtoi(g_w.pct_buf);
        bool can_go = (cur_pct >= 1 && cur_pct <= 100 && g_w.etw != nullptr);
        g_w.rc_primary = D2D1::RectF(kWinW - kPadX - 236.0f, fy, kWinW - kPadX, fy + 38.0f);
        DrawButton(ctx, pal, g_w.rc_primary, L"我已关掉官方驱动，开始验证 →", true, can_go, g_w.hover_hit == HIT_PRIMARY);
    }

    /* ================================================================
     * STEP 3: Live Independent Replay Verification & One-Click Enable
     * ================================================================ */
    else if (g_w.step == STEP_VERIFY) {
        D2D1_RECT_F hero = D2D1::RectF(kPadX, body_top, kWinW - kPadX, body_bot);
        FillRoundRect(ctx, hero, 16.0f, pal.card_bg);
        DrawRoundRect(ctx, hero, 16.0f,
                      (!g_w.verifying && g_w.best_idx >= 0) ? pal.card_border_pinned : pal.card_border, 1.2f);

        float cx = (hero.left + hero.right) * 0.5f;

        if (g_w.verifying || g_w.refining) {
            ComPtr<ID2D1SolidColorBrush> br;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(pal.ring_normal, br.Put()))) {
                DrawRefreshIcon(g_w.d2d_factory.Get(), ctx, br.Get(), g_w.round_stroke.Get(),
                                cx, hero.top + 112.0f, 22.0f, g_w.anim_phase * 360.0f);
            }
            DrawTextStr(ctx, g_w.fmt_hero_title.Get(),
                        g_w.refining ? L"正在用第二次电量精确核验..." : L"正在脱离官方驱动，向 2.4G 接收器独立验证...",
                        D2D1::RectF(hero.left + 20.0f, hero.top + 162.0f, hero.right - 20.0f, hero.top + 196.0f),
                        pal.text_primary);
            DrawTextStr(ctx, g_w.fmt_pill.Get(), L"只保存真正能独立读出正确电量的协议，绝不瞎猜",
                        D2D1::RectF(hero.left + 24.0f, hero.top + 202.0f, hero.right - 24.0f, hero.top + 228.0f),
                        pal.text_secondary);
        } else if (g_w.best_idx >= 0 && (size_t)g_w.best_idx < g_w.cand_count) {
            const octans_learn_candidate_t &best = g_w.cands[g_w.best_idx];
            /* Signature Octans Battery Ring */
            float rcy = hero.top + 78.0f;
            float rrad = 42.0f;
            {
                ComPtr<ID2D1SolidColorBrush> trk, acc, gly;
                ctx->CreateSolidColorBrush(pal.ring_track, trk.Put());
                ctx->CreateSolidColorBrush(pal.ring_normal, acc.Put());
                ctx->CreateSolidColorBrush(pal.glyph_active, gly.Put());
                if (trk && acc && gly) {
                    DrawBatteryRingArc(g_w.d2d_factory.Get(), ctx, trk.Get(), g_w.round_stroke.Get(),
                                       cx, rcy, rrad, 6.5f, 1.0f, false);
                    DrawBatteryRingArc(g_w.d2d_factory.Get(), ctx, acc.Get(), g_w.round_stroke.Get(),
                                       cx, rcy, rrad, 6.5f, best.live_pct / 100.0f, false);
                    DrawDeviceGlyph(g_w.d2d_factory.Get(), ctx, gly.Get(), g_w.target_kind,
                                    cx, rcy, 15.0f, false);
                }
            }

            wchar_t pct_str[32];
            _snwprintf(pct_str, 31, L"%d%%", best.live_pct);
            pct_str[31] = L'\0';
            DrawTextStr(ctx, g_w.fmt_digits_lg.Get(), pct_str,
                        D2D1::RectF(hero.left + 20.0f, rcy + 50.0f, hero.right - 20.0f, rcy + 82.0f),
                        pal.ring_normal);

            wchar_t ok_title[128];
            _snwprintf(ok_title, 127, g_w.saved_done ? L"「%ls」已成功启用！" : L"验证成功！已独立读出「%ls」",
                       g_w.target_name);
            ok_title[127] = L'\0';
            DrawTextStr(ctx, g_w.fmt_hero_title.Get(), ok_title,
                        D2D1::RectF(hero.left + 20.0f, rcy + 84.0f, hero.right - 20.0f, rcy + 114.0f),
                        pal.text_primary);

            const wchar_t *sub_msg = g_w.refine_msg[0] ? g_w.refine_msg
                : (best.rule.chg_offset >= 0
                    ? L"已脱离官方驱动读回实时电量，并同时识别出充电状态位。"
                    : L"已完全脱离官方驱动直接从 2.4G 接收器读回电量，点击下方按钮立即生效。");
            DrawTextStr(ctx, g_w.fmt_pill.Get(), sub_msg,
                        D2D1::RectF(hero.left + 26.0f, rcy + 116.0f, hero.right - 26.0f, rcy + 142.0f),
                        pal.text_secondary);

            /* Optional quick disambiguation / charging-bit learning strip */
            if (!g_w.saved_done) {
                D2D1_RECT_F ref_rc = D2D1::RectF(hero.left + 22.0f, hero.bottom - 94.0f, hero.right - 22.0f, hero.bottom - 16.0f);
                FillRoundRect(ctx, ref_rc, 12.0f,
                              pal.is_dark ? MakeColor(1, 1, 1, 0.05f) : MakeColor(0, 0, 0, 0.03f));
                DrawRoundRect(ctx, ref_rc, 12.0f, pal.card_border, 1.0f);

                const wchar_t *hint = (g_w.ambiguous_count > 0)
                    ? L"可选二次核验（检测到多处相同数值）：插上充电线或等电量变化后，输入新电量点确认"
                    : L"可选：想顺便学会“充电中”状态？插上充电线，勾选充电中并点确认";
                DrawTextStr(ctx, g_w.fmt_card_sub.Get(), hint,
                            D2D1::RectF(ref_rc.left + 12.0f, ref_rc.top + 8.0f, ref_rc.right - 12.0f, ref_rc.top + 28.0f),
                            pal.text_secondary);

                g_w.rc_refine_input = D2D1::RectF(ref_rc.left + 12.0f, ref_rc.top + 34.0f, ref_rc.left + 84.0f, ref_rc.top + 66.0f);
                FillRoundRect(ctx, g_w.rc_refine_input, 10.0f, pal.card_bg_hover);
                DrawRoundRect(ctx, g_w.rc_refine_input, 10.0f,
                              g_w.refine_focused ? pal.ring_normal : pal.card_border, g_w.refine_focused ? 1.5f : 1.0f);
                wchar_t rtxt[16];
                bool rcaret = g_w.refine_focused && (((int)(g_w.anim_phase * 2.0f) % 2) == 0);
                if (g_w.refine_pct_len > 0) _snwprintf(rtxt, 15, L"%ls%ls", g_w.refine_pct_buf, rcaret ? L"|" : L"");
                else _snwprintf(rtxt, 15, L"%d%ls", best.live_pct, rcaret ? L"|" : L"");
                rtxt[15] = L'\0';
                DrawTextStr(ctx, g_w.fmt_btn.Get(), rtxt, g_w.rc_refine_input,
                            g_w.refine_pct_len > 0 ? pal.text_primary : pal.text_muted);

                g_w.rc_refine_chg = D2D1::RectF(g_w.rc_refine_input.right + 12.0f, ref_rc.top + 36.0f, g_w.rc_refine_input.right + 126.0f, ref_rc.top + 64.0f);
                FillRoundRect(ctx, g_w.rc_refine_chg, 14.0f,
                              g_w.refine_charging
                                  ? MakeColor(pal.ring_normal.r, pal.ring_normal.g, pal.ring_normal.b, 0.20f)
                                  : pal.card_bg);
                DrawRoundRect(ctx, g_w.rc_refine_chg, 14.0f,
                              g_w.refine_charging ? pal.card_border_pinned : pal.card_border, 1.0f);
                DrawTextStr(ctx, g_w.fmt_pill.Get(), g_w.refine_charging ? L"⚡ 正在充电" : L"○ 正在充电",
                            g_w.rc_refine_chg, g_w.refine_charging ? pal.ring_normal : pal.text_secondary);

                g_w.rc_refine_btn = D2D1::RectF(ref_rc.right - 122.0f, ref_rc.top + 34.0f, ref_rc.right - 12.0f, ref_rc.top + 66.0f);
                DrawButton(ctx, pal, g_w.rc_refine_btn, L"二次精确确认", false, true, g_w.hover_hit == HIT_REFINE_BTN);
            }
        } else {
            /* Honest failure diagnostic */
            DrawOctansLogo(g_w.d2d_factory.Get(), ctx, cx, hero.top + 86.0f, 34.0f, g_w.is_dark, false, 1.0f, 0.65f);
            DrawTextStr(ctx, g_w.fmt_hero_title.Get(), L"这次未能独立验证通过",
                        D2D1::RectF(hero.left + 20.0f, hero.top + 138.0f, hero.right - 20.0f, hero.top + 170.0f),
                        pal.text_primary);

            wchar_t diag[320];
            if (g_w.captured_count == 0) {
                wcsncpy(diag, L"原因：监听期间未捕获到官方驱动发往该 2.4G 接收器的通信。\n建议：点下方“重新学习一次”，在监听状态下重新打开官方驱动并确认电量刷新出来。", 319);
            } else if (g_w.cand_count == 0) {
                _snwprintf(diag, 319, L"原因：已捕获 %u 条 USB 通信，但未在回包中匹配到电量 %d%%。\n建议：核对官方驱动显示的准确电量；若该设备采用了动态加密协议，暂无法通过明文规则读取。",
                           (unsigned)g_w.captured_count, g_w.expected_pct);
            } else {
                _snwprintf(diag, 319, L"原因：已捕获 %u 条通信并推断出 %u 条候选规则，但脱离官方驱动后独立查询未获预期回复。\n建议：请确认官方驱动（含右下角托盘图标）已彻底退出后再试；若仍失败，说明该设备需要官方驱动持续加密握手。",
                           (unsigned)g_w.captured_count, (unsigned)g_w.cand_count);
            }
            diag[319] = L'\0';
            DrawTextStr(ctx, g_w.fmt_body.Get(), diag,
                        D2D1::RectF(hero.left + 32.0f, hero.top + 180.0f, hero.right - 32.0f, hero.bottom - 24.0f),
                        pal.text_secondary);
        }

        /* Footer */
        float fy = kWinH - kFooterH + 10.0f;
        g_w.rc_back = D2D1::RectF(kPadX, fy, kPadX + 124.0f, fy + 38.0f);
        DrawButton(ctx, pal, g_w.rc_back, L"← 返回设备列表", false, !g_w.verifying, g_w.hover_hit == HIT_BACK);

        g_w.rc_primary = D2D1::RectF(kWinW - kPadX - 188.0f, fy, kWinW - kPadX, fy + 38.0f);
        if (g_w.saved_done) {
            DrawButton(ctx, pal, g_w.rc_primary, L"完成并关闭", true, true, g_w.hover_hit == HIT_PRIMARY);
        } else if (!g_w.verifying && g_w.best_idx >= 0) {
            DrawButton(ctx, pal, g_w.rc_primary, L"保存并立即启用", true, true, g_w.hover_hit == HIT_PRIMARY);
        } else {
            DrawButton(ctx, pal, g_w.rc_primary, L"重新学习一次", true, !g_w.verifying, g_w.hover_hit == HIT_PRIMARY);
        }
    }

    /* Toast Pill Overlay */
    if (g_w.toast_msg[0] && GetTickCount64() < g_w.toast_until_ms) {
        D2D1_RECT_F trc = D2D1::RectF(kPadX + 24.0f, kWinH - kFooterH - 42.0f, kWinW - kPadX - 24.0f, kWinH - kFooterH - 10.0f);
        FillRoundRect(ctx, trc, 16.0f, pal.is_dark ? HexColor(0x2C2C30, 0.96f) : HexColor(0x1C1C1E, 0.90f));
        DrawTextStr(ctx, g_w.fmt_pill.Get(), g_w.toast_msg, trc, MakeColor(1, 1, 1, 0.96f));
    }

    HRESULT hr = ctx->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseRenderTarget();
    } else if (g_w.swap_chain) {
        g_w.swap_chain->Present(1, 0);
    }
}

/* ============================================================================
 * Hit Testing & Window Procedure
 * ============================================================================ */

static int HitTestDip(float x, float y) {
    if (PtInRectF(g_w.rc_close, x, y)) return HIT_CLOSE;
    if (g_w.step == STEP_SELECT) {
        if (PtInRectF(g_w.rc_rescan, x, y)) return HIT_RESCAN;
        if (PtInRectF(g_w.rc_open_dir, x, y)) return HIT_OPEN_DIR;
        if (PtInRectF(g_w.rc_primary, x, y)) return HIT_PRIMARY;
        if (PtInRectF(g_w.rc_list_clip, x, y)) {
            for (size_t i = 0; i < g_w.group_count && i < 32u; ++i) {
                if (PtInRectF(g_w.rc_cards[i], x, y)) return HIT_CARD_BASE + (int)i;
            }
        }
    } else if (g_w.step == STEP_LEARN) {
        if (PtInRectF(g_w.rc_back, x, y)) return HIT_BACK;
        if (PtInRectF(g_w.rc_primary, x, y)) return HIT_PRIMARY;
        if (PtInRectF(g_w.rc_input_pct, x, y)) return HIT_INPUT_PCT;
    } else if (g_w.step == STEP_VERIFY) {
        if (PtInRectF(g_w.rc_back, x, y)) return HIT_BACK;
        if (PtInRectF(g_w.rc_primary, x, y)) return HIT_PRIMARY;
        if (PtInRectF(g_w.rc_refine_input, x, y)) return HIT_REFINE_INPUT;
        if (PtInRectF(g_w.rc_refine_chg, x, y)) return HIT_REFINE_CHG;
        if (PtInRectF(g_w.rc_refine_btn, x, y)) return HIT_REFINE_BTN;
    }
    return HIT_NONE;
}

static void HandleClick(int hit) {
    if (hit == HIT_CLOSE) {
        PostMessageW(g_w.hwnd, WM_CLOSE, 0, 0);
        return;
    }

    if (g_w.step == STEP_SELECT) {
        if (hit >= HIT_CARD_BASE && hit < HIT_CARD_BASE + (int)g_w.group_count) {
            g_w.selected_group = hit - HIT_CARD_BASE;
        } else if (hit == HIT_RESCAN) {
            StartFullScan();
        } else if (hit == HIT_OPEN_DIR) {
            wchar_t ini[MAX_PATH];
            GetDevicesIniPath(ini, MAX_PATH);
            wchar_t *sl = wcsrchr(ini, L'\\');
            if (sl) *sl = L'\0';
            CreateDirectoryW(ini, nullptr);
            ShellExecuteW(g_w.hwnd, L"open", ini, nullptr, nullptr, SW_SHOWNORMAL);
        } else if (hit == HIT_PRIMARY) {
            LaunchLearnForSelected();
        }
    } else if (g_w.step == STEP_LEARN) {
        if (hit == HIT_INPUT_PCT) {
            g_w.pct_focused = true;
        } else if (hit == HIT_BACK) {
            StopEtwIfRunning(false);
            g_w.step = STEP_SELECT;
            if (g_w.group_count == 0) StartFullScan();
        } else if (hit == HIT_PRIMARY) {
            BeginStep3Verify();
        }
    } else if (g_w.step == STEP_VERIFY) {
        if (hit == HIT_BACK && !g_w.verifying) {
            g_w.step = STEP_SELECT;
            StartFullScan();
        } else if (hit == HIT_REFINE_INPUT) {
            g_w.refine_focused = true;
        } else if (hit == HIT_REFINE_CHG) {
            g_w.refine_charging = !g_w.refine_charging;
        } else if (hit == HIT_REFINE_BTN && !g_w.refining && g_w.best_idx >= 0) {
            int np = g_w.refine_pct_len > 0 ? _wtoi(g_w.refine_pct_buf) : g_w.cands[g_w.best_idx].live_pct;
            if (np < 1 || np > 100) {
                ShowToast(L"请输入 1–100 的新电量");
            } else {
                g_w.refining = true;
                int packed = (np & 0xFF) | ((g_w.refine_charging ? 1 : 0) << 8);
                HANDLE h = CreateThread(nullptr, 0, RefineWorkerThread, (LPVOID)(intptr_t)packed, 0, nullptr);
                if (h) CloseHandle(h);
            }
        } else if (hit == HIT_PRIMARY && !g_w.verifying) {
            if (g_w.saved_done) {
                PostMessageW(g_w.hwnd, WM_CLOSE, 0, 0);
            } else if (g_w.best_idx >= 0) {
                SaveVerifiedAndFinish();
            } else {
                BeginStep2Learn();
            }
        }
    }
    InvalidateRect(g_w.hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK WizardWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_w.hwnd = hwnd;
        g_w.dpi = GetDpiForWindow(hwnd);
        if (g_w.dpi == 0) g_w.dpi = 96;
        g_w.is_dark = DetectSystemDarkMode();
        g_w.backdrop_ok = ApplyWin11Backdrop(hwnd, g_w.is_dark);
        g_w.last_tick_ms = GetTickCount64();
        SetTimer(hwnd, TIMER_ANIM, 33, nullptr);
        if (g_w.smoke_ms > 0) {
            SetTimer(hwnd, TIMER_SMOKE, (UINT)g_w.smoke_ms, nullptr);
        }
        if (g_w.launch_learn && g_w.target_vid != 0) {
            BeginStep2Learn();
        } else {
            StartFullScan();
        }
        return 0;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        float scale = 96.0f / (float)g_w.dpi;
        float x = pt.x * scale;
        float y = pt.y * scale;
        if (y < kHeaderH && !PtInRectF(g_w.rc_close, x, y)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }

    case WM_MOUSEMOVE: {
        float scale = 96.0f / (float)g_w.dpi;
        int hit = HitTestDip(GET_X_LPARAM(lp) * scale, GET_Y_LPARAM(lp) * scale);
        if (hit != g_w.hover_hit) {
            g_w.hover_hit = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursorW(nullptr, hit != HIT_NONE ? IDC_HAND : IDC_ARROW));
        return 0;
    }

    case WM_LBUTTONDOWN: {
        float scale = 96.0f / (float)g_w.dpi;
        int hit = HitTestDip(GET_X_LPARAM(lp) * scale, GET_Y_LPARAM(lp) * scale);
        if (hit != HIT_NONE) HandleClick(hit);
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (g_w.step == STEP_SELECT && g_w.list_max_scroll > 0.0f) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            g_w.list_scroll -= (float)delta * 0.4f;
            if (g_w.list_scroll < 0.0f) g_w.list_scroll = 0.0f;
            if (g_w.list_scroll > g_w.list_max_scroll) g_w.list_scroll = g_w.list_max_scroll;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wp;
        if (g_w.step == STEP_LEARN && g_w.pct_focused) {
            if (ch >= L'0' && ch <= L'9' && g_w.pct_len < 3) {
                g_w.pct_buf[g_w.pct_len++] = ch;
                g_w.pct_buf[g_w.pct_len] = L'\0';
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (ch == 8 && g_w.pct_len > 0) {
                g_w.pct_buf[--g_w.pct_len] = L'\0';
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (ch == 13) {
                BeginStep3Verify();
            }
        } else if (g_w.step == STEP_VERIFY && g_w.refine_focused) {
            if (ch >= L'0' && ch <= L'9' && g_w.refine_pct_len < 3) {
                g_w.refine_pct_buf[g_w.refine_pct_len++] = ch;
                g_w.refine_pct_buf[g_w.refine_pct_len] = L'\0';
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (ch == 8 && g_w.refine_pct_len > 0) {
                g_w.refine_pct_buf[--g_w.refine_pct_len] = L'\0';
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_TIMER:
        if (wp == TIMER_SMOKE) {
            KillTimer(hwnd, TIMER_SMOKE);
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == TIMER_ANIM) {
            uint64_t now = GetTickCount64();
            float dt = (float)(now - g_w.last_tick_ms) / 1000.0f;
            if (dt > 0.1f) dt = 0.033f;
            g_w.last_tick_ms = now;
            g_w.anim_phase = fmodf(g_w.anim_phase + dt, 1.0f);
            if (g_w.brand_t < 1.0f) {
                g_w.brand_t += dt * 1.9f;
                if (g_w.brand_t > 1.0f) g_w.brand_t = 1.0f;
            }
            if (g_w.step == STEP_LEARN && g_w.etw) {
                octans_etw_get_stats(g_w.etw, &g_w.live_tx, &g_w.live_rx, &g_w.live_dev_seen);
            }
            bool need_draw = g_w.scanning || g_w.verifying || g_w.refining ||
                             g_w.brand_t < 1.0f || g_w.step == STEP_LEARN ||
                             (g_w.toast_msg[0] != L'\0');
            if (g_w.toast_msg[0] && now >= g_w.toast_until_ms) {
                g_w.toast_msg[0] = L'\0';
                need_draw = true;
            }
            if (need_draw) RenderWizard();
        }
        return 0;

    case WM_SETTINGCHANGE: {
        bool dark = DetectSystemDarkMode();
        if (dark != g_w.is_dark) {
            g_w.is_dark = dark;
            g_w.backdrop_ok = ApplyWin11Backdrop(hwnd, g_w.is_dark);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_APP_SCAN_DONE: {
        ScanResult *res = (ScanResult *)lp;
        g_w.scanning = false;
        if (res) {
            g_w.all_iface_count = res->all_iface_count;
            memcpy(g_w.all_ifaces, res->all_ifaces, sizeof(g_w.all_ifaces));
            g_w.group_count = res->group_count;
            memcpy(g_w.groups, res->groups, sizeof(g_w.groups));
            g_w.auto_found_count = res->auto_found;

            /* Auto-select first unrecognized real peripheral if any */
            g_w.selected_group = -1;
            for (size_t i = 0; i < g_w.group_count; ++i) {
                if (!g_w.groups[i].is_system_controller && g_w.groups[i].live_pct < 0) {
                    g_w.selected_group = (int)i;
                    break;
                }
            }
            if (g_w.selected_group < 0 && g_w.group_count > 0) {
                g_w.selected_group = 0;
            }

            if (g_w.auto_found_count > 0) {
                _snwprintf(g_w.banner_msg, 255,
                           L"全面扫描完成！已自动识别并添加 %d 台新设备。若有遗漏请选设备点“跟着官方驱动学一次”。",
                           g_w.auto_found_count);
            } else {
                wcsncpy(g_w.banner_msg,
                        L"全面扫描完成。未识别的设备请选中下方卡片，点右下角“跟着官方驱动学一次”（全程约 15 秒）。",
                        255);
            }
            free(res);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_VERIFY_DONE: {
        VerifyResult *res = (VerifyResult *)lp;
        g_w.verifying = false;
        if (res) {
            g_w.cand_count = res->cand_count;
            memcpy(g_w.cands, res->cands, sizeof(g_w.cands));
            g_w.verified_count = res->verified_count;
            g_w.best_idx = res->best_idx;
            g_w.ambiguous_count = res->ambiguous_count;
            free(res);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_APP_REFINE_DONE: {
        RefineResult *rr = (RefineResult *)lp;
        g_w.refining = false;
        if (rr) {
            if (rr->remaining > 0 && rr->best_idx >= 0) {
                g_w.best_idx = rr->best_idx;
                g_w.ambiguous_count = (rr->remaining > 1) ? (rr->remaining - 1) : 0;
                if (g_w.cands[g_w.best_idx].rule.chg_offset >= 0) {
                    wcsncpy(g_w.refine_msg, L"二次精确核验通过！同时已自动识别出“充电中”状态。", 159);
                } else {
                    wcsncpy(g_w.refine_msg, L"二次精确核验通过！已锁定最准确的电量字段。", 159);
                }
            } else {
                ShowToast(L"二次核验未匹配新电量，已保留原验证结果");
            }
            free(rr);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RenderWizard();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        StopEtwIfRunning(false);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ANIM);
        ReleaseRenderTarget();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ParseCmdLine() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--smoke") == 0) {
            g_w.smoke_ms = 1200;
            if (i + 1 < argc) {
                int v = _wtoi(argv[i + 1]);
                if (v >= 100 && v <= 15000) g_w.smoke_ms = v;
            }
        } else if (wcscmp(argv[i], L"--learn") == 0 && i + 4 < argc) {
            g_w.launch_learn = true;
            g_w.target_vid = (uint16_t)wcstoul(argv[i + 1], nullptr, 16);
            g_w.target_pid = (uint16_t)wcstoul(argv[i + 2], nullptr, 16);
            g_w.target_kind = (octans_device_kind_t)_wtoi(argv[i + 3]);
            wcsncpy(g_w.target_name, argv[i + 4], 63);
            g_w.target_name[63] = L'\0';
            i += 4;
        }
    }
    LocalFree(argv);
}

} /* anonymous namespace */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    FontsInit();

    ParseCmdLine();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WizardWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"OctansAddDeviceWizardV3";
    RegisterClassExW(&wc);

    UINT sys_dpi = GetDpiForSystem();
    if (sys_dpi == 0) sys_dpi = 96;
    int px_w = MulDiv((int)kWinW, (int)sys_dpi, 96);
    int px_h = MulDiv((int)kWinH, (int)sys_dpi, 96);

    RECT wa = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + ((wa.right - wa.left) - px_w) / 2;
    int y = wa.top + ((wa.bottom - wa.top) - px_h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Octans · 添加无线设备",
        WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX | WS_SYSMENU,
        x, y, px_w, px_h,
        nullptr, nullptr, hInst, nullptr);

    if (hwnd) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    StopEtwIfRunning(false);
    free(g_w.captured_pkts);
    g_w.captured_pkts = nullptr;
    FontsShutdown();
    CoUninitialize();
    return 0;
}
