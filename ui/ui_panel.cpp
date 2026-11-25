#include "ui_panel.h"
#include "ui_theme.h"
#include "ui_glyphs.h"
#include "ui_brand.h"
#include "ui_fonts.h"
#include "ui_tray.h"
#include <mmsystem.h>
#include <stdio.h>

namespace octans_ui {

static constexpr UINT_PTR TIMER_ANIM      = 1;
static constexpr UINT_PTR TIMER_HOVER     = 2;
static constexpr UINT_PTR TIMER_IDLE_TRIM = 3;

/* Layout constants (DIPs) */
static constexpr float kPanelPadX      = 16.0f;
static constexpr float kPanelPadTop    = 12.0f;
static constexpr float kPanelPadBottom = 12.0f;
static constexpr float kHeaderH        = 30.0f;
static constexpr float kHeaderGap      = 10.0f;
static constexpr float kCardW          = 152.0f;
static constexpr float kCardH          = 134.0f;
static constexpr float kCardGap        = 12.0f;
static constexpr float kFooterGap      = 10.0f;
static constexpr float kFooterH        = 26.0f;
static constexpr float kCardRadius     = 16.0f;
static constexpr float kPanelRadius    = 8.0f;  /* Matches Win11 DWMWCP_ROUND seamlessly */
static constexpr float kMaxGridViewH   = 356.0f; /* ~2.55 rows before scrolling */

struct CardAnim {
    char id[64];
    float pct_start;
    float display_pct;
    float target_pct;
    float pct_t;        /* Bounded ease-out tween progress [0 .. 1] — zero asymptotic 77->78 stall */
    float pct_dur;      /* Duration in seconds (0.52s on panel open, 0.42s on live updates) */
    float hover_t;
    float enter_t;      /* Staggered card entrance [<=0 .. 1] */
    float pulse_t;      /* Tactile click/pin spring pulse [1 -> 0] */
    bool initialized;
};

struct PanelContext {
    HWND hwnd = nullptr;
    UINT dpi = 96;
    bool backdrop_supported = false;

    /* Logical panel dimensions (DIPs) & anchored screen position (px) */
    float logical_w = 342.0f;
    float logical_h = 338.0f;
    int target_x = 0;
    int target_y = 0;
    int anchor_x = 0;
    int anchor_y = 0;
    bool has_anchor = false;
    bool anchor_from_top = false;

    /* Offscreen 32-bit premultiplied DIB size */
    UINT buf_w = 0;
    UINT buf_h = 0;

    /* Visibility & high-precision QPC animation state */
    bool want_visible = false;
    bool exiting_layered = false;
    bool animating = false;
    bool anim_fast_mode = false;
    bool timer_period_active = false;
    float panel_t = 0.0f; /* 0..1: 0=hidden, 1=open */
    float exit_u = 0.0f;  /* 0..1: monotonic shrink-to-tray progress */
    LARGE_INTEGER qpc_freq = {};
    LARGE_INTEGER last_qpc = {};
    uint64_t last_show_ms = 0;
    float refresh_spin_deg = 0.0f;
    bool refresh_spinning = false;
    float brand_anim_t = 1.0f;
    float liquid_wave_sec = 0.0f;
    float scan_elapsed_sec = 0.0f;
    float scan_done_sec = 0.0f;
    float scan_overlay_t = 0.0f;
    float refresh_tip_t = 0.0f;
    float refresh_done_sec = 0.0f;
    bool refresh_done_active = false;

    /* Interaction & inertial scroll state */
    int hover_card = -1;
    int hover_header_btn = 0; /* 0=none, 1=refresh, 2=more */
    bool mouse_tracked = false;
    int tooltip_card = -1;
    float tooltip_t = 0.0f;
    float scroll_y = 0.0f;
    float target_scroll_y = 0.0f;
    float max_scroll_y = 0.0f;

    /* Cached hit rects (DIPs) */
    D2D1_RECT_F refresh_btn_rc = {};
    D2D1_RECT_F more_btn_rc = {};
    D2D1_RECT_F discover_btn_rc = {};
    D2D1_RECT_F grid_hit_rc = {};
    D2D1_RECT_F grid_clip_rc = {};
    D2D1_RECT_F footer_rc = {};
    D2D1_RECT_F card_rects[MAX_DEVICES] = {};

    /* Per-device smooth ring & hover animations */
    CardAnim anims[MAX_DEVICES] = {};

    /* Ultra-lightweight CPU Direct2D + GDI DIB resources (ZERO D3D11/DXGI/GPU driver bloat!) */
    HDC mem_dc = nullptr;
    HBITMAP mem_bmp = nullptr;
    HGDIOBJ old_bmp = nullptr;
    void *mem_bits = nullptr;
    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<ID2D1StrokeStyle> round_stroke;

    ComPtr<IDWriteFactory> dwrite_factory;
    ComPtr<IDWriteTypography> tabular_typo;
    ComPtr<IDWriteTextFormat> fmt_title;
    ComPtr<IDWriteTextFormat> fmt_subtitle;
    ComPtr<IDWriteTextFormat> fmt_percent;
    ComPtr<IDWriteTextFormat> fmt_name;
    ComPtr<IDWriteTextFormat> fmt_sublabel;
    ComPtr<IDWriteTextFormat> fmt_tooltip;
    ComPtr<IDWriteTextFormat> fmt_empty;
};

static PanelContext g_panel;

static D2D1_COLOR_F LerpColor(const D2D1_COLOR_F &a, const D2D1_COLOR_F &b, float t) {
    t = ClampF(t, 0.0f, 1.0f);
    return MakeColor(
        LerpF(a.r, b.r, t),
        LerpF(a.g, b.g, t),
        LerpF(a.b, b.b, t),
        LerpF(a.a, b.a, t)
    );
}

static bool PtInD2DRect(const D2D1_RECT_F &r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static UINT GetFrameIntervalMs() {
    DWM_TIMING_INFO ti = {};
    ti.cbSize = sizeof(ti);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) &&
        ti.rateRefresh.uiNumerator > 0 && ti.rateRefresh.uiDenominator > 0) {
        double hz = static_cast<double>(ti.rateRefresh.uiNumerator) /
                    static_cast<double>(ti.rateRefresh.uiDenominator);
        if (hz >= 110.0) return 7;  /* ~120-144Hz */
        if (hz >= 85.0)  return 10; /* ~90-100Hz */
    }
    return 15; /* ~60Hz */
}

static void StartAnimTimer(bool fast = true) {
    if (!g_panel.hwnd) return;
    if (!g_panel.animating) {
        g_panel.animating = true;
        g_panel.anim_fast_mode = fast;
        if (g_panel.qpc_freq.QuadPart == 0) {
            QueryPerformanceFrequency(&g_panel.qpc_freq);
        }
        QueryPerformanceCounter(&g_panel.last_qpc);
        if (fast && !g_panel.timer_period_active) {
            timeBeginPeriod(1);
            g_panel.timer_period_active = true;
        }
        SetTimer(g_panel.hwnd, TIMER_ANIM, fast ? GetFrameIntervalMs() : 32u, nullptr);
    } else if (fast != g_panel.anim_fast_mode) {
        g_panel.anim_fast_mode = fast;
        if (fast && !g_panel.timer_period_active) {
            timeBeginPeriod(1);
            g_panel.timer_period_active = true;
        } else if (!fast && g_panel.timer_period_active) {
            timeEndPeriod(1);
            g_panel.timer_period_active = false;
        }
        SetTimer(g_panel.hwnd, TIMER_ANIM, fast ? GetFrameIntervalMs() : 32u, nullptr);
    }
}

static void StopAnimTimer() {
    if (g_panel.hwnd && g_panel.animating) {
        KillTimer(g_panel.hwnd, TIMER_ANIM);
        g_panel.animating = false;
        g_panel.anim_fast_mode = false;
    }
    if (g_panel.timer_period_active) {
        timeEndPeriod(1);
        g_panel.timer_period_active = false;
    }
}

static void ReleaseBackbuffer() {
    if (g_panel.mem_dc) {
        if (g_panel.old_bmp) {
            SelectObject(g_panel.mem_dc, g_panel.old_bmp);
            g_panel.old_bmp = nullptr;
        }
        DeleteDC(g_panel.mem_dc);
        g_panel.mem_dc = nullptr;
    }
    if (g_panel.mem_bmp) {
        DeleteObject(g_panel.mem_bmp);
        g_panel.mem_bmp = nullptr;
    }
    g_panel.mem_bits = nullptr;
    g_panel.buf_w = 0;
    g_panel.buf_h = 0;
}

static void ReleaseGraphicsResources() {
    ReleaseBackbuffer();
    g_panel.rt.Reset();
    g_panel.round_stroke.Reset();
}

static void ReleaseAllIdleResources() {
    ReleaseGraphicsResources();
    g_panel.fmt_empty.Reset();
    g_panel.fmt_tooltip.Reset();
    g_panel.fmt_sublabel.Reset();
    g_panel.fmt_name.Reset();
    g_panel.fmt_percent.Reset();
    g_panel.fmt_subtitle.Reset();
    g_panel.fmt_title.Reset();
    g_panel.tabular_typo.Reset();
    g_panel.dwrite_factory.Reset();
    g_panel.d2d_factory.Reset();
}

static bool EnsureDirectWriteResources() {
    if (g_panel.dwrite_factory) return true;

    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown **>(g_panel.dwrite_factory.Put())))) {
        return false;
    }

    if (SUCCEEDED(g_panel.dwrite_factory->CreateTypography(g_panel.tabular_typo.Put()))) {
        DWRITE_FONT_FEATURE feat = {};
        feat.nameTag = DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES;
        feat.parameter = 1;
        g_panel.tabular_typo->AddFontFeature(feat);
    }

    auto make_fmt = [](
        ComPtr<IDWriteTextFormat> &out,
        DWRITE_FONT_WEIGHT weight,
        float size,
        DWRITE_TEXT_ALIGNMENT align,
        bool ellipsis
    ) {
        CreateOctansTextFormat(
            g_panel.dwrite_factory.Get(),
            weight,
            size,
            align,
            DWRITE_FONT_STRETCH_NORMAL,
            ellipsis,
            out
        );
    };

    make_fmt(g_panel.fmt_title,    DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  true);
    make_fmt(g_panel.fmt_subtitle, DWRITE_FONT_WEIGHT_REGULAR,   11.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  true);
    make_fmt(g_panel.fmt_percent,  DWRITE_FONT_WEIGHT_SEMI_BOLD, 19.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   false);
    make_fmt(g_panel.fmt_name,     DWRITE_FONT_WEIGHT_MEDIUM,    12.0f, DWRITE_TEXT_ALIGNMENT_CENTER,   true);
    make_fmt(g_panel.fmt_sublabel, DWRITE_FONT_WEIGHT_REGULAR,   10.2f, DWRITE_TEXT_ALIGNMENT_CENTER,   true);
    make_fmt(g_panel.fmt_tooltip,  DWRITE_FONT_WEIGHT_REGULAR,   10.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   true);
    make_fmt(g_panel.fmt_empty,    DWRITE_FONT_WEIGHT_MEDIUM,    12.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   false);
    return true;
}

static bool EnsureGraphicsResources(UINT px_w, UINT px_h) {
    if (!g_panel.hwnd) return false;
    if (px_w < 2) px_w = 2;
    if (px_h < 2) px_h = 2;

    if (!EnsureDirectWriteResources()) return false;

    if (!g_panel.d2d_factory) {
        D2D1_FACTORY_OPTIONS fopts = {};
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory),
                &fopts,
                g_panel.d2d_factory.PutVoid()))) {
            return false;
        }
    }

    if (!g_panel.round_stroke) {
        D2D1_STROKE_STYLE_PROPERTIES sp = {};
        sp.startCap = D2D1_CAP_STYLE_ROUND;
        sp.endCap = D2D1_CAP_STYLE_ROUND;
        sp.dashCap = D2D1_CAP_STYLE_ROUND;
        sp.lineJoin = D2D1_LINE_JOIN_ROUND;
        sp.miterLimit = 10.0f;
        sp.dashStyle = D2D1_DASH_STYLE_SOLID;
        g_panel.d2d_factory->CreateStrokeStyle(sp, nullptr, 0, g_panel.round_stroke.Put());
    }

    if (!g_panel.rt) {
        D2D1_RENDER_TARGET_PROPERTIES rtp = {};
        /* Pure CPU software render target: renders 350x240 flyout in <0.7ms with ZERO D3D11/GPU driver DLL bloat! */
        rtp.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        rtp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        rtp.dpiX = static_cast<float>(g_panel.dpi);
        rtp.dpiY = static_cast<float>(g_panel.dpi);
        rtp.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        rtp.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        if (FAILED(g_panel.d2d_factory->CreateDCRenderTarget(&rtp, g_panel.rt.Put()))) {
            return false;
        }
    } else {
        float cur_dx = 96.0f, cur_dy = 96.0f;
        g_panel.rt->GetDpi(&cur_dx, &cur_dy);
        float want_dpi = static_cast<float>(g_panel.dpi);
        if (fabsf(cur_dx - want_dpi) > 0.5f) {
            g_panel.rt->SetDpi(want_dpi, want_dpi);
        }
    }

    if (!g_panel.mem_dc || !g_panel.mem_bmp || g_panel.buf_w != px_w || g_panel.buf_h != px_h) {
        ReleaseBackbuffer();
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(px_w);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(px_h);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC scr = GetDC(nullptr);
        g_panel.mem_dc = CreateCompatibleDC(scr);
        g_panel.mem_bmp = CreateDIBSection(scr, &bmi, DIB_RGB_COLORS, &g_panel.mem_bits, nullptr, 0);
        if (scr) ReleaseDC(nullptr, scr);
        if (!g_panel.mem_dc || !g_panel.mem_bmp || !g_panel.mem_bits) {
            ReleaseBackbuffer();
            return false;
        }
        g_panel.old_bmp = SelectObject(g_panel.mem_dc, g_panel.mem_bmp);
        g_panel.buf_w = px_w;
        g_panel.buf_h = px_h;
    }

    return g_panel.rt.Get() != nullptr && g_panel.mem_dc != nullptr;
}

static void ComputeLayoutMetrics() {
    const UiState &st = GetState();
    size_t count = st.device_count;

    float grid_w = 0.0f;
    float grid_content_h = 0.0f;
    float grid_view_h = 0.0f;
    int cols = 2;

    if (count == 0) {
        cols = 1;
        g_panel.logical_w = 308.0f;
        grid_content_h = (st.demo_hooks.on_action != nullptr) ? 116.0f : 168.0f;
        grid_view_h = grid_content_h;
    } else if (count == 1) {
        cols = 1;
        grid_w = kCardW + 76.0f; /* Comfortable header clearance for brand wordmark + "· 1 台设备" */
        g_panel.logical_w = kPanelPadX * 2.0f + grid_w;
        grid_content_h = (st.scan_phase != SCAN_IDLE) ? 156.0f : kCardH;
        grid_view_h = grid_content_h;
    } else {
        cols = 2;
        grid_w = kCardW * 2.0f + kCardGap;
        g_panel.logical_w = kPanelPadX * 2.0f + grid_w;
        int rows = static_cast<int>((count + 1) / 2);
        grid_content_h = rows * kCardH + (rows - 1) * kCardGap;
        grid_view_h = (grid_content_h > kMaxGridViewH) ? kMaxGridViewH : grid_content_h;
    }

    float grid_top = kPanelPadTop + kHeaderH + kHeaderGap;
    float footer_top = grid_top + grid_view_h + kFooterGap;
    g_panel.logical_h = footer_top + kFooterH + kPanelPadBottom;
    g_panel.max_scroll_y = (grid_content_h > grid_view_h) ? (grid_content_h - grid_view_h) : 0.0f;
    g_panel.target_scroll_y = ClampF(g_panel.target_scroll_y, 0.0f, g_panel.max_scroll_y);
    g_panel.scroll_y = ClampF(g_panel.scroll_y, 0.0f, g_panel.max_scroll_y);

    float btn_sz = 26.0f;
    float btn_y = kPanelPadTop + (kHeaderH - btn_sz) * 0.5f;
    float more_r = g_panel.logical_w - kPanelPadX;
    g_panel.more_btn_rc = MakeRect(more_r - btn_sz, btn_y, more_r, btn_y + btn_sz);
    float ref_r = g_panel.more_btn_rc.left - 4.0f;
    g_panel.refresh_btn_rc = MakeRect(ref_r - btn_sz, btn_y, ref_r, btn_y + btn_sz);

    g_panel.grid_hit_rc = MakeRect(
        kPanelPadX,
        grid_top,
        g_panel.logical_w - kPanelPadX,
        grid_top + grid_view_h
    );
    /* Generous 6dp breathing margin so hover lift/scale (1.022x, -1.8dp) is NEVER clipped! */
    g_panel.grid_clip_rc = MakeRect(
        kPanelPadX - 6.0f,
        grid_top - 6.0f,
        g_panel.logical_w - kPanelPadX + 6.0f,
        grid_top + grid_view_h + 6.0f
    );
    g_panel.footer_rc = MakeRect(
        kPanelPadX,
        footer_top,
        g_panel.logical_w - kPanelPadX,
        footer_top + kFooterH
    );

    if (count == 0 && !st.is_scanning && !(st.demo_hooks.on_action != nullptr)) {
        float cx = g_panel.logical_w * 0.5f;
        float btn_w = 108.0f;
        float btn_h = 30.0f;
        float btn_top = grid_top + 114.0f;
        g_panel.discover_btn_rc = MakeRect(cx - btn_w * 0.5f, btn_top, cx + btn_w * 0.5f, btn_top + btn_h);
    } else {
        g_panel.discover_btn_rc = MakeRect(0.0f, 0.0f, 0.0f, 0.0f);
    }

    for (size_t i = 0; i < count && i < MAX_DEVICES; ++i) {
        if (count == 1) {
            g_panel.card_rects[i] = MakeRect(
                kPanelPadX,
                grid_top,
                g_panel.logical_w - kPanelPadX,
                grid_top + kCardH
            );
        } else {
            int row = static_cast<int>(i) / cols;
            int col = static_cast<int>(i) % cols;
            float x0 = kPanelPadX + col * (kCardW + kCardGap);
            float y0 = grid_top + row * (kCardH + kCardGap) - g_panel.scroll_y;
            g_panel.card_rects[i] = MakeRect(x0, y0, x0 + kCardW, y0 + kCardH);
        }
    }
}

static void ComputeTargetPosition(int ax, int ay) {
    g_panel.anchor_x = ax;
    g_panel.anchor_y = ay;
    g_panel.has_anchor = true;

    POINT pt = { ax, ay };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    /* Query monitor DPI cleanly */
    UINT dpi = GetWindowDpiSafe(g_panel.hwnd);
    g_panel.dpi = dpi;
    float scale = static_cast<float>(dpi) / 96.0f;

    int px_w = static_cast<int>(ceilf(g_panel.logical_w * scale));
    int px_h = static_cast<int>(ceilf(g_panel.logical_h * scale));
    int margin = static_cast<int>(ceilf(12.0f * scale));

    int x = ax - px_w / 2;
    if (x + px_w > mi.rcWork.right - margin) {
        x = mi.rcWork.right - margin - px_w;
    }
    if (x < mi.rcWork.left + margin) {
        x = mi.rcWork.left + margin;
    }

    int y = 0;
    if (ay < mi.rcWork.top + static_cast<int>(64.0f * scale)) {
        /* Top taskbar */
        g_panel.anchor_from_top = true;
        y = mi.rcWork.top + margin;
    } else {
        /* Standard bottom taskbar */
        g_panel.anchor_from_top = false;
        y = mi.rcWork.bottom - margin - px_h;
    }
    if (y < mi.rcWork.top + margin) y = mi.rcWork.top + margin;
    if (y + px_h > mi.rcWork.bottom - margin) y = mi.rcWork.bottom - margin - px_h;

    g_panel.target_x = x;
    g_panel.target_y = y;
}

static void ApplyWindowBoundsForAnim() {
    if (!g_panel.hwnd) return;
    float scale = static_cast<float>(g_panel.dpi) / 96.0f;
    int px_w = static_cast<int>(ceilf(g_panel.logical_w * scale));
    int px_h = static_cast<int>(ceilf(g_panel.logical_h * scale));

    /* On exit, keep HWND stationary while DirectComposition GPU-shrinks & fades smoothly into tray! */
    int slide_px = 0;
    if (g_panel.want_visible) {
        float ease = EaseOutQuint(g_panel.panel_t);
        slide_px = static_cast<int>(roundf((1.0f - ease) * 16.0f * scale));
    }
    int cur_y = g_panel.anchor_from_top ? (g_panel.target_y - slide_px) : (g_panel.target_y + slide_px);

    SetWindowPos(
        g_panel.hwnd,
        HWND_TOPMOST,
        g_panel.target_x,
        cur_y,
        px_w,
        px_h,
        SWP_NOACTIVATE
    );
}

static const wchar_t *FormatConnShort(octans_conn_kind_t c) {
    switch (c) {
        case OCTANS_CONN_DONGLE_24G: return L"2.4G";
        case OCTANS_CONN_BLUETOOTH:  return L"\u84dd\u7259";
        case OCTANS_CONN_WIRED:      return L"\u6709\u7ebf";
        default:                     return L"";
    }
}

static void FormatSubLabel(const octans_device_view_t &d, wchar_t *buf, size_t cap, int *semantic_kind) {
    /* semantic_kind: 0=neutral, 1=low(red), 2=charging/full(green), 3=muted */
    *semantic_kind = 0;
    const wchar_t *conn = FormatConnShort(d.conn);

    if (d.power_state == OCTANS_PWR_OFFLINE) {
        *semantic_kind = 3;
        _snwprintf(buf, cap - 1, L"\u79bb\u7ebf");
        buf[cap - 1] = L'\0';
        return;
    }
    if (d.power_state == OCTANS_PWR_SLEEPING) {
        *semantic_kind = 3;
        if (conn[0]) _snwprintf(buf, cap - 1, L"%ls \u00b7 \u4f11\u7720", conn);
        else _snwprintf(buf, cap - 1, L"\u4f11\u7720");
        buf[cap - 1] = L'\0';
        return;
    }
    if (d.power_state == OCTANS_PWR_CHARGING) {
        *semantic_kind = 2;
        if (conn[0]) _snwprintf(buf, cap - 1, L"%ls \u00b7 \u5145\u7535\u4e2d", conn);
        else _snwprintf(buf, cap - 1, L"\u5145\u7535\u4e2d");
        buf[cap - 1] = L'\0';
        return;
    }
    if (d.power_state == OCTANS_PWR_FULL) {
        *semantic_kind = 2;
        if (conn[0]) _snwprintf(buf, cap - 1, L"%ls \u00b7 \u5df2\u5145\u6ee1", conn);
        else _snwprintf(buf, cap - 1, L"\u5df2\u5145\u6ee1");
        buf[cap - 1] = L'\0';
        return;
    }
    if (d.is_low_battery || (d.battery_percent >= 0 && d.battery_percent <= 20)) {
        *semantic_kind = 1;
        if (conn[0]) _snwprintf(buf, cap - 1, L"%ls \u00b7 \u4f4e\u7535\u91cf", conn);
        else _snwprintf(buf, cap - 1, L"\u4f4e\u7535\u91cf");
        buf[cap - 1] = L'\0';
        return;
    }
    if (d.is_estimated) {
        if (conn[0]) _snwprintf(buf, cap - 1, L"%ls \u00b7 \u4f30\u7b97", conn);
        else _snwprintf(buf, cap - 1, L"\u4f30\u7b97");
        buf[cap - 1] = L'\0';
        return;
    }

    if (conn[0]) {
        wcsncpy(buf, conn, cap - 1);
        buf[cap - 1] = L'\0';
    } else {
        wcsncpy(buf, L"\u5df2\u8fde\u63a5", cap - 1);
        buf[cap - 1] = L'\0';
    }
}

static void FormatTooltipDetail(const octans_device_view_t &d, wchar_t *buf, size_t cap) {
    buf[0] = L'\0';
    size_t pos = 0;
    auto append = [&](const wchar_t *s) {
        if (!s || !s[0]) return;
        size_t len = wcslen(s);
        if (pos + len < cap) {
            wmemcpy(buf + pos, s, len);
            pos += len;
            buf[pos] = L'\0';
        }
    };

    if (d.brand[0] && wcsstr(d.name, d.brand) == nullptr) {
        append(d.brand);
        append(L" \u00b7 ");
    }
    append(d.name[0] ? d.name : L"Device");

    if (d.voltage_mv > 0) {
        wchar_t vb[32] = {};
        _snwprintf(vb, 31, L" \u00b7 %.2fV", static_cast<double>(d.voltage_mv) / 1000.0);
        append(vb);
    }
    if (d.signal_percent >= 0) {
        wchar_t sb[32] = {};
        _snwprintf(sb, 31, L" \u00b7 \u4fe1\u53f7 %d%%", static_cast<int>(d.signal_percent));
        append(sb);
    }
    if (d.is_estimated) {
        append(L" \u00b7 \u7535\u538b\u4f30\u7b97");
    }
    if (d.updated_ms > 0) {
        uint64_t now = GetTickCount64();
        uint64_t age_s = (now > d.updated_ms) ? ((now - d.updated_ms) / 1000ULL) : 0ULL;
        wchar_t ab[32] = {};
        if (age_s < 5) {
            wcscpy(ab, L" \u00b7 \u521a\u521a\u66f4\u65b0");
        } else if (age_s < 60) {
            _snwprintf(ab, 31, L" \u00b7 %us \u524d", static_cast<unsigned>(age_s));
        } else {
            _snwprintf(ab, 31, L" \u00b7 %um \u524d", static_cast<unsigned>(age_s / 60ULL));
        }
        append(ab);
    }
}

static void DrawTabularText(
    ID2D1RenderTarget *ctx,
    const wchar_t *text,
    IDWriteTextFormat *fmt,
    const D2D1_RECT_F &rect,
    ID2D1Brush *brush
) {
    if (!ctx || !text || !fmt || !brush || !g_panel.dwrite_factory) return;
    UINT32 len = static_cast<UINT32>(wcslen(text));
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 0.0f || h <= 0.0f) return;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_panel.dwrite_factory->CreateTextLayout(text, len, fmt, w, h, layout.Put()))) {
        ctx->DrawText(text, len, fmt, rect, brush);
        return;
    }

    if (g_panel.tabular_typo) {
        DWRITE_TEXT_RANGE range = { 0, len };
        layout->SetTypography(g_panel.tabular_typo.Get(), range);
    }
    ctx->DrawTextLayout(MakePt(rect.left, rect.top), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

/* Smooth left-to-right progressive text reveal (clipped precisely to centered layout bounds). */
static void DrawProgressiveRevealText(
    ID2D1RenderTarget *ctx,
    const wchar_t *text,
    IDWriteTextFormat *fmt,
    const D2D1_RECT_F &rect,
    ID2D1Brush *brush,
    float reveal_frac
) {
    if (!ctx || !text || !fmt || !brush || !g_panel.dwrite_factory) return;
    float rf = ClampF(reveal_frac, 0.0f, 1.0f);
    if (rf <= 0.005f) return;

    UINT32 len = static_cast<UINT32>(wcslen(text));
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w <= 0.0f || h <= 0.0f) return;

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_panel.dwrite_factory->CreateTextLayout(text, len, fmt, w, h, layout.Put()))) {
        ctx->DrawText(text, len, fmt, rect, brush);
        return;
    }

    DWRITE_TEXT_METRICS tm = {};
    if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.widthIncludingTrailingWhitespace > 1.0f) {
        float text_l = rect.left + tm.left;
        float text_w = tm.widthIncludingTrailingWhitespace + 4.0f;
        D2D1_RECT_F clip_rc = MakeRect(text_l - 2.0f, rect.top, text_l + text_w * rf, rect.bottom);
        ctx->PushAxisAlignedClip(clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ctx->DrawTextLayout(MakePt(rect.left, rect.top), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
        ctx->PopAxisAlignedClip();
    } else {
        ctx->DrawTextLayout(MakePt(rect.left, rect.top), layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

static void RenderPanel(HDC paint_dc = nullptr) {
    if (!g_panel.hwnd) return;
    const UiState &st = GetState();
    ComputeLayoutMetrics();

    float scale = static_cast<float>(g_panel.dpi) / 96.0f;
    UINT px_w = static_cast<UINT>(ceilf(g_panel.logical_w * scale));
    UINT px_h = static_cast<UINT>(ceilf(g_panel.logical_h * scale));
    if (!EnsureGraphicsResources(px_w, px_h)) return;

    RECT bind_rc = { 0, 0, static_cast<LONG>(px_w), static_cast<LONG>(px_h) };
    if (FAILED(g_panel.rt->BindDC(g_panel.mem_dc, &bind_rc))) {
        ReleaseGraphicsResources();
        return;
    }

    ThemePalette pal = GetThemePalette(st.is_dark);
    ID2D1RenderTarget *ctx = g_panel.rt.Get();

    /* Open mode: panel surface strictly 1:1 with Win11 DWM Acrylic HWND (zero double layer).
     * Exit mode: DWM Acrylic disabled + WS_EX_LAYERED UpdateLayeredWindow shrinks & fades cleanly into tray! */
    bool is_exiting = (!g_panel.want_visible && g_panel.exiting_layered);
    float vis_ease = EaseOutQuint(g_panel.panel_t);
    float exit_p = ClampF(g_panel.exit_u, 0.0f, 1.0f);
    float exit_accel = exit_p * exit_p; /* True ease-in acceleration into tray: zero stall! */

    /* Keep card & content equally vivid as it accelerates into the tray, dissolving seamlessly to 0.00 */
    float bg_alpha = is_exiting ? ClampF(1.0f - exit_accel, 0.0f, 1.0f) : 1.0f;
    float content_alpha = is_exiting ? bg_alpha : ClampF(vis_ease, 0.0f, 1.0f);

    ctx->BeginDraw();
    if (is_exiting) {
        /* Compute pivot directly toward the clicked tray icon along the bottom (or top) edge */
        float pivot_x = g_panel.logical_w * 0.5f;
        if (g_panel.has_anchor) {
            float rel_x = static_cast<float>(g_panel.anchor_x - g_panel.target_x) / scale;
            pivot_x = ClampF(rel_x, 32.0f, g_panel.logical_w - 32.0f);
        }
        float pivot_y = g_panel.anchor_from_top ? 0.0f : g_panel.logical_h;
        float s = LerpF(1.0f, 0.78f, exit_accel);
        float ty = (g_panel.anchor_from_top ? -1.0f : 1.0f) * (exit_accel * 26.0f);
        ctx->SetTransform(MatrixScaleTranslate(s, s, pivot_x, pivot_y, 0.0f, ty));
    } else {
        ctx->SetTransform(MatrixIdentity());
    }
    ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ctx->Clear(MakeColor(0.0f, 0.0f, 0.0f, 0.0f));

    ComPtr<ID2D1SolidColorBrush> brush;
    ctx->CreateSolidColorBrush(pal.text_primary, brush.Put());
    if (!brush) {
        ctx->EndDraw();
        return;
    }

    auto set_bg_col = [&](D2D1_COLOR_F c) {
        c.a *= bg_alpha;
        brush->SetColor(c);
    };
    auto set_col = [&](D2D1_COLOR_F c, float extra_a = 1.0f) {
        c.a *= (content_alpha * extra_a);
        brush->SetColor(c);
    };

    /* 1. Frosted panel surface (1:1 over Win11 Acrylic while open; crisp frosted card while shrinking into tray) */
    D2D1_RECT_F full_rc = MakeRect(0.0f, 0.0f, g_panel.logical_w, g_panel.logical_h);
    D2D1_ROUNDED_RECT panel_rr = MakeRoundedRect(full_rc, kPanelRadius, kPanelRadius);
    bool use_acrylic_tint = (g_panel.backdrop_supported && !is_exiting);
    set_bg_col(use_acrylic_tint ? pal.panel_tint : pal.panel_fallback_bg);
    ctx->FillRoundedRectangle(panel_rr, brush.Get());

    /* Subtle top inner specular highlight + outer hairline */
    D2D1_RECT_F inner_rc = MakeRect(1.0f, 1.0f, g_panel.logical_w - 1.0f, g_panel.logical_h - 1.0f);
    set_bg_col(pal.panel_inner_specular);
    ctx->DrawRoundedRectangle(MakeRoundedRect(inner_rc, kPanelRadius - 1.0f, kPanelRadius - 1.0f), brush.Get(), 1.0f);

    D2D1_RECT_F outer_rc = MakeRect(0.5f, 0.5f, g_panel.logical_w - 0.5f, g_panel.logical_h - 0.5f);
    set_bg_col(pal.panel_outer_border);
    ctx->DrawRoundedRectangle(MakeRoundedRect(outer_rc, kPanelRadius - 0.5f, kPanelRadius - 0.5f), brush.Get(), 1.0f);

    /* 2. Minimal Header: Octans Vector Wordmark + "· N 台设备" + Refresh / More icon buttons */
    float header_top = kPanelPadTop;
    float header_bot = header_top + kHeaderH;

    D2D1_RECT_F wm_rc = MakeRect(kPanelPadX, header_top + 2.5f, kPanelPadX + 76.0f, header_top + 23.5f);
    DrawOctansWordmark(
        g_panel.d2d_factory.Get(),
        ctx,
        wm_rc,
        st.is_dark,
        g_panel.brand_anim_t,
        content_alpha
    );

    wchar_t count_buf[48] = {};
    if (st.scan_phase == SCAN_ACTIVE) {
        wcscpy(count_buf, L"\u00b7 \u626b\u63cf\u4e2d");
    } else if (st.device_count > 0) {
        _snwprintf(count_buf, 47, L"\u00b7 %u \u53f0\u8bbe\u5907", static_cast<unsigned>(st.device_count));
    } else {
        wcscpy(count_buf, L"\u00b7 \u65e0\u8bbe\u5907");
    }
    D2D1_RECT_F sub_rc = MakeRect(kPanelPadX + 79.0f, header_top + 3.0f, g_panel.refresh_btn_rc.left - 6.0f, header_bot);

    /* Subtle whisper-quiet header cross-fade ("· 已刷新") when manual refresh completes */
    float ref_badge_a = 0.0f;
    if (g_panel.refresh_done_active && st.scan_phase == SCAN_IDLE) {
        float t = g_panel.refresh_done_sec;
        if (t < 0.24f) {
            ref_badge_a = EaseOutCubic(t / 0.24f);
        } else if (t < 1.34f) {
            ref_badge_a = 1.0f;
        } else if (t < 1.78f) {
            ref_badge_a = 1.0f - EaseOutCubic((t - 1.34f) / 0.44f);
        }
    }
    if (ref_badge_a < 0.99f) {
        D2D1_RECT_F cnt_rc = MakeRect(sub_rc.left, sub_rc.top - ref_badge_a * 2.5f, sub_rc.right, sub_rc.bottom - ref_badge_a * 2.5f);
        set_col(pal.text_secondary, 1.0f - ref_badge_a);
        ctx->DrawText(count_buf, static_cast<UINT32>(wcslen(count_buf)), g_panel.fmt_subtitle.Get(), cnt_rc, brush.Get());
    }
    if (ref_badge_a > 0.01f) {
        D2D1_RECT_F done_rc = MakeRect(sub_rc.left, sub_rc.top + (1.0f - ref_badge_a) * 2.5f, sub_rc.right, sub_rc.bottom + (1.0f - ref_badge_a) * 2.5f);
        set_col(st.is_dark ? HexColor(0x34D399, 0.95f) : HexColor(0x10B981, 0.95f), ref_badge_a);
        ctx->DrawText(L"\u00b7 \u5df2\u5237\u65b0", 5, g_panel.fmt_subtitle.Get(), done_rc, brush.Get());
    }

    /* Refresh button */
    {
        bool hov = (g_panel.hover_header_btn == 1);
        float cx = (g_panel.refresh_btn_rc.left + g_panel.refresh_btn_rc.right) * 0.5f;
        float cy = (g_panel.refresh_btn_rc.top + g_panel.refresh_btn_rc.bottom) * 0.5f;
        if (hov) {
            set_col(pal.btn_hover_bg);
            ctx->FillEllipse(MakeEllipse(cx, cy, 13.0f, 13.0f), brush.Get());
        }
        set_col(hov ? pal.btn_icon_hover : pal.btn_icon);
        DrawRefreshIcon(
            g_panel.d2d_factory.Get(),
            ctx,
            brush.Get(),
            g_panel.round_stroke.Get(),
            cx, cy,
            5.6f,
            g_panel.refresh_spin_deg
        );
    }

    /* More (...) button */
    {
        bool hov = (g_panel.hover_header_btn == 2);
        float cx = (g_panel.more_btn_rc.left + g_panel.more_btn_rc.right) * 0.5f;
        float cy = (g_panel.more_btn_rc.top + g_panel.more_btn_rc.bottom) * 0.5f;
        if (hov) {
            set_col(pal.btn_hover_bg);
            ctx->FillEllipse(MakeEllipse(cx, cy, 13.0f, 13.0f), brush.Get());
        }
        set_col(hov ? pal.btn_icon_hover : pal.btn_icon);
        DrawMoreIcon(ctx, brush.Get(), cx, cy, 5.8f);
    }

    /* 3. Device Grid (2x2 Apple iOS Batteries Cards) or Empty State.
     *    Only clip when scrollable (>4 devices), with 6dp breathing room so hovered cards never clip! */
    bool need_grid_clip = (g_panel.max_scroll_y > 0.5f);
    if (need_grid_clip) {
        ctx->PushAxisAlignedClip(g_panel.grid_clip_rc, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    float cards_vis = 1.0f - EaseOutCubic(ClampF(g_panel.scan_overlay_t, 0.0f, 1.0f));

    if (st.device_count == 0) {
        if (cards_vis > 0.01f) {
            float empty_a = cards_vis;
            float cx = (g_panel.grid_hit_rc.left + g_panel.grid_hit_rc.right) * 0.5f;
            float cy = g_panel.grid_hit_rc.top + 46.0f;
            DrawOctansLogo(
                g_panel.d2d_factory.Get(),
                ctx,
                cx, cy,
                22.0f,
                st.is_dark,
                false,
                g_panel.brand_anim_t,
                content_alpha * empty_a
            );

            D2D1_RECT_F er = MakeRect(g_panel.grid_hit_rc.left, cy + 32.0f, g_panel.grid_hit_rc.right, cy + 54.0f);
            set_col(pal.text_secondary, empty_a);
            ctx->DrawText(L"\u672a\u68c0\u6d4b\u5230\u65e0\u7ebf\u5916\u8bbe", 8, g_panel.fmt_name.Get(), er, brush.Get());

            if (!(st.demo_hooks.on_action != nullptr) && g_panel.discover_btn_rc.right > g_panel.discover_btn_rc.left) {
                bool hov = (g_panel.hover_header_btn == 3);
                D2D1_RECT_F br = g_panel.discover_btn_rc;
                D2D1_ROUNDED_RECT drr = MakeRoundedRect(br, 15.0f, 15.0f);

                /* Refined Apple/Octans frosted tinted pill (light & airy, never heavy) */
                D2D1_COLOR_F accent = st.is_dark ? HexColor(0x34D399, 1.0f) : HexColor(0x059669, 1.0f);
                D2D1_COLOR_F bg = st.is_dark
                    ? (hov ? HexColor(0x34D399, 0.18f) : MakeColor(1.0f, 1.0f, 1.0f, 0.085f))
                    : (hov ? HexColor(0x10B981, 0.13f) : MakeColor(1.0f, 1.0f, 1.0f, 0.92f));
                D2D1_COLOR_F bd = st.is_dark
                    ? (hov ? HexColor(0x34D399, 0.55f) : MakeColor(1.0f, 1.0f, 1.0f, 0.14f))
                    : (hov ? HexColor(0x10B981, 0.45f) : MakeColor(0.0f, 0.0f, 0.0f, 0.09f));
                D2D1_COLOR_F fg = hov ? accent : pal.text_primary;

                if (!st.is_dark) {
                    /* Soft ambient shadow under light pill */
                    set_col(MakeColor(0.0f, 0.0f, 0.0f, 0.045f), empty_a);
                    ctx->FillRoundedRectangle(
                        MakeRoundedRect(MakeRect(br.left, br.top + 1.0f, br.right, br.bottom + 1.5f), 15.0f, 15.0f),
                        brush.Get()
                    );
                }

                set_col(bg, empty_a);
                ctx->FillRoundedRectangle(drr, brush.Get());

                set_col(bd, empty_a);
                ctx->DrawRoundedRectangle(
                    MakeRoundedRect(MakeRect(br.left + 0.5f, br.top + 0.5f, br.right - 0.5f, br.bottom - 0.5f), 14.5f, 14.5f),
                    brush.Get(), 1.0f
                );

                /* Optically centered radar icon + "扫描设备" label */
                float bcx = (br.left + br.right) * 0.5f;
                float bcy = (br.top + br.bottom) * 0.5f;
                set_col(hov ? accent : pal.text_secondary, empty_a);
                DrawScanRadarIcon(g_panel.d2d_factory.Get(), ctx, brush.Get(), g_panel.round_stroke.Get(), bcx - 27.0f, bcy, 5.4f);

                set_col(fg, empty_a * 0.94f);
                D2D1_RECT_F tr = MakeRect(bcx - 16.0f, br.top, br.right - 8.0f, br.bottom);
                ctx->DrawText(L"\u626b\u63cf\u8bbe\u5907", 4, g_panel.fmt_name.Get(), tr, brush.Get());
            }
        }
    } else if (cards_vis > 0.01f) {
        for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
            const octans_device_view_t &d = st.devices[i];
            const D2D1_RECT_F &cr = g_panel.card_rects[i];
            if (need_grid_clip && (cr.bottom < g_panel.grid_clip_rc.top - 4.0f || cr.top > g_panel.grid_clip_rc.bottom + 4.0f)) {
                continue;
            }

            bool is_pinned = IsDevicePinned(st, d.id);
            float ht = g_panel.anims[i].hover_t;
            float ent = EaseOutQuint(ClampF(g_panel.anims[i].enter_t, 0.0f, 1.0f));
            float pulse = g_panel.anims[i].pulse_t;
            bool muted = IsDeviceMuted(d);

            float card_cx = (cr.left + cr.right) * 0.5f;
            float card_cy = (cr.top + cr.bottom) * 0.5f;
            /* Tactile card transform: staggered rise + subtle hover lift + click spring bounce */
            float hover_scale = 1.0f + ht * 0.020f;
            float press_dip = (pulse > 0.0f) ? (-0.030f * sinf(pulse * 3.14159265f)) : 0.0f;
            float card_scale = LerpF(0.94f, 1.0f, ent) * (hover_scale + press_dip);
            float card_ty = (1.0f - ent) * 11.0f - ht * 1.6f;

            D2D1_MATRIX_3X2_F base_xf = {};
            ctx->GetTransform(&base_xf);
            D2D1_MATRIX_3X2_F card_xf = MatrixScaleTranslate(card_scale, card_scale, card_cx, card_cy, 0.0f, card_ty);
            D2D1_MATRIX_3X2_F combined = {};
            combined._11 = card_xf._11 * base_xf._11;
            combined._12 = 0.0f;
            combined._21 = 0.0f;
            combined._22 = card_xf._22 * base_xf._22;
            combined._31 = card_xf._31 * base_xf._11 + base_xf._31;
            combined._32 = card_xf._32 * base_xf._22 + base_xf._32;
            ctx->SetTransform(combined);

            float card_alpha = ClampF(ent, 0.0f, 1.0f) * cards_vis;
            auto set_card_col = [&](D2D1_COLOR_F c, float extra_a = 1.0f) {
                c.a *= (content_alpha * card_alpha * extra_a);
                brush->SetColor(c);
            };

            /* Card surface & border */
            D2D1_ROUNDED_RECT crr = MakeRoundedRect(cr, kCardRadius, kCardRadius);
            set_card_col(LerpColor(pal.card_bg, pal.card_bg_hover, ht));
            ctx->FillRoundedRectangle(crr, brush.Get());

            D2D1_RECT_F cbr = MakeRect(cr.left + 0.5f, cr.top + 0.5f, cr.right - 0.5f, cr.bottom - 0.5f);
            set_card_col(is_pinned ? pal.card_border_pinned : pal.card_border);
            ctx->DrawRoundedRectangle(MakeRoundedRect(cbr, kCardRadius - 0.5f, kCardRadius - 0.5f), brush.Get(), is_pinned ? 1.35f : 1.0f);

            /* Multi-pin badge at top-right: always visible when pinned, subtle outline on hover */
            D2D1_COLOR_F accent = GetDeviceAccentColor(d, pal, st.tray_style);
            if (is_pinned || ht > 0.04f) {
                float px = cr.right - 14.0f;
                float py = cr.top + 14.0f;
                if (is_pinned) {
                    set_card_col(accent, 0.94f);
                    DrawPinIcon(g_panel.d2d_factory.Get(), ctx, brush.Get(), px, py, 5.2f, true);
                } else {
                    set_card_col(pal.text_muted, ht * 0.88f);
                    DrawPinIcon(g_panel.d2d_factory.Get(), ctx, brush.Get(), px, py, 5.0f, false);
                }
            }

            /* Battery Visual Widget — adapts smoothly to Octans / Nebula / Pulsar */
            float cx = card_cx;
            float ring_cy = cr.top + 39.0f;
            float ring_r = 25.5f;
            float ring_stroke = 5.4f;
            float frac = (d.battery_percent >= 0) ? ClampF(g_panel.anims[i].display_pct / 100.0f, 0.0f, 1.0f) : 0.0f;

            /* Expanding halo pulse when clicked/pinned */
            if (pulse > 0.01f) {
                float halo_r = ring_r + (1.0f - pulse) * 9.0f;
                set_card_col(accent, pulse * 0.42f);
                ctx->DrawEllipse(MakeEllipse(cx, ring_cy, halo_r, halo_r), brush.Get(), 2.2f * pulse);
            }

            if (st.tray_style == TRAY_STYLE_OCTANS) {
                /* Scheme 1 (Octans, Default): Translucent 3-stop gradient liquid silhouette + gentle dual-layer wave */
                LiquidGradientColors lc = GetDeviceLiquidColors(d, pal);
                float ca = content_alpha * card_alpha;
                float wave_t = IsDeviceMuted(d) ? -1.0f : (g_panel.liquid_wave_sec + static_cast<float>(i) * 0.85f);

                DrawLiquidSilhouetteGlyph(
                    g_panel.d2d_factory.Get(),
                    ctx,
                    g_panel.round_stroke.Get(),
                    d.kind,
                    cx, ring_cy,
                    54.0f,
                    frac,
                    lc.top, lc.mid, lc.bot, lc.wave_back,
                    pal.kit_track, pal.kit_detail_ink,
                    wave_t,
                    ca
                );

                if (d.power_state == OCTANS_PWR_CHARGING || d.power_state == OCTANS_PWR_FULL) {
                    float bx = cx + 19.0f;
                    float by = ring_cy + 18.0f;
                    float br = 8.2f;
                    set_card_col(pal.bolt_badge_bg);
                    ctx->FillEllipse(MakeEllipse(bx, by, br, br), brush.Get());
                    set_card_col(accent);
                    DrawLightningBolt(g_panel.d2d_factory.Get(), ctx, brush.Get(), bx, by, 5.4f);
                }
            } else if (st.tray_style == TRAY_STYLE_PULSAR) {
                /* Scheme 3 (Pulsar): 12-Segment Precision Radial Dial + sleek outline icon */
                ComPtr<ID2D1SolidColorBrush> act_b, trk_b;
                D2D1_COLOR_F ac = accent;
                D2D1_COLOR_F tc = pal.kit_track;
                ac.a *= (content_alpha * card_alpha);
                tc.a *= (content_alpha * card_alpha);
                ctx->CreateSolidColorBrush(ac, act_b.Put());
                ctx->CreateSolidColorBrush(tc, trk_b.Put());
                if (act_b && trk_b) {
                    DrawPrecisionDialArc(
                        ctx,
                        act_b.Get(),
                        trk_b.Get(),
                        g_panel.round_stroke.Get(),
                        cx, ring_cy + 1.0f,
                        19.5f,
                        26.5f,
                        2.85f,
                        frac
                    );
                }

                set_card_col(muted ? pal.glyph_muted : pal.glyph_active);
                DrawDeviceGlyph(
                    g_panel.d2d_factory.Get(),
                    ctx,
                    brush.Get(),
                    d.kind,
                    cx, ring_cy + 1.0f,
                    11.0f,
                    false
                );

                if (d.power_state == OCTANS_PWR_CHARGING || d.power_state == OCTANS_PWR_FULL) {
                    float bx = cx;
                    float by = ring_cy + 23.0f;
                    float br = 7.8f;
                    set_card_col(pal.bolt_badge_bg);
                    ctx->FillEllipse(MakeEllipse(bx, by, br, br), brush.Get());
                    set_card_col(accent);
                    DrawLightningBolt(g_panel.d2d_factory.Get(), ctx, brush.Get(), bx, by, 5.2f);
                }
            } else {
                /* Scheme 2 (Nebula): Classic Circular Battery Ring + sleek outline icon */
                set_card_col(pal.ring_track);
                ctx->DrawEllipse(MakeEllipse(cx, ring_cy, ring_r, ring_r), brush.Get(), ring_stroke);

                if (d.battery_percent >= 0 && frac > 0.005f) {
                    set_card_col(accent);
                    DrawBatteryRingArc(
                        g_panel.d2d_factory.Get(),
                        ctx,
                        brush.Get(),
                        g_panel.round_stroke.Get(),
                        cx, ring_cy,
                        ring_r,
                        ring_stroke,
                        frac,
                        (d.power_state == OCTANS_PWR_CHARGING)
                    );
                }

                set_card_col(muted ? pal.glyph_muted : pal.glyph_active);
                DrawDeviceGlyph(
                    g_panel.d2d_factory.Get(),
                    ctx,
                    brush.Get(),
                    d.kind,
                    cx, ring_cy,
                    11.4f,
                    false
                );

                if (d.power_state == OCTANS_PWR_CHARGING || d.power_state == OCTANS_PWR_FULL) {
                    float bx = cx;
                    float by = ring_cy + ring_r;
                    float br = 8.2f;
                    set_card_col(pal.bolt_badge_bg);
                    ctx->FillEllipse(MakeEllipse(bx, by, br, br), brush.Get());
                    set_card_col(pal.bolt_color);
                    DrawLightningBolt(g_panel.d2d_factory.Get(), ctx, brush.Get(), bx, by, 5.6f);
                }
            }

            /* Percentage (20pt SemiBold Tabular Figures) */
            wchar_t pct_buf[16] = {};
            if (d.power_state == OCTANS_PWR_OFFLINE || d.battery_percent < 0) {
                wcscpy(pct_buf, L"--");
            } else if (d.is_estimated) {
                _snwprintf(pct_buf, 15, L"~%d%%", static_cast<int>(roundf(g_panel.anims[i].display_pct)));
            } else {
                _snwprintf(pct_buf, 15, L"%d%%", static_cast<int>(roundf(g_panel.anims[i].display_pct)));
            }
            D2D1_RECT_F pct_rc = MakeRect(cr.left + 8.0f, cr.top + 70.0f, cr.right - 8.0f, cr.top + 95.0f);
            set_card_col(muted ? pal.text_muted : pal.text_primary);
            DrawTabularText(ctx, pct_buf, g_panel.fmt_percent.Get(), pct_rc, brush.Get());

            /* Single-line ellipsized device name */
            const wchar_t *name = d.name[0] ? d.name : L"\u672a\u77e5\u8bbe\u5907";
            D2D1_RECT_F name_rc = MakeRect(cr.left + 10.0f, cr.top + 95.5f, cr.right - 10.0f, cr.top + 112.5f);
            set_card_col(muted ? pal.text_secondary : pal.text_primary, muted ? 0.85f : 0.94f);
            ctx->DrawText(name, static_cast<UINT32>(wcslen(name)), g_panel.fmt_name.Get(), name_rc, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            /* Refined semantic sub-label */
            wchar_t sub_lbl[48] = {};
            int sem_kind = 0;
            FormatSubLabel(d, sub_lbl, 48, &sem_kind);
            D2D1_RECT_F sub_lbl_rc = MakeRect(cr.left + 8.0f, cr.top + 112.5f, cr.right - 8.0f, cr.top + 127.5f);
            if (sem_kind == 1) {
                set_card_col(pal.ring_low, 0.94f);
            } else if (sem_kind == 2) {
                set_card_col((st.tray_style == TRAY_STYLE_OCTANS && d.power_state == OCTANS_PWR_CHARGING)
                    ? (st.is_dark ? HexColor(0x38BDF8, 1.0f) : HexColor(0x0284C7, 1.0f))
                    : pal.ring_charging, 0.94f);
            } else if (sem_kind == 3) {
                set_card_col(pal.text_muted);
            } else {
                set_card_col(pal.text_secondary);
            }
            ctx->DrawText(sub_lbl, static_cast<UINT32>(wcslen(sub_lbl)), g_panel.fmt_sublabel.Get(), sub_lbl_rc, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

            ctx->SetTransform(base_xf);
        }

        /* Subtle macOS/iOS translucent scroll thumb when >4 devices */
        if (g_panel.max_scroll_y > 1.0f) {
            float view_h = g_panel.grid_hit_rc.bottom - g_panel.grid_hit_rc.top;
            float content_h = view_h + g_panel.max_scroll_y;
            float thumb_h = ClampF(view_h * (view_h / content_h), 26.0f, view_h - 8.0f);
            float thumb_y = g_panel.grid_hit_rc.top + (g_panel.scroll_y / g_panel.max_scroll_y) * (view_h - thumb_h);
            float tx = g_panel.logical_w - 6.0f;
            D2D1_RECT_F tr = MakeRect(tx - 1.5f, thumb_y, tx + 1.5f, thumb_y + thumb_h);
            set_col(pal.scroll_thumb);
            ctx->FillRoundedRectangle(MakeRoundedRect(tr, 1.5f, 1.5f), brush.Get());
        }
    }

    if (need_grid_clip) {
        ctx->PopAxisAlignedClip();
    }

    /* 3b. Clean Exclusive Scanning & Scan-Complete Stage (cards smoothly cross-fade out so zero overlap!) */
    if (g_panel.scan_overlay_t > 0.01f) {
        float ov_a = EaseOutCubic(ClampF(g_panel.scan_overlay_t, 0.0f, 1.0f));
        const D2D1_RECT_F &vr = g_panel.grid_hit_rc;

        float vcx = (vr.left + vr.right) * 0.5f;
        float vcy = (vr.top + vr.bottom) * 0.5f - 14.0f;
        D2D1_RECT_F msg_rc = MakeRect(vr.left + 8.0f, vcy + 44.0f, vr.right - 8.0f, vcy + 68.0f);

        if (st.scan_phase == SCAN_COMPLETE || g_panel.scan_done_sec > 0.001f) {
            DrawScanCompleteAnim(
                g_panel.d2d_factory.Get(),
                ctx,
                g_panel.round_stroke.Get(),
                vcx, vcy,
                22.0f,
                st.is_dark,
                g_panel.scan_done_sec / 0.75f,
                content_alpha * ov_a
            );

            wchar_t done_lbl[64] = {};
            if (st.device_count > 0) {
                _snwprintf(done_lbl, 63, L"\u626b\u63cf\u5b8c\u6210 \u00b7 \u53d1\u73b0 %u \u53f0\u8bbe\u5907", static_cast<unsigned>(st.device_count));
            } else {
                wcscpy(done_lbl, L"\u626b\u63cf\u5b8c\u6210 \u00b7 \u6682\u672a\u53d1\u73b0\u65e0\u7ebf\u5916\u8bbe");
            }
            float rev = EaseOutCubic(ClampF(g_panel.scan_done_sec / 0.45f, 0.0f, 1.0f));
            set_col(st.is_dark ? HexColor(0x34D399, 0.98f) : HexColor(0x059669, 0.98f), ov_a);
            DrawProgressiveRevealText(ctx, done_lbl, g_panel.fmt_empty.Get(), msg_rc, brush.Get(), rev);
        } else {
            DrawCelestialScanAnim(
                g_panel.d2d_factory.Get(),
                ctx,
                g_panel.round_stroke.Get(),
                vcx, vcy,
                21.0f,
                st.is_dark,
                g_panel.scan_elapsed_sec,
                content_alpha * ov_a
            );

            /* 5 rotating reassuring phrases: 0.72s left-to-right reveal + 3.00s steady hold + 0.38s fade */
            static const wchar_t *kScanPhrases[5] = {
                L"\u6b63\u5728\u63a2\u6d4b\u9644\u8fd1\u7684 2.4G \u4e0e\u84dd\u7259\u8bbe\u5907",
                L"\u5168\u76d8\u534f\u8bae\u5339\u914d\u4e2d\uff0c\u8bf7\u7a0d\u5019\u7247\u523b",
                L"\u626b\u63cf\u7565\u5fae\u8017\u65f6\uff0c\u8bf7\u8010\u5fc3\u7b49\u5f85",
                L"\u6b63\u5728\u5524\u9192\u5e76\u8bfb\u53d6\u5916\u8bbe\u7535\u91cf\u901a\u9053",
                L"\u6b63\u5728\u6821\u9a8c\u591a\u901a\u9053\u65e0\u7ebf\u5916\u8bbe\u4fe1\u53f7"
            };
            static constexpr float kPhrasePeriod = 4.10f;
            int p_idx = static_cast<int>(g_panel.scan_elapsed_sec / kPhrasePeriod) % 5;
            float local_t = fmodf(g_panel.scan_elapsed_sec, kPhrasePeriod);
            float reveal_f = EaseOutCubic(ClampF(local_t / 0.72f, 0.0f, 1.0f));
            float fade_a = (local_t > 3.72f)
                ? ClampF(1.0f - (local_t - 3.72f) / 0.38f, 0.0f, 1.0f)
                : ClampF(0.35f + 0.65f * reveal_f, 0.0f, 1.0f);

            set_col(pal.text_secondary, ov_a * fade_a * 0.96f);
            DrawProgressiveRevealText(ctx, kScanPhrases[p_idx], g_panel.fmt_name.Get(), msg_rc, brush.Get(), reveal_f);
        }
    }

    /* 4. Zero-Occlusion Integrated Bottom Detail Footer (cleanly below cards, never overlaps!) */
    {
        const D2D1_RECT_F &fr = g_panel.footer_rc;
        set_col(pal.tooltip_bg);
        ctx->FillRoundedRectangle(MakeRoundedRect(fr, 10.0f, 10.0f), brush.Get());
        set_col(pal.tooltip_border);
        ctx->DrawRoundedRectangle(
            MakeRoundedRect(MakeRect(fr.left + 0.5f, fr.top + 0.5f, fr.right - 0.5f, fr.bottom - 0.5f), 9.5f, 9.5f),
            brush.Get(), 1.0f
        );

        D2D1_RECT_F ftr = MakeRect(fr.left + 10.0f, fr.top, fr.right - 10.0f, fr.bottom);
        bool show_detail = (st.scan_phase == SCAN_IDLE &&
                            g_panel.tooltip_card >= 0 &&
                            static_cast<size_t>(g_panel.tooltip_card) < st.device_count &&
                            g_panel.tooltip_t > 0.02f);

        if (show_detail) {
            const octans_device_view_t &td = st.devices[g_panel.tooltip_card];
            wchar_t tip_text[160] = {};
            FormatTooltipDetail(td, tip_text, 160);
            set_col(pal.text_primary, ClampF(g_panel.tooltip_t, 0.0f, 1.0f) * 0.92f);
            ctx->DrawText(tip_text, static_cast<UINT32>(wcslen(tip_text)), g_panel.fmt_tooltip.Get(), ftr, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            wchar_t idle_hint[96] = {};
            size_t active_pins = CountActivePinnedDevices(st);
            if (st.scan_phase == SCAN_ACTIVE) {
                wcscpy(idle_hint, L"\u6b63\u5728\u5168\u76d8\u626b\u63cf 2.4G / \u84dd\u7259 / HID \u8bbe\u5907");
            } else if (st.scan_phase == SCAN_COMPLETE) {
                wcscpy(idle_hint, L"\u626b\u63cf\u5b8c\u6210 \u00b7 \u5373\u5c06\u5448\u73b0\u5b9e\u65f6\u7535\u91cf");
            } else if (st.device_count == 0) {
                wcscpy(idle_hint, L"\u652f\u6301 2.4G \u63a5\u6536\u5668 \u00b7 \u84dd\u7259\u5916\u8bbe \u00b7 \u6807\u51c6 HID \u8bbe\u5907");
            } else if (active_pins > 0) {
                _snwprintf(idle_hint, 95, L"\u5df2\u56fa\u5b9a %u \u53f0\u5230\u4efb\u52a1\u680f \u00b7 \u60ac\u505c\u5361\u7247\u67e5\u770b\u8be6\u60c5", static_cast<unsigned>(active_pins));
            } else {
                wcscpy(idle_hint, L"\u70b9\u51fb\u8bbe\u5907\u56fa\u5b9a\u5230\u4efb\u52a1\u680f \u00b7 \u60ac\u505c\u5361\u7247\u67e5\u770b\u8be6\u60c5");
            }
            set_col(pal.text_muted, 1.0f - ClampF(g_panel.tooltip_t, 0.0f, 1.0f));
            ctx->DrawText(idle_hint, static_cast<UINT32>(wcslen(idle_hint)), g_panel.fmt_tooltip.Get(), ftr, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    /* 5. Hover Micro-Tooltip Pill for Top-Right Refresh Button ("立即刷新电量") */
    if (g_panel.refresh_tip_t > 0.02f) {
        float rt_a = ClampF(g_panel.refresh_tip_t, 0.0f, 1.0f);
        float tip_w = 78.0f;
        float tip_h = 22.0f;
        float rcx = (g_panel.refresh_btn_rc.left + g_panel.refresh_btn_rc.right) * 0.5f;
        float tl = ClampF(rcx - tip_w * 0.5f, kPanelPadX, g_panel.logical_w - kPanelPadX - tip_w);
        float tt = g_panel.refresh_btn_rc.bottom + 4.0f - (1.0f - rt_a) * 3.0f;
        D2D1_RECT_F trc = MakeRect(tl, tt, tl + tip_w, tt + tip_h);
        D2D1_ROUNDED_RECT trr = MakeRoundedRect(trc, 7.0f, 7.0f);

        set_col(st.is_dark ? HexColor(0x2A2A30, 0.96f) : HexColor(0xFFFFFF, 0.96f), rt_a);
        ctx->FillRoundedRectangle(trr, brush.Get());
        set_col(pal.panel_outer_border, rt_a);
        ctx->DrawRoundedRectangle(trr, brush.Get(), 0.9f);
        set_col(pal.text_primary, rt_a * 0.92f);
        ctx->DrawText(L"\u7acb\u5373\u5237\u65b0\u7535\u91cf", 6, g_panel.fmt_tooltip.Get(), trc, brush.Get());
    }

    HRESULT hr = ctx->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseGraphicsResources();
        return;
    }

    if (is_exiting) {
        LONG_PTR ex = GetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE);
        if ((ex & WS_EX_LAYERED) == 0) {
            SetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        }
        RECT wr = {};
        GetWindowRect(g_panel.hwnd, &wr);
        POINT pt_dst = { wr.left, wr.top };
        SIZE sz_win = { static_cast<LONG>(px_w), static_cast<LONG>(px_h) };
        POINT pt_src = { 0, 0 };
        BLENDFUNCTION bf = {};
        bf.BlendOp = AC_SRC_OVER;
        bf.SourceConstantAlpha = 255;
        bf.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(g_panel.hwnd, nullptr, &pt_dst, &sz_win, g_panel.mem_dc, &pt_src, 0, &bf, ULW_ALPHA);
    } else {
        HDC dst_dc = paint_dc ? paint_dc : GetDC(g_panel.hwnd);
        if (dst_dc) {
            BitBlt(dst_dc, 0, 0, static_cast<int>(px_w), static_cast<int>(px_h), g_panel.mem_dc, 0, 0, SRCCOPY);
            if (!paint_dc) ReleaseDC(g_panel.hwnd, dst_dc);
        }
    }
}

static void StepAnimations() {
    LARGE_INTEGER now_qpc = {};
    QueryPerformanceCounter(&now_qpc);
    float dt = 0.016f;
    if (g_panel.qpc_freq.QuadPart > 0 && g_panel.last_qpc.QuadPart > 0 &&
        now_qpc.QuadPart >= g_panel.last_qpc.QuadPart) {
        dt = static_cast<float>(static_cast<double>(now_qpc.QuadPart - g_panel.last_qpc.QuadPart) /
                                static_cast<double>(g_panel.qpc_freq.QuadPart));
    }
    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.05f)  dt = 0.05f;
    g_panel.last_qpc = now_qpc;

    bool any_active = false;

    /* 1. Panel open (240ms Apple spring-quint) vs. smooth layered shrink-to-tray exit (155ms ease-in) */
    if (!g_panel.want_visible) {
        g_panel.exit_u += dt * (1.0f / 0.155f);
        if (g_panel.exit_u >= 1.0f) {
            g_panel.exit_u = 1.0f;
            g_panel.panel_t = 0.0f;
            g_panel.exiting_layered = false;
            ShowWindow(g_panel.hwnd, SW_HIDE);
            LONG_PTR ex = GetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE);
            if ((ex & WS_EX_LAYERED) != 0) {
                SetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            }
            StopAnimTimer();
            ReleaseGraphicsResources();
            SetTimer(g_panel.hwnd, TIMER_IDLE_TRIM, 1200u, nullptr);
            return;
        }
        RenderPanel();
        return;
    }

    if (fabsf(g_panel.panel_t - 1.0f) > 0.002f) {
        g_panel.panel_t = ClampF(g_panel.panel_t + dt * (1.0f / 0.24f), 0.0f, 1.0f);
        ApplyWindowBoundsForAnim();
        any_active = true;
    } else {
        g_panel.panel_t = 1.0f;
    }

    /* 2. Per-card staggered entrance, bounded ease-out battery ring sweep, hover lift & click pulse */
    const UiState &st = GetState();
    float hover_alpha = 1.0f - expf(-dt * 16.0f);
    for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
        CardAnim &ca = g_panel.anims[i];

        if (ca.enter_t < 1.0f) {
            ca.enter_t += dt * (1.0f / 0.30f);
            if (ca.enter_t > 1.0f) ca.enter_t = 1.0f;
            any_active = true;
        }

        if (ca.enter_t > 0.02f && ca.pct_t < 1.0f) {
            float dur = (ca.pct_dur > 0.05f) ? ca.pct_dur : 0.52f;
            ca.pct_t += dt / dur;
            if (ca.pct_t >= 1.0f) {
                ca.pct_t = 1.0f;
                ca.display_pct = ca.target_pct;
            } else {
                ca.display_pct = LerpF(ca.pct_start, ca.target_pct, EaseOutCubic(ca.pct_t));
                any_active = true;
            }
        } else if (ca.enter_t >= 1.0f && ca.pct_t >= 1.0f) {
            ca.display_pct = ca.target_pct;
        }

        float ht_target = (static_cast<int>(i) == g_panel.hover_card) ? 1.0f : 0.0f;
        if (fabsf(ca.hover_t - ht_target) > 0.008f) {
            ca.hover_t = LerpF(ca.hover_t, ht_target, hover_alpha);
            any_active = true;
        } else {
            ca.hover_t = ht_target;
        }

        if (ca.pulse_t > 0.005f) {
            ca.pulse_t -= dt * (1.0f / 0.34f);
            if (ca.pulse_t < 0.0f) ca.pulse_t = 0.0f;
            any_active = true;
        } else {
            ca.pulse_t = 0.0f;
        }
    }

    /* 3. Smooth inertial wheel scrolling */
    if (fabsf(g_panel.scroll_y - g_panel.target_scroll_y) > 0.25f) {
        g_panel.scroll_y = LerpF(g_panel.scroll_y, g_panel.target_scroll_y, 1.0f - expf(-dt * 14.0f));
        any_active = true;
    } else {
        g_panel.scroll_y = g_panel.target_scroll_y;
    }

    /* 4. Tooltip smooth fade & refresh micro-tooltip fade */
    float tip_target = (g_panel.tooltip_card >= 0) ? 1.0f : 0.0f;
    if (fabsf(g_panel.tooltip_t - tip_target) > 0.01f) {
        g_panel.tooltip_t = LerpF(g_panel.tooltip_t, tip_target, 1.0f - expf(-dt * 15.0f));
        any_active = true;
    } else {
        g_panel.tooltip_t = tip_target;
    }

    float ref_tip_target = (g_panel.hover_header_btn == 1) ? 1.0f : 0.0f;
    if (fabsf(g_panel.refresh_tip_t - ref_tip_target) > 0.01f) {
        g_panel.refresh_tip_t = LerpF(g_panel.refresh_tip_t, ref_tip_target, 1.0f - expf(-dt * 18.0f));
        any_active = true;
    } else {
        g_panel.refresh_tip_t = ref_tip_target;
    }

    /* 5. Brand writing entrance, frosted scan overlay, completion bloom & smooth refresh feedback */
    float ov_target = (st.scan_phase != SCAN_IDLE) ? 1.0f : 0.0f;
    if (fabsf(g_panel.scan_overlay_t - ov_target) > 0.005f) {
        float ov_speed = (ov_target > g_panel.scan_overlay_t) ? (1.0f / 0.24f) : (1.0f / 0.34f);
        if (ov_target > g_panel.scan_overlay_t) {
            g_panel.scan_overlay_t = ClampF(g_panel.scan_overlay_t + dt * ov_speed, 0.0f, 1.0f);
        } else {
            g_panel.scan_overlay_t = ClampF(g_panel.scan_overlay_t - dt * ov_speed, 0.0f, 1.0f);
        }
        any_active = true;
    } else {
        g_panel.scan_overlay_t = ov_target;
        if (ov_target <= 0.0f) {
            g_panel.scan_done_sec = 0.0f;
        }
    }

    if (st.scan_phase == SCAN_ACTIVE) {
        g_panel.scan_elapsed_sec += dt;
        g_panel.scan_done_sec = 0.0f;
        any_active = true;
    } else if (st.scan_phase == SCAN_COMPLETE) {
        g_panel.scan_done_sec += dt;
        any_active = true;
    }

    if (g_panel.brand_anim_t < 1.0f) {
        g_panel.brand_anim_t += dt * (1.0f / 0.72f);
        if (g_panel.brand_anim_t >= 1.0f) {
            g_panel.brand_anim_t = 1.0f;
        } else {
            any_active = true;
        }
    }
    if (g_panel.refresh_spinning || st.refresh_pending) {
        g_panel.refresh_spin_deg += dt * 540.0f;
        if (g_panel.refresh_spin_deg >= 360.0f) {
            if (st.refresh_pending) {
                g_panel.refresh_spin_deg = fmodf(g_panel.refresh_spin_deg, 360.0f);
                any_active = true;
            } else {
                g_panel.refresh_spin_deg = 0.0f;
                g_panel.refresh_spinning = false;
            }
        } else {
            any_active = true;
        }
    }
    if (g_panel.refresh_done_active) {
        g_panel.refresh_done_sec += dt;
        if (g_panel.refresh_done_sec >= 1.78f) {
            g_panel.refresh_done_active = false;
            g_panel.refresh_done_sec = 0.0f;
        } else {
            any_active = true;
        }
    }

    /* 6. Gentle flowing dual-layer water wave for Octans (default liquid) style while panel is open */
    bool liquid_wave_active = false;
    if (st.tray_style == TRAY_STYLE_OCTANS && st.device_count > 0 && st.scan_phase == SCAN_IDLE) {
        for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
            if (!IsDeviceMuted(st.devices[i]) && st.devices[i].battery_percent > 0 && st.devices[i].battery_percent < 100) {
                liquid_wave_active = true;
                break;
            }
        }
    }
    if (liquid_wave_active) {
        g_panel.liquid_wave_sec = fmodf(g_panel.liquid_wave_sec + dt, 212.8f);
    }

    RenderPanel();

    /* Demand-driven idle stop & gentle 30fps pacing for liquid wave (0% CPU as soon as panel hides!) */
    if (any_active) {
        if (!g_panel.anim_fast_mode) {
            StartAnimTimer(true);
        }
    } else if (liquid_wave_active) {
        if (g_panel.anim_fast_mode) {
            StartAnimTimer(false);
        }
    } else {
        StopAnimTimer();
    }
}

static void HitTestAt(float dip_x, float dip_y, int *out_card, int *out_header_btn) {
    *out_card = -1;
    *out_header_btn = 0;

    if (PtInD2DRect(g_panel.refresh_btn_rc, dip_x, dip_y)) {
        *out_header_btn = 1;
        return;
    }
    if (PtInD2DRect(g_panel.more_btn_rc, dip_x, dip_y)) {
        *out_header_btn = 2;
        return;
    }
    if (PtInD2DRect(g_panel.grid_hit_rc, dip_x, dip_y)) {
        const UiState &st = GetState();
        if (st.scan_phase != SCAN_IDLE) {
            return; /* Frosted scan overlay blocks card interaction while scanning */
        }
        if (st.device_count == 0 && !(st.demo_hooks.on_action != nullptr) &&
            PtInD2DRect(g_panel.discover_btn_rc, dip_x, dip_y)) {
            *out_header_btn = 3;
            return;
        }
        for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
            if (PtInD2DRect(g_panel.card_rects[i], dip_x, dip_y)) {
                *out_card = static_cast<int>(i);
                return;
            }
        }
    }
}

static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                bool interactive = (g_panel.hover_card >= 0 || g_panel.hover_header_btn != 0);
                SetCursor(LoadCursorW(nullptr, interactive ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_TIMER:
            if (wp == TIMER_ANIM) {
                StepAnimations();
                return 0;
            }
            if (wp == TIMER_HOVER) {
                KillTimer(hwnd, TIMER_HOVER);
                if (g_panel.hover_card >= 0) {
                    g_panel.tooltip_card = g_panel.hover_card;
                    StartAnimTimer();
                }
                return 0;
            }
            if (wp == TIMER_IDLE_TRIM) {
                KillTimer(hwnd, TIMER_IDLE_TRIM);
                if (!g_panel.want_visible && !IsWindowVisible(hwnd)) {
                    ReleaseAllIdleResources();
                    TrayReleaseRenderCache();
                    TrimIdleWorkingSet();
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE: {
            if (!g_panel.mouse_tracked) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_panel.mouse_tracked = true;
            }
            float scale = static_cast<float>(g_panel.dpi) / 96.0f;
            float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
            float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;

            int new_card = -1;
            int new_btn = 0;
            HitTestAt(dx, dy, &new_card, &new_btn);

            if (new_card != g_panel.hover_card || new_btn != g_panel.hover_header_btn) {
                g_panel.hover_card = new_card;
                g_panel.hover_header_btn = new_btn;

                SetCursor(LoadCursorW(nullptr, (new_card >= 0 || new_btn != 0) ? IDC_HAND : IDC_ARROW));

                KillTimer(hwnd, TIMER_HOVER);
                if (new_card >= 0) {
                    /* Dedicated bottom footer never occludes cards, so update immediately on hover! */
                    g_panel.tooltip_card = new_card;
                } else {
                    g_panel.tooltip_card = -1;
                }
                StartAnimTimer();
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_panel.mouse_tracked = false;
            KillTimer(hwnd, TIMER_HOVER);
            if (g_panel.hover_card != -1 || g_panel.hover_header_btn != 0 || g_panel.tooltip_card != -1) {
                g_panel.hover_card = -1;
                g_panel.hover_header_btn = 0;
                g_panel.tooltip_card = -1;
                StartAnimTimer();
            }
            return 0;

        case WM_LBUTTONUP: {
            float scale = static_cast<float>(g_panel.dpi) / 96.0f;
            float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
            float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;

            int card = -1;
            int btn = 0;
            HitTestAt(dx, dy, &card, &btn);

            if (btn == 1) {
                ActionTriggerRefresh();
            } else if (btn == 2) {
                POINT pt = {
                    static_cast<LONG>(roundf(g_panel.more_btn_rc.right * scale)),
                    static_cast<LONG>(roundf(g_panel.more_btn_rc.bottom * scale))
                };
                ClientToScreen(hwnd, &pt);
                ActionShowContextMenu(pt.x, pt.y, -1, MENU_MODE_PANEL_GENERAL);
            } else if (btn == 3) {
                ActionTriggerFullScan();
            } else if (card >= 0) {
                const UiState &st = GetState();
                if (static_cast<size_t>(card) < st.device_count) {
                    g_panel.anims[card].pulse_t = 1.0f;
                    ActionTogglePinDevice(st.devices[card].id);
                    StartAnimTimer();
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            float scale = static_cast<float>(g_panel.dpi) / 96.0f;
            float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
            float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
            int card = -1;
            int btn = 0;
            HitTestAt(dx, dy, &card, &btn);

            POINT pt = { static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)) };
            ClientToScreen(hwnd, &pt);
            MenuMode mode = (card >= 0) ? MENU_MODE_PANEL_DEVICE : MENU_MODE_PANEL_GENERAL;
            ActionShowContextMenu(pt.x, pt.y, card, mode);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (g_panel.max_scroll_y > 0.0f) {
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                g_panel.target_scroll_y = ClampF(
                    g_panel.target_scroll_y - (static_cast<float>(delta) / 120.0f) * 56.0f,
                    0.0f,
                    g_panel.max_scroll_y
                );
                StartAnimTimer();
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) {
                PanelHide();
                return 0;
            }
            if (wp == VK_F5) {
                ActionTriggerRefresh();
                return 0;
            }
            UiState &st = GetState();
            if (st.demo_hooks.handle_key && st.demo_hooks.handle_key(wp, st.demo_hooks.user_ctx)) {
                return 0;
            }
            break;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wp) == WA_INACTIVE && !GetState().modal_open && g_panel.want_visible) {
                /* Guard against taskbar focus bounce within 160ms of opening the flyout */
                if ((GetTickCount64() - g_panel.last_show_ms) >= 160ULL) {
                    GetState().last_inactive_hide_ms = GetTickCount64();
                    PanelHide();
                }
            }
            return 0;
        }

        case WM_DPICHANGED: {
            g_panel.dpi = HIWORD(wp);
            if (g_panel.has_anchor) {
                ComputeTargetPosition(g_panel.anchor_x, g_panel.anchor_y);
                ApplyWindowBoundsForAnim();
            }
            RenderPanel();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (g_panel.panel_t > 0.01f && !g_panel.exiting_layered) {
                RenderPanel(hdc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool PanelInit() {
    static const wchar_t *kPanelClass = L"OctansFlyoutWnd";
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PanelWndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    RegisterClassExW(&wc);

    g_panel.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kPanelClass,
        L"Octans",
        WS_POPUP,
        0, 0, 342, 338,
        nullptr,
        nullptr,
        hinst,
        nullptr
    );
    if (!g_panel.hwnd) return false;

    DisableWindowIme(g_panel.hwnd);
    g_panel.dpi = GetWindowDpiSafe(g_panel.hwnd);
    g_panel.backdrop_supported = ApplyWin11Backdrop(g_panel.hwnd, GetState().is_dark);
    ComputeLayoutMetrics();
    return true;
}

void PanelShutdown() {
    StopAnimTimer();
    if (g_panel.hwnd) {
        KillTimer(g_panel.hwnd, TIMER_HOVER);
        KillTimer(g_panel.hwnd, TIMER_IDLE_TRIM);
    }
    ReleaseAllIdleResources();

    if (g_panel.hwnd) {
        DestroyWindow(g_panel.hwnd);
        g_panel.hwnd = nullptr;
    }
}

void PanelShow(int anchor_x, int anchor_y, bool toggle_if_visible) {
    if (!g_panel.hwnd) return;
    if (toggle_if_visible && g_panel.want_visible && IsWindowVisible(g_panel.hwnd)) {
        PanelHide();
        return;
    }

    KillTimer(g_panel.hwnd, TIMER_IDLE_TRIM);
    LONG_PTR ex = GetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) != 0) {
        SetWindowLongPtrW(g_panel.hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }

    ComputeLayoutMetrics();
    ComputeTargetPosition(anchor_x, anchor_y);
    g_panel.backdrop_supported = ApplyWin11Backdrop(g_panel.hwnd, GetState().is_dark);

    bool opening_fresh = !IsWindowVisible(g_panel.hwnd) || !g_panel.want_visible;
    g_panel.want_visible = true;
    g_panel.exiting_layered = false;
    g_panel.exit_u = 0.0f;
    g_panel.last_show_ms = GetTickCount64();

    if (opening_fresh) {
        g_panel.panel_t = 0.02f;
        g_panel.brand_anim_t = 0.0f;
        /* Stagger cards & sweep battery rings crisply from 0% up to target in 520ms (no asymptotic tail) */
        const UiState &st = GetState();
        for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
            g_panel.anims[i].enter_t = -0.07f * static_cast<float>(i);
            g_panel.anims[i].pct_start = 0.0f;
            g_panel.anims[i].display_pct = 0.0f;
            g_panel.anims[i].pct_t = 0.0f;
            g_panel.anims[i].pct_dur = 0.52f;
            g_panel.anims[i].hover_t = 0.0f;
            g_panel.anims[i].pulse_t = 0.0f;
        }
        ApplyWindowBoundsForAnim();
        ShowWindow(g_panel.hwnd, SW_SHOW);
        RenderPanel();
    } else {
        ApplyWindowBoundsForAnim();
    }
    SetForegroundWindow(g_panel.hwnd);
    StartAnimTimer(true);
}

void PanelHide() {
    if (!g_panel.hwnd) return;
    if (!g_panel.want_visible && !IsWindowVisible(g_panel.hwnd)) return;
    g_panel.want_visible = false;
    g_panel.exiting_layered = true;
    g_panel.exit_u = 0.0f;
    g_panel.hover_card = -1;
    g_panel.hover_header_btn = 0;
    g_panel.tooltip_card = -1;
    KillTimer(g_panel.hwnd, TIMER_HOVER);

    /* Release DWM fixed-rect Acrylic right before layered shrink-to-tray so card shrinks with zero double box! */
    ClearWin11BackdropForExit(g_panel.hwnd);
    RenderPanel();
    StartAnimTimer(true);
}

bool PanelIsVisible() {
    return g_panel.want_visible && g_panel.hwnd && IsWindowVisible(g_panel.hwnd);
}

void PanelOnDevicesChanged(const octans_device_view_t *devices, size_t count) {
    if (count > MAX_DEVICES) count = MAX_DEVICES;

    /* Preserve smooth animation progress by matching device IDs across snapshots */
    CardAnim old_anims[MAX_DEVICES];
    memcpy(old_anims, g_panel.anims, sizeof(old_anims));
    size_t old_init_count = 0;
    for (size_t j = 0; j < MAX_DEVICES; ++j) {
        if (old_anims[j].initialized) ++old_init_count;
    }

    for (size_t i = 0; i < count; ++i) {
        CardAnim next = {};
        strncpy(next.id, devices[i].id, sizeof(next.id) - 1);
        float tgt = (devices[i].battery_percent >= 0)
            ? ClampF(static_cast<float>(devices[i].battery_percent), 0.0f, 100.0f)
            : 0.0f;
        next.target_pct = tgt;
        next.pct_start = (old_init_count == 0) ? 0.0f : tgt;
        next.display_pct = (old_init_count == 0) ? 0.0f : tgt;
        next.pct_t = (old_init_count == 0) ? 0.0f : 1.0f;
        next.pct_dur = (old_init_count == 0) ? 0.56f : 0.42f;
        next.hover_t = 0.0f;
        next.enter_t = (old_init_count == 0) ? (-0.10f * static_cast<float>(i)) : 1.0f;
        next.pulse_t = 0.0f;
        next.initialized = true;

        for (size_t j = 0; j < MAX_DEVICES; ++j) {
            if (old_anims[j].initialized && old_anims[j].id[0] != '\0' &&
                strcmp(old_anims[j].id, next.id) == 0) {
                next.display_pct = old_anims[j].display_pct;
                next.hover_t = old_anims[j].hover_t;
                next.enter_t = old_anims[j].enter_t;
                next.pulse_t = old_anims[j].pulse_t;
                if (fabsf(old_anims[j].target_pct - tgt) > 0.1f) {
                    next.pct_start = old_anims[j].display_pct;
                    next.pct_t = 0.0f;
                    next.pct_dur = 0.42f;
                } else {
                    next.pct_start = old_anims[j].pct_start;
                    next.pct_t = old_anims[j].pct_t;
                    next.pct_dur = old_anims[j].pct_dur;
                }
                break;
            }
        }
        g_panel.anims[i] = next;
    }
    for (size_t i = count; i < MAX_DEVICES; ++i) {
        g_panel.anims[i] = {};
    }

    float prev_w = g_panel.logical_w;
    float prev_h = g_panel.logical_h;
    ComputeLayoutMetrics();

    if (g_panel.want_visible && g_panel.hwnd && IsWindowVisible(g_panel.hwnd)) {
        if ((fabsf(prev_w - g_panel.logical_w) > 0.5f || fabsf(prev_h - g_panel.logical_h) > 0.5f) &&
            g_panel.has_anchor) {
            ComputeTargetPosition(g_panel.anchor_x, g_panel.anchor_y);
            ApplyWindowBoundsForAnim();
        }
        StartAnimTimer();
    }
}

void PanelOnPinnedChanged() {
    if (PanelIsVisible()) {
        RenderPanel();
    }
}

void PanelOnStyleChanged() {
    if (PanelIsVisible()) {
        RenderPanel();
        if (GetState().tray_style == TRAY_STYLE_OCTANS) {
            StartAnimTimer(false);
        }
    }
}

void PanelOnScanStateChanged() {
    const UiState &st = GetState();
    if (st.scan_phase == SCAN_ACTIVE) {
        g_panel.scan_elapsed_sec = 0.0f;
        g_panel.scan_done_sec = 0.0f;
        g_panel.hover_card = -1;
        g_panel.tooltip_card = -1;
        g_panel.hover_header_btn = 0;
    } else if (st.scan_phase == SCAN_COMPLETE) {
        g_panel.scan_done_sec = 0.0f;
    } else if (st.scan_phase == SCAN_IDLE && st.device_count > 0) {
        /* Staggered card rise as scan completion fades cleanly into the live cards */
        for (size_t i = 0; i < st.device_count && i < MAX_DEVICES; ++i) {
            g_panel.anims[i].enter_t = -0.07f * static_cast<float>(i);
        }
    }
    float prev_w = g_panel.logical_w;
    float prev_h = g_panel.logical_h;
    ComputeLayoutMetrics();
    if (PanelIsVisible()) {
        if ((fabsf(prev_w - g_panel.logical_w) > 0.5f || fabsf(prev_h - g_panel.logical_h) > 0.5f) &&
            g_panel.has_anchor) {
            ComputeTargetPosition(g_panel.anchor_x, g_panel.anchor_y);
            ApplyWindowBoundsForAnim();
        }
        StartAnimTimer();
    }
}

void PanelOnThemeChanged(bool is_dark) {
    if (!g_panel.hwnd) return;
    g_panel.backdrop_supported = ApplyWin11Backdrop(g_panel.hwnd, is_dark);
    if (PanelIsVisible()) {
        RenderPanel();
    }
}

void PanelTriggerRefreshSpin() {
    g_panel.refresh_spinning = true;
    g_panel.refresh_spin_deg = 0.0f;
    if (PanelIsVisible()) {
        StartAnimTimer();
    }
}

void PanelOnRefreshCompleted() {
    g_panel.refresh_done_active = true;
    g_panel.refresh_done_sec = 0.0f;
    if (PanelIsVisible()) {
        StartAnimTimer();
    }
}

HWND PanelGetHwnd() {
    return g_panel.hwnd;
}

} /* namespace octans_ui */
