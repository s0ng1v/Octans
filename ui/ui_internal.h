#ifndef OCTANS_UI_INTERNAL_H
#define OCTANS_UI_INTERNAL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
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
#include <dwmapi.h>
#include <shellapi.h>
#include <d2d1_1.h>
#include <dwrite.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "octans_ui_api.h"

namespace octans_ui {

/* Fixed-size capacity — zero dynamic heap churn on hot UI updates. */
static constexpr size_t MAX_DEVICES = 16;

/* Internal Win32 messages marshaled to the UI thread. */
static constexpr UINT WM_APP_TRAY_CALLBACK    = WM_APP + 10;
static constexpr UINT WM_APP_DEVICES_UPDATED  = WM_APP + 11;
static constexpr UINT WM_APP_SHOW_PANEL       = WM_APP + 12;
static constexpr UINT WM_APP_HIDE_PANEL       = WM_APP + 13;
static constexpr UINT WM_APP_THEME_CHANGED    = WM_APP + 14;

enum TrayStyle : uint8_t {
    TRAY_STYLE_OCTANS = 0, /* Scheme 1 (Default): Liquid silhouette fill with 3-stop gradient (+ flowing wave in panel) */
    TRAY_STYLE_NEBULA = 1, /* Scheme 2: Classic circular battery ring + sleek outline icons */
    TRAY_STYLE_PULSAR = 2  /* Scheme 3: 12-segment precision dial (panel) + bold icon with bottom digits (tray) */
};

enum MenuMode : uint8_t {
    MENU_MODE_TRAY          = 0, /* Right-click on taskbar tray icon: full menu */
    MENU_MODE_PANEL_DEVICE  = 1, /* Right-click on a device card in flyout: Pin/Unpin + Rename only */
    MENU_MODE_PANEL_GENERAL = 2  /* Right-click on flyout background or '...' button: general settings, no Rename */
};

/* Continuous tray scale in [0.0f .. 1.0f]:
 *   0.00 = 小巧, 0.25 = 紧凑, 0.50 = 标准(默认, 原100%档位), 0.75 = 大号, 1.00 = 超大 */
static constexpr float kDefaultTrayScale = 0.50f;

/* Minimal zero-overhead COM smart pointer (no WRL/STL dependency). */
template <typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;

    T *Get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    T **Put() {
        Reset();
        return &ptr_;
    }

    void **PutVoid() {
        Reset();
        return reinterpret_cast<void **>(&ptr_);
    }

    void Reset(T *p = nullptr) {
        if (ptr_) {
            ptr_->Release();
        }
        ptr_ = p;
    }

private:
    T *ptr_;
};

/* Lightweight Direct2D value constructors (avoid d2d1helper.h quirks). */
static inline D2D1_COLOR_F MakeColor(float r, float g, float b, float a = 1.0f) {
    D2D1_COLOR_F c = { r, g, b, a };
    return c;
}

static inline D2D1_COLOR_F HexColor(uint32_t rgb, float a = 1.0f) {
    return MakeColor(
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        a
    );
}

static inline D2D1_POINT_2F MakePt(float x, float y) {
    D2D1_POINT_2F p = { x, y };
    return p;
}

static inline D2D1_RECT_F MakeRect(float l, float t, float r, float b) {
    D2D1_RECT_F rc = { l, t, r, b };
    return rc;
}

static inline D2D1_ROUNDED_RECT MakeRoundedRect(const D2D1_RECT_F &r, float rx, float ry) {
    D2D1_ROUNDED_RECT rr = { r, rx, ry };
    return rr;
}

static inline D2D1_ELLIPSE MakeEllipse(float cx, float cy, float rx, float ry) {
    D2D1_ELLIPSE e = { { cx, cy }, rx, ry };
    return e;
}

static inline D2D1_MATRIX_3X2_F MatrixIdentity() {
    D2D1_MATRIX_3X2_F m = {};
    m._11 = 1.0f; m._12 = 0.0f;
    m._21 = 0.0f; m._22 = 1.0f;
    m._31 = 0.0f; m._32 = 0.0f;
    return m;
}

static inline D2D1_MATRIX_3X2_F MatrixRotation(float deg, float cx, float cy) {
    float rad = deg * 0.017453292519943295f;
    float c = cosf(rad);
    float s = sinf(rad);
    D2D1_MATRIX_3X2_F m = {};
    m._11 = c;  m._12 = s;
    m._21 = -s; m._22 = c;
    m._31 = cx - cx * c + cy * s;
    m._32 = cy - cx * s - cy * c;
    return m;
}

static inline D2D1_MATRIX_3X2_F MatrixScaleTranslate(float sx, float sy, float cx, float cy, float tx, float ty) {
    D2D1_MATRIX_3X2_F m = {};
    m._11 = sx; m._12 = 0.0f;
    m._21 = 0.0f; m._22 = sy;
    m._31 = cx - cx * sx + tx;
    m._32 = cy - cy * sy + ty;
    return m;
}

static inline float ClampF(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float LerpF(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float EaseOutCubic(float t) {
    float u = 1.0f - ClampF(t, 0.0f, 1.0f);
    return 1.0f - u * u * u;
}

static inline float EaseOutQuint(float t) {
    float u = 1.0f - ClampF(t, 0.0f, 1.0f);
    float u2 = u * u;
    return 1.0f - u2 * u2 * u;
}

static inline float EaseInCubic(float t) {
    float u = ClampF(t, 0.0f, 1.0f);
    return u * u * u;
}

static inline void EnablePerMonitorDpiV2() {
    typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFn)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (fn) {
            /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4 */
            fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));
        }
    }
}

static inline UINT GetWindowDpiSafe(HWND hwnd) {
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 && hwnd) {
        auto fn = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) {
            UINT d = fn(hwnd);
            if (d >= 72) return d;
        }
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi >= 72 ? dpi : 96;
}

static inline int GetSystemMetricsDpiSafe(int index, UINT dpi) {
    typedef int (WINAPI *GetSystemMetricsForDpiFn)(int, UINT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        auto fn = reinterpret_cast<GetSystemMetricsForDpiFn>(
            GetProcAddress(user32, "GetSystemMetricsForDpi"));
        if (fn) {
            return fn(index, dpi);
        }
    }
    return MulDiv(GetSystemMetrics(index), static_cast<int>(dpi), 96);
}

/* Detach IME from non-text windows so third-party IMEs (e.g. Sogou ~35MB) never inject into tray/panel. */
static inline void DisableWindowIme(HWND hwnd) {
    if (!hwnd) return;
    typedef HANDLE (WINAPI *ImmAssociateContextFn)(HWND, HANDLE);
    HMODULE imm32 = GetModuleHandleW(L"imm32.dll");
    if (!imm32) imm32 = LoadLibraryW(L"imm32.dll");
    if (imm32) {
        auto fn = reinterpret_cast<ImmAssociateContextFn>(GetProcAddress(imm32, "ImmAssociateContext"));
        if (fn) fn(hwnd, nullptr);
    }
}

/* Ask Windows memory manager to trim cold working-set pages while Octans rests silently in the tray. */
static inline void TrimIdleWorkingSet() {
    SetProcessWorkingSetSizeEx(
        GetCurrentProcess(),
        static_cast<SIZE_T>(-1),
        static_cast<SIZE_T>(-1),
        0
    );
}

/* Optional internal hook for standalone demo_main.cpp (never exposed in octans_ui_api.h). */
struct DemoMenuHooks {
    void (*on_action)(int action_id, void *user_ctx); /* 1=drain/charge, 2=toggle_chg, 3=toggle_theme, 4=cycle_count */
    bool (*handle_key)(WPARAM vk, void *user_ctx);
    void *user_ctx;
};

enum ScanPhase : uint8_t {
    SCAN_IDLE     = 0,
    SCAN_ACTIVE   = 1,
    SCAN_COMPLETE = 2
};

/* Global UI state owned by ui_api.cpp and shared with ui_panel/ui_tray. */
struct UiState {
    DWORD ui_thread_id = 0;
    bool initialized = false;
    bool com_initialized = false;
    bool is_dark = true;
    bool modal_open = false;
    bool is_scanning = false;
    bool initial_scan_done = false;
    ScanPhase scan_phase = SCAN_IDLE;
    uint64_t scan_started_ms = 0;
    uint64_t scan_completed_ms = 0;
    bool refresh_pending = false;
    uint64_t last_inactive_hide_ms = 0;
    TrayStyle tray_style = TRAY_STYLE_OCTANS;
    float tray_scale = kDefaultTrayScale;

    octans_backend_ops_t ops = {};
    void *backend_ctx = nullptr;

    HWND host_hwnd = nullptr;

    SRWLOCK pending_lock = SRWLOCK_INIT;
    octans_device_view_t pending_devices[MAX_DEVICES] = {};
    size_t pending_count = 0;
    bool has_pending_devices = false;

    octans_device_view_t devices[MAX_DEVICES] = {};
    size_t device_count = 0;

    /* Multi-device tray pinning: user can pin any number of devices simultaneously. */
    char pinned_ids[MAX_DEVICES][64] = {};
    size_t pinned_count = 0;

    /* User-removed devices (hidden until user clicks "扫描设备"). */
    char hidden_ids[MAX_DEVICES][64] = {};
    size_t hidden_count = 0;

    DemoMenuHooks demo_hooks = {};
};

UiState &GetState();

/* Multi-pin & hidden helpers */
bool IsDevicePinned(const UiState &st, const char *device_id);
bool IsDeviceHidden(const UiState &st, const char *device_id);
size_t CountActivePinnedDevices(const UiState &st);

/* Shared controller actions called by panel & tray. */
void ActionTriggerRefresh();
void ActionTriggerFullScan();
void ActionTogglePinDevice(const char *device_id);
void ActionRemoveDevice(const char *device_id);
void ActionShowContextMenu(int screen_x, int screen_y, int clicked_device_index, MenuMode mode);
void ActionPromptRenameDevice(int device_index);
void ActionToggleTrayStyle(TrayStyle style);
void ActionSetTrayScale(float scale);
void ActionToggleAutostart();
void ActionShowAboutDialog();
void ActionRunDemoAction(int action_id);
void ActionRequestQuit();
void ActionSetDemoHooks(const DemoMenuHooks *hooks);

/* Helper: choose fallback primary device index when 0 pinned devices are active (-1 if none). */
int SelectFallbackTrayDeviceIndex(const octans_device_view_t *devices, size_t count);

} /* namespace octans_ui */

#endif /* OCTANS_UI_INTERNAL_H */
