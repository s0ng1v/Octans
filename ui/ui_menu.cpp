#include "ui_menu.h"
#include "ui_theme.h"
#include "ui_glyphs.h"
#include "ui_fonts.h"
#include "ui_panel.h"
#include <stdio.h>

namespace octans_ui {

static constexpr UINT_PTR TIMER_MENU_ANIM = 1;

enum HitZone : int {
    HIT_NONE = 0,
    HIT_REFRESH,
    HIT_SCAN_DEVICES,
    HIT_PIN_DEVICE,
    HIT_RENAME_DEVICE,
    HIT_DELETE_DEVICE,
    HIT_SEG_OCTANS,
    HIT_SEG_NEBULA,
    HIT_SEG_PULSAR,
    HIT_SLIDER,
    HIT_AUTOSTART,
    HIT_DEMO_DRAIN,
    HIT_DEMO_CHARGE,
    HIT_DEMO_THEME,
    HIT_DEMO_COUNT,
    HIT_ABOUT,
    HIT_QUIT
};

struct MenuState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    UINT dpi = 96;
    MenuMode mode = MENU_MODE_TRAY;
    float logical_w = 256.0f;
    float logical_h = 320.0f;
    int device_index = -1;

    bool has_refresh = false;
    bool has_scan_devices = false;
    bool has_device_pin = false;
    bool has_rename = false;
    bool has_delete_device = false;
    bool has_general = false;
    bool has_demo = false;

    uint64_t created_ms = 0;
    bool done = false;
    bool dismissed_by_outside = false;
    bool dragging_slider = false;
    bool mouse_tracked = false;
    int hover_zone = HIT_NONE;
    int pending_post_action = 0; /* 1=rename, 2=quit, 3=full_scan, 4=delete, 5=about */

    /* Smooth animations (3-way segmented pill slide + autostart pill switch) */
    bool anim_active = false;
    LARGE_INTEGER qpc_freq = {};
    LARGE_INTEGER qpc_last = {};
    float seg_pos = 0.0f;         /* 0.0 = Octans, 1.0 = Nebula, 2.0 = Pulsar */
    float seg_target = 0.0f;
    float auto_pos = 0.0f;        /* 0.0 = Off, 1.0 = On */
    float auto_target = 0.0f;

    /* Layout rects (DIPs) */
    D2D1_RECT_F rc_refresh = {};
    D2D1_RECT_F rc_scan_devices = {};
    D2D1_RECT_F rc_pin = {};
    D2D1_RECT_F rc_rename = {};
    float div_dev_del_y = 0.0f;
    D2D1_RECT_F rc_delete = {};
    float div1_y = 0.0f;
    float style_label_y = 0.0f;
    D2D1_RECT_F rc_seg_wrap = {};
    D2D1_RECT_F rc_seg[3] = {};
    D2D1_RECT_F rc_slider_hit = {};
    D2D1_RECT_F rc_slider_track = {};
    float slider_label_y = 0.0f;
    float div2_y = 0.0f;
    D2D1_RECT_F rc_autostart = {};
    float div3_y = 0.0f;
    float demo_label_y = 0.0f;
    D2D1_RECT_F rc_demo[4] = {};
    float div4_y = 0.0f;
    D2D1_RECT_F rc_about = {};
    D2D1_RECT_F rc_quit = {};

    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<ID2D1DCRenderTarget> rt;
    ComPtr<ID2D1StrokeStyle> round_stroke;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> fmt_item;
    ComPtr<IDWriteTextFormat> fmt_small;
    ComPtr<IDWriteTextFormat> fmt_seg;
    ComPtr<IDWriteTextFormat> fmt_right;
};

static bool PtInR(const D2D1_RECT_F &r, float x, float y) {
    return (r.right > r.left) && (r.bottom > r.top) &&
           x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static D2D1_COLOR_F LerpColor(const D2D1_COLOR_F &a, const D2D1_COLOR_F &b, float t) {
    float k = ClampF(t, 0.0f, 1.0f);
    return MakeColor(
        LerpF(a.r, b.r, k),
        LerpF(a.g, b.g, k),
        LerpF(a.b, b.b, k),
        LerpF(a.a, b.a, k)
    );
}

static void LayoutMenu(MenuState &m) {
    const UiState &st = GetState();
    bool valid_dev = (m.device_index >= 0 && static_cast<size_t>(m.device_index) < st.device_count);

    if (m.mode == MENU_MODE_PANEL_DEVICE) {
        m.logical_w         = 200.0f;
        m.has_refresh       = false;
        m.has_scan_devices  = false;
        m.has_device_pin    = valid_dev;
        m.has_rename        = valid_dev;
        m.has_delete_device = valid_dev;
        m.has_general       = false;
        m.has_demo          = false;
    } else if (m.mode == MENU_MODE_PANEL_GENERAL) {
        m.logical_w         = 256.0f;
        m.has_refresh       = true;
        m.has_scan_devices  = (st.demo_hooks.on_action == nullptr);
        m.has_device_pin    = false;
        m.has_rename        = false;
        m.has_delete_device = false;
        m.has_general       = true;
        m.has_demo          = (st.demo_hooks.on_action != nullptr);
    } else {
        /* MENU_MODE_TRAY */
        m.logical_w         = 256.0f;
        m.has_refresh       = true;
        m.has_scan_devices  = (st.demo_hooks.on_action == nullptr);
        m.has_device_pin    = valid_dev;
        m.has_rename        = (st.device_count > 0);
        m.has_delete_device = valid_dev;
        m.has_general       = true;
        m.has_demo          = (st.demo_hooks.on_action != nullptr);
    }

    float pad = 8.0f;
    float w = m.logical_w;
    float y = 8.0f;
    float row_h = 32.0f;

    if (m.has_refresh) {
        m.rc_refresh = MakeRect(pad, y, w - pad, y + row_h);
        y += row_h + 2.0f;
    } else {
        m.rc_refresh = {};
    }

    if (m.has_scan_devices) {
        m.rc_scan_devices = MakeRect(pad, y, w - pad, y + row_h);
        y += row_h + 2.0f;
    } else {
        m.rc_scan_devices = {};
    }

    if (m.has_device_pin) {
        m.rc_pin = MakeRect(pad, y, w - pad, y + row_h);
        y += row_h + 2.0f;
    } else {
        m.rc_pin = {};
    }

    if (m.has_rename) {
        m.rc_rename = MakeRect(pad, y, w - pad, y + row_h);
        y += row_h + 2.0f;
    } else {
        m.rc_rename = {};
    }

    if (m.has_delete_device) {
        if (!m.has_general) {
            y += 3.0f;
            m.div_dev_del_y = y;
            y += 6.0f;
        } else {
            m.div_dev_del_y = 0.0f;
        }
        m.rc_delete = MakeRect(pad, y, w - pad, y + row_h);
        y += row_h + 2.0f;
    } else {
        m.div_dev_del_y = 0.0f;
        m.rc_delete = {};
    }

    if (!m.has_general) {
        m.div1_y = 0.0f;
        m.rc_seg_wrap = {};
        for (int i = 0; i < 3; ++i) m.rc_seg[i] = {};
        m.rc_slider_hit = {};
        m.rc_slider_track = {};
        m.div2_y = 0.0f;
        m.rc_autostart = {};
        m.div3_y = 0.0f;
        for (int i = 0; i < 4; ++i) m.rc_demo[i] = {};
        m.div4_y = 0.0f;
        m.rc_about = {};
        m.rc_quit = {};
        m.logical_h = y + 6.0f;
        return;
    }

    y += 4.0f;
    m.div1_y = y;
    y += 8.0f;

    /* Display Style Section Header + 3-Way Cosmic Segmented Control (Octans | Nebula | Pulsar) */
    m.style_label_y = y;
    y += 19.0f;
    float seg_h = 30.0f;
    m.rc_seg_wrap = MakeRect(pad + 4.0f, y, w - pad - 4.0f, y + seg_h);
    float inner_l = m.rc_seg_wrap.left + 2.5f;
    float inner_r = m.rc_seg_wrap.right - 2.5f;
    float seg_w = (inner_r - inner_l) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        m.rc_seg[i] = MakeRect(
            inner_l + seg_w * static_cast<float>(i),
            m.rc_seg_wrap.top + 2.5f,
            inner_l + seg_w * static_cast<float>(i + 1),
            m.rc_seg_wrap.bottom - 2.5f
        );
    }
    y += seg_h + 11.0f;

    /* Live Tray Size Slider Section */
    m.slider_label_y = y;
    y += 18.0f;
    m.rc_slider_hit = MakeRect(pad + 4.0f, y, w - pad - 4.0f, y + 26.0f);
    float trk_y = y + 11.0f;
    m.rc_slider_track = MakeRect(pad + 12.0f, trk_y - 2.2f, w - pad - 12.0f, trk_y + 2.2f);
    y += 28.0f;

    y += 4.0f;
    m.div2_y = y;
    y += 7.0f;

    /* Autostart switch row */
    m.rc_autostart = MakeRect(pad, y, w - pad, y + row_h);
    y += row_h + 2.0f;

    if (m.has_demo) {
        y += 4.0f;
        m.div3_y = y;
        y += 8.0f;
        m.demo_label_y = y;
        y += 18.0f;

        float chip_gap = 6.0f;
        float chip_w = (w - (pad + 4.0f) * 2.0f - chip_gap) * 0.5f;
        float chip_h = 26.0f;
        float x0 = pad + 4.0f;
        float x1 = x0 + chip_w + chip_gap;
        m.rc_demo[0] = MakeRect(x0, y, x0 + chip_w, y + chip_h);
        m.rc_demo[1] = MakeRect(x1, y, x1 + chip_w, y + chip_h);
        y += chip_h + 6.0f;
        m.rc_demo[2] = MakeRect(x0, y, x0 + chip_w, y + chip_h);
        m.rc_demo[3] = MakeRect(x1, y, x1 + chip_w, y + chip_h);
        y += chip_h + 4.0f;
    } else {
        m.div3_y = 0.0f;
        for (int i = 0; i < 4; ++i) m.rc_demo[i] = {};
    }

    y += 4.0f;
    m.div4_y = y;
    y += 7.0f;

    m.rc_about = MakeRect(pad, y, w - pad, y + row_h);
    y += row_h + 2.0f;

    m.rc_quit = MakeRect(pad, y, w - pad, y + row_h);
    y += row_h + 8.0f;

    m.logical_h = y;
}

static bool EnsureMenuResources(MenuState &m) {
    if (!m.d2d_factory) {
        D2D1_FACTORY_OPTIONS fo = {};
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory),
                &fo,
                m.d2d_factory.PutVoid()))) {
            return false;
        }
        D2D1_STROKE_STYLE_PROPERTIES sp = {};
        sp.startCap = D2D1_CAP_STYLE_ROUND;
        sp.endCap = D2D1_CAP_STYLE_ROUND;
        sp.lineJoin = D2D1_LINE_JOIN_ROUND;
        m.d2d_factory->CreateStrokeStyle(sp, nullptr, 0, m.round_stroke.Put());
    }
    if (!m.dwrite) {
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown **>(m.dwrite.Put())))) {
            return false;
        }
        auto mk = [&](ComPtr<IDWriteTextFormat> &out, DWRITE_FONT_WEIGHT wt, float sz, DWRITE_TEXT_ALIGNMENT al) {
            CreateOctansTextFormat(m.dwrite.Get(), wt, sz, al, DWRITE_FONT_STRETCH_NORMAL, true, out);
        };
        mk(m.fmt_item,  DWRITE_FONT_WEIGHT_REGULAR,   12.5f, DWRITE_TEXT_ALIGNMENT_LEADING);
        mk(m.fmt_small, DWRITE_FONT_WEIGHT_MEDIUM,    11.0f, DWRITE_TEXT_ALIGNMENT_LEADING);
        mk(m.fmt_seg,   DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.2f, DWRITE_TEXT_ALIGNMENT_CENTER);
        mk(m.fmt_right, DWRITE_FONT_WEIGHT_SEMI_BOLD, 11.0f, DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (!m.rt) {
        D2D1_RENDER_TARGET_PROPERTIES rtp = {};
        rtp.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
        rtp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        rtp.dpiX = static_cast<float>(m.dpi);
        rtp.dpiY = static_cast<float>(m.dpi);
        if (FAILED(m.d2d_factory->CreateDCRenderTarget(&rtp, m.rt.Put()))) {
            return false;
        }
    }
    return m.rt.Get() != nullptr;
}

static const wchar_t *DescribeScaleLabel(float s) {
    if (s < 0.15f) return L"\u5c0f\u5de7";
    if (s < 0.38f) return L"\u7d27\u51d1";
    if (s < 0.63f) return L"\u6807\u51c6";
    if (s < 0.88f) return L"\u5927\u53f7";
    return L"\u8d85\u5927";
}

static void RenderMenuToDC(MenuState &m, HDC target_dc) {
    if (!target_dc || !m.hwnd || !EnsureMenuResources(m)) return;

    RECT crc = {};
    GetClientRect(m.hwnd, &crc);
    int pw = crc.right - crc.left;
    int ph = crc.bottom - crc.top;
    if (pw < 2 || ph < 2) return;

    HDC mem_dc = CreateCompatibleDC(target_dc);
    HBITMAP bmp = CreateCompatibleBitmap(target_dc, pw, ph);
    if (!mem_dc || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (mem_dc) DeleteDC(mem_dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    const UiState &st = GetState();
    ThemePalette pal = GetThemePalette(st.is_dark);
    bool dk = st.is_dark;
    ID2D1DCRenderTarget *rt = m.rt.Get();

    if (SUCCEEDED(rt->BindDC(mem_dc, &crc))) {
        rt->BeginDraw();
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

        D2D1_COLOR_F bg_col  = dk ? HexColor(0x222226, 1.0f) : HexColor(0xF9F9FB, 1.0f);
        D2D1_COLOR_F row_hov = dk ? MakeColor(1, 1, 1, 0.09f) : MakeColor(0, 0, 0, 0.055f);
        D2D1_COLOR_F div_col = dk ? MakeColor(1, 1, 1, 0.09f) : MakeColor(0, 0, 0, 0.07f);
        D2D1_COLOR_F accent  = dk ? HexColor(0x30D158, 1.0f) : HexColor(0x34C759, 1.0f);
        D2D1_COLOR_F red_col = dk ? HexColor(0xFF453A, 1.0f) : HexColor(0xFF3B30, 1.0f);

        rt->Clear(bg_col);

        ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(pal.text_primary, b.Put());
        if (b) {
            b->SetColor(pal.panel_outer_border);
            rt->DrawRoundedRectangle(
                MakeRoundedRect(MakeRect(0.5f, 0.5f, m.logical_w - 0.5f, m.logical_h - 0.5f), 7.5f, 7.5f),
                b.Get(), 1.0f
            );

            auto draw_row_bg = [&](const D2D1_RECT_F &r, int zone, bool danger = false) {
                if (m.hover_zone == zone) {
                    b->SetColor(danger ? MakeColor(red_col.r, red_col.g, red_col.b, dk ? 0.16f : 0.11f) : row_hov);
                    rt->FillRoundedRectangle(MakeRoundedRect(r, 8.0f, 8.0f), b.Get());
                }
            };

            auto draw_div = [&](float dy) {
                b->SetColor(div_col);
                rt->DrawLine(MakePt(14.0f, dy), MakePt(m.logical_w - 14.0f, dy), b.Get(), 1.0f);
            };

            /* 1. Refresh row */
            if (m.has_refresh) {
                draw_row_bg(m.rc_refresh, HIT_REFRESH);
                b->SetColor(pal.text_secondary);
                DrawRefreshIcon(m.d2d_factory.Get(), rt, b.Get(), m.round_stroke.Get(),
                                m.rc_refresh.left + 16.0f, (m.rc_refresh.top + m.rc_refresh.bottom) * 0.5f, 5.2f, 0.0f);
                b->SetColor(pal.text_primary);
                rt->DrawText(L"\u7acb\u5373\u5237\u65b0\u7535\u91cf", 6, m.fmt_item.Get(),
                             MakeRect(m.rc_refresh.left + 32.0f, m.rc_refresh.top, m.rc_refresh.right - 8.0f, m.rc_refresh.bottom), b.Get());
            }

            /* 1b. Scan Devices row (replaces old Add/Discover helper) */
            if (m.has_scan_devices) {
                draw_row_bg(m.rc_scan_devices, HIT_SCAN_DEVICES);
                float pcx = m.rc_scan_devices.left + 16.0f;
                float pcy = (m.rc_scan_devices.top + m.rc_scan_devices.bottom) * 0.5f;
                b->SetColor(pal.text_secondary);
                DrawScanRadarIcon(m.d2d_factory.Get(), rt, b.Get(), m.round_stroke.Get(), pcx, pcy, 5.8f);
                b->SetColor(pal.text_primary);
                rt->DrawText(L"\u626b\u63cf\u8bbe\u5907", 4, m.fmt_item.Get(),
                             MakeRect(m.rc_scan_devices.left + 32.0f, m.rc_scan_devices.top, m.rc_scan_devices.right - 8.0f, m.rc_scan_devices.bottom), b.Get());
            }

            /* 2. Device Pin/Unpin row */
            if (m.has_device_pin) {
                const octans_device_view_t &dev = st.devices[m.device_index];
                bool pinned = IsDevicePinned(st, dev.id);
                draw_row_bg(m.rc_pin, HIT_PIN_DEVICE);
                b->SetColor(pinned ? accent : pal.text_secondary);
                DrawPinIcon(m.d2d_factory.Get(), rt, b.Get(),
                            m.rc_pin.left + 16.0f, (m.rc_pin.top + m.rc_pin.bottom) * 0.5f, 5.0f, pinned);
                b->SetColor(pal.text_primary);
                const wchar_t *txt = pinned ? L"\u4ece\u4efb\u52a1\u680f\u53d6\u6d88\u56fa\u5b9a" : L"\u56fa\u5b9a\u5230\u4efb\u52a1\u680f";
                rt->DrawText(txt, static_cast<UINT32>(wcslen(txt)), m.fmt_item.Get(),
                             MakeRect(m.rc_pin.left + 32.0f, m.rc_pin.top, m.rc_pin.right - 8.0f, m.rc_pin.bottom), b.Get());
            }

            /* 3. Rename Device row */
            if (m.has_rename) {
                int ridx = (m.device_index >= 0 && static_cast<size_t>(m.device_index) < st.device_count) ? m.device_index : 0;
                draw_row_bg(m.rc_rename, HIT_RENAME_DEVICE);
                b->SetColor(pal.text_secondary);
                DrawDeviceGlyph(m.d2d_factory.Get(), rt, b.Get(), OCTANS_DEV_STYLUS,
                                m.rc_rename.left + 16.0f, (m.rc_rename.top + m.rc_rename.bottom) * 0.5f, 6.4f, false);
                wchar_t rbuf[80] = {};
                if (m.has_device_pin) {
                    wcscpy(rbuf, L"\u91cd\u547d\u540d\u6b64\u8bbe\u5907...");
                } else {
                    _snwprintf(rbuf, 79, L"\u91cd\u547d\u540d\u300c%ls\u300d...", st.devices[ridx].name);
                }
                b->SetColor(pal.text_primary);
                rt->DrawText(rbuf, static_cast<UINT32>(wcslen(rbuf)), m.fmt_item.Get(),
                             MakeRect(m.rc_rename.left + 32.0f, m.rc_rename.top, m.rc_rename.right - 8.0f, m.rc_rename.bottom), b.Get());
            }

            /* 3b. Delete / Remove Device row */
            if (m.has_delete_device) {
                if (m.div_dev_del_y > 0.0f) {
                    draw_div(m.div_dev_del_y);
                }
                draw_row_bg(m.rc_delete, HIT_DELETE_DEVICE, true);
                bool del_hov = (m.hover_zone == HIT_DELETE_DEVICE);
                b->SetColor(del_hov ? red_col : pal.text_secondary);
                DrawTrashIcon(rt, b.Get(), m.round_stroke.Get(),
                              m.rc_delete.left + 16.0f, (m.rc_delete.top + m.rc_delete.bottom) * 0.5f, 5.4f);
                b->SetColor(del_hov ? red_col : pal.text_primary);
                rt->DrawText(L"\u5220\u9664\u6b64\u8bbe\u5907", 5, m.fmt_item.Get(),
                             MakeRect(m.rc_delete.left + 32.0f, m.rc_delete.top, m.rc_delete.right - 8.0f, m.rc_delete.bottom), b.Get());
            }

            if (m.has_general) {
                draw_div(m.div1_y);

                /* 4. Display Style Label + 3-Way Cosmic Segmented Control: Octans | Nebula | Pulsar */
                b->SetColor(pal.text_secondary);
                rt->DrawText(L"\u663e\u793a\u6837\u5f0f", 4, m.fmt_small.Get(),
                             MakeRect(14.0f, m.style_label_y, m.logical_w - 14.0f, m.style_label_y + 18.0f), b.Get());

                b->SetColor(dk ? MakeColor(1, 1, 1, 0.08f) : MakeColor(0, 0, 0, 0.06f));
                rt->FillRoundedRectangle(MakeRoundedRect(m.rc_seg_wrap, 9.0f, 9.0f), b.Get());

                float sp = ClampF(m.seg_pos, 0.0f, 2.0f);
                float seg_w = m.rc_seg[0].right - m.rc_seg[0].left;
                float pill_l = m.rc_seg[0].left + sp * seg_w;
                D2D1_RECT_F pill_rc = MakeRect(pill_l, m.rc_seg[0].top, pill_l + seg_w, m.rc_seg[0].bottom);

                b->SetColor(MakeColor(0, 0, 0, dk ? 0.28f : 0.07f));
                rt->FillRoundedRectangle(
                    MakeRoundedRect(MakeRect(pill_rc.left, pill_rc.top + 0.8f, pill_rc.right, pill_rc.bottom + 0.8f), 7.0f, 7.0f),
                    b.Get()
                );
                b->SetColor(dk ? HexColor(0x3A3A40, 1.0f) : HexColor(0xFFFFFF, 1.0f));
                rt->FillRoundedRectangle(MakeRoundedRect(pill_rc, 7.0f, 7.0f), b.Get());
                b->SetColor(dk ? MakeColor(1, 1, 1, 0.12f) : MakeColor(0, 0, 0, 0.07f));
                rt->DrawRoundedRectangle(MakeRoundedRect(pill_rc, 7.0f, 7.0f), b.Get(), 0.8f);

                static const wchar_t *kStyleNames[3] = { L"Octans", L"Nebula", L"Pulsar" };
                for (int i = 0; i < 3; ++i) {
                    float dist = fabsf(sp - static_cast<float>(i));
                    float active_w = ClampF(1.0f - dist, 0.0f, 1.0f);
                    b->SetColor(LerpColor(pal.text_secondary, pal.text_primary, active_w));
                    rt->DrawText(kStyleNames[i], static_cast<UINT32>(wcslen(kStyleNames[i])), m.fmt_seg.Get(), m.rc_seg[i], b.Get());
                }

                /* 5. Live Continuous Tray Size Slider (50% = Default center tick) */
                b->SetColor(pal.text_secondary);
                rt->DrawText(L"\u6258\u76d8\u56fe\u6807\u5927\u5c0f", 6, m.fmt_small.Get(),
                             MakeRect(14.0f, m.slider_label_y, m.logical_w - 86.0f, m.slider_label_y + 18.0f), b.Get());

                wchar_t sbuf[32] = {};
                int pct_lbl = static_cast<int>(roundf(st.tray_scale * 100.0f));
                _snwprintf(sbuf, 31, L"%ls \u00b7 %d%%", DescribeScaleLabel(st.tray_scale), pct_lbl);
                b->SetColor(accent);
                rt->DrawText(sbuf, static_cast<UINT32>(wcslen(sbuf)), m.fmt_right.Get(),
                             MakeRect(m.logical_w - 114.0f, m.slider_label_y, m.logical_w - 14.0f, m.slider_label_y + 18.0f), b.Get());

                float tl = m.rc_slider_track.left;
                float tr = m.rc_slider_track.right;
                float ty = (m.rc_slider_track.top + m.rc_slider_track.bottom) * 0.5f;
                float thumb_x = LerpF(tl, tr, ClampF(st.tray_scale, 0.0f, 1.0f));

                b->SetColor(dk ? MakeColor(1, 1, 1, 0.14f) : MakeColor(0, 0, 0, 0.11f));
                rt->FillRoundedRectangle(MakeRoundedRect(m.rc_slider_track, 2.2f, 2.2f), b.Get());

                D2D1_RECT_F fill_rc = MakeRect(tl, m.rc_slider_track.top, thumb_x, m.rc_slider_track.bottom);
                if (fill_rc.right > fill_rc.left + 1.0f) {
                    b->SetColor(accent);
                    rt->FillRoundedRectangle(MakeRoundedRect(fill_rc, 2.2f, 2.2f), b.Get());
                }

                for (int t = 0; t <= 4; ++t) {
                    float tx = LerpF(tl, tr, static_cast<float>(t) * 0.25f);
                    bool is_def = (t == 2);
                    b->SetColor(is_def ? accent : (dk ? MakeColor(1, 1, 1, 0.22f) : MakeColor(0, 0, 0, 0.18f)));
                    rt->FillEllipse(MakeEllipse(tx, ty + 9.5f, is_def ? 1.8f : 1.2f, is_def ? 1.8f : 1.2f), b.Get());
                }

                float thumb_r = (m.dragging_slider || m.hover_zone == HIT_SLIDER) ? 7.6f : 6.8f;
                b->SetColor(MakeColor(0, 0, 0, dk ? 0.35f : 0.16f));
                rt->FillEllipse(MakeEllipse(thumb_x, ty + 0.8f, thumb_r + 0.8f, thumb_r + 0.8f), b.Get());
                b->SetColor(HexColor(0xFFFFFF, 1.0f));
                rt->FillEllipse(MakeEllipse(thumb_x, ty, thumb_r, thumb_r), b.Get());
                b->SetColor(accent);
                rt->DrawEllipse(MakeEllipse(thumb_x, ty, thumb_r - 0.5f, thumb_r - 0.5f), b.Get(), 1.4f);

                draw_div(m.div2_y);

                /* 6. Autostart with animated Apple iOS Pill Toggle Switch */
                draw_row_bg(m.rc_autostart, HIT_AUTOSTART);
                b->SetColor(pal.text_primary);
                rt->DrawText(L"\u5f00\u673a\u81ea\u542f\u52a8", 5, m.fmt_item.Get(),
                             MakeRect(m.rc_autostart.left + 10.0f, m.rc_autostart.top, m.rc_autostart.right - 50.0f, m.rc_autostart.bottom), b.Get());
                {
                    float ap = ClampF(m.auto_pos, 0.0f, 1.0f);
                    float sw_w = 34.0f, sw_h = 19.0f;
                    float sw_r = m.rc_autostart.right - 8.0f;
                    float sw_l = sw_r - sw_w;
                    float sw_cy = (m.rc_autostart.top + m.rc_autostart.bottom) * 0.5f;
                    D2D1_RECT_F sw_rc = MakeRect(sw_l, sw_cy - sw_h * 0.5f, sw_r, sw_cy + sw_h * 0.5f);
                    D2D1_COLOR_F off_col = dk ? MakeColor(1, 1, 1, 0.18f) : MakeColor(0, 0, 0, 0.16f);
                    b->SetColor(LerpColor(off_col, accent, ap));
                    rt->FillRoundedRectangle(MakeRoundedRect(sw_rc, sw_h * 0.5f, sw_h * 0.5f), b.Get());

                    float knob_r = 7.2f;
                    float knob_cx = LerpF(sw_l + knob_r + 2.3f, sw_r - knob_r - 2.3f, ap);
                    b->SetColor(HexColor(0xFFFFFF, 0.99f));
                    rt->FillEllipse(MakeEllipse(knob_cx, sw_cy, knob_r, knob_r), b.Get());
                }

                /* 7. Optional Demo Quick Chips (2x2) */
                if (m.has_demo) {
                    draw_div(m.div3_y);
                    b->SetColor(pal.text_muted);
                    rt->DrawText(L"Demo \u5feb\u6377\u8c03\u8bd5", 9, m.fmt_small.Get(),
                                 MakeRect(14.0f, m.demo_label_y, m.logical_w - 14.0f, m.demo_label_y + 16.0f), b.Get());

                    const wchar_t *labels[4] = {
                        L"\u7535\u91cf\u52a8\u6548 (D)",
                        L"\u5207\u6362\u5145\u7535",
                        L"\u4eae\u6697\u5207\u6362 (T)",
                        L"\u589e\u5220\u8bbe\u5907 (C)"
                    };
                    int zones[4] = { HIT_DEMO_DRAIN, HIT_DEMO_CHARGE, HIT_DEMO_THEME, HIT_DEMO_COUNT };
                    for (int i = 0; i < 4; ++i) {
                        bool hov = (m.hover_zone == zones[i]);
                        b->SetColor(hov
                            ? (dk ? MakeColor(1, 1, 1, 0.14f) : MakeColor(0, 0, 0, 0.09f))
                            : (dk ? MakeColor(1, 1, 1, 0.06f) : MakeColor(0, 0, 0, 0.04f)));
                        rt->FillRoundedRectangle(MakeRoundedRect(m.rc_demo[i], 7.0f, 7.0f), b.Get());
                        b->SetColor(pal.text_primary);
                        rt->DrawText(labels[i], static_cast<UINT32>(wcslen(labels[i])), m.fmt_seg.Get(), m.rc_demo[i], b.Get());
                    }
                }

                draw_div(m.div4_y);

                /* 8. About Octans (version, author, manual update check) */
                draw_row_bg(m.rc_about, HIT_ABOUT);
                {
                    float icx = m.rc_about.left + 16.0f;
                    float icy = (m.rc_about.top + m.rc_about.bottom) * 0.5f;
                    b->SetColor(pal.text_secondary);
                    rt->DrawEllipse(MakeEllipse(icx, icy, 5.4f, 5.4f), b.Get(), 1.25f);
                    rt->FillEllipse(MakeEllipse(icx, icy - 2.3f, 0.85f, 0.85f), b.Get());
                    rt->DrawLine(MakePt(icx, icy - 0.3f), MakePt(icx, icy + 2.9f), b.Get(), 1.35f, m.round_stroke.Get());
                }
                b->SetColor(pal.text_primary);
                rt->DrawText(L"\u5173\u4e8e Octans", 9, m.fmt_item.Get(),
                             MakeRect(m.rc_about.left + 32.0f, m.rc_about.top, m.rc_about.right - 8.0f, m.rc_about.bottom), b.Get());

                /* 9. Quit Octans */
                draw_row_bg(m.rc_quit, HIT_QUIT, true);
                b->SetColor(m.hover_zone == HIT_QUIT ? red_col : pal.text_primary);
                rt->DrawText(L"\u9000\u51fa Octans", 9, m.fmt_item.Get(),
                             MakeRect(m.rc_quit.left + 10.0f, m.rc_quit.top, m.rc_quit.right - 8.0f, m.rc_quit.bottom), b.Get());
            }
        }

        HRESULT hr = rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            m.rt.Reset();
        }
    }

    BitBlt(target_dc, 0, 0, pw, ph, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
}

static void RepaintMenu(MenuState &m) {
    if (m.hwnd) {
        InvalidateRect(m.hwnd, nullptr, FALSE);
        UpdateWindow(m.hwnd);
    }
}

static void StartMenuAnim(MenuState &m) {
    if (!m.hwnd) return;
    if (!m.anim_active) {
        QueryPerformanceCounter(&m.qpc_last);
        SetTimer(m.hwnd, TIMER_MENU_ANIM, 10, nullptr);
        m.anim_active = true;
    }
}

static void StopMenuAnim(MenuState &m) {
    if (m.anim_active && m.hwnd) {
        KillTimer(m.hwnd, TIMER_MENU_ANIM);
        m.anim_active = false;
    }
}

static void StepMenuAnim(MenuState &m) {
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    float dt = (m.qpc_freq.QuadPart > 0)
        ? static_cast<float>(static_cast<double>(now.QuadPart - m.qpc_last.QuadPart) / static_cast<double>(m.qpc_freq.QuadPart))
        : 0.016f;
    m.qpc_last = now;
    dt = ClampF(dt, 0.001f, 0.050f);

    float k = 1.0f - expf(-dt * 22.0f);
    bool active = false;

    if (fabsf(m.seg_pos - m.seg_target) > 0.003f) {
        m.seg_pos = LerpF(m.seg_pos, m.seg_target, k);
        active = true;
    } else {
        m.seg_pos = m.seg_target;
    }

    if (fabsf(m.auto_pos - m.auto_target) > 0.003f) {
        m.auto_pos = LerpF(m.auto_pos, m.auto_target, k);
        active = true;
    } else {
        m.auto_pos = m.auto_target;
    }

    RepaintMenu(m);
    if (!active) {
        StopMenuAnim(m);
    }
}

static int HitTestMenu(const MenuState &m, float dx, float dy) {
    if (m.has_refresh && PtInR(m.rc_refresh, dx, dy)) return HIT_REFRESH;
    if (m.has_scan_devices && PtInR(m.rc_scan_devices, dx, dy)) return HIT_SCAN_DEVICES;
    if (m.has_device_pin && PtInR(m.rc_pin, dx, dy)) return HIT_PIN_DEVICE;
    if (m.has_rename && PtInR(m.rc_rename, dx, dy)) return HIT_RENAME_DEVICE;
    if (m.has_delete_device && PtInR(m.rc_delete, dx, dy)) return HIT_DELETE_DEVICE;
    if (m.has_general) {
        if (PtInR(m.rc_seg[0], dx, dy)) return HIT_SEG_OCTANS;
        if (PtInR(m.rc_seg[1], dx, dy)) return HIT_SEG_NEBULA;
        if (PtInR(m.rc_seg[2], dx, dy)) return HIT_SEG_PULSAR;
        if (PtInR(m.rc_slider_hit, dx, dy)) return HIT_SLIDER;
        if (PtInR(m.rc_autostart, dx, dy)) return HIT_AUTOSTART;
        if (m.has_demo) {
            if (PtInR(m.rc_demo[0], dx, dy)) return HIT_DEMO_DRAIN;
            if (PtInR(m.rc_demo[1], dx, dy)) return HIT_DEMO_CHARGE;
            if (PtInR(m.rc_demo[2], dx, dy)) return HIT_DEMO_THEME;
            if (PtInR(m.rc_demo[3], dx, dy)) return HIT_DEMO_COUNT;
        }
        if (PtInR(m.rc_about, dx, dy)) return HIT_ABOUT;
        if (PtInR(m.rc_quit, dx, dy)) return HIT_QUIT;
    }
    return HIT_NONE;
}

static void UpdateSliderFromX(MenuState &m, float dx) {
    float tl = m.rc_slider_track.left;
    float tr = m.rc_slider_track.right;
    if (tr <= tl) return;
    float raw = ClampF((dx - tl) / (tr - tl), 0.0f, 1.0f);
    for (int t = 0; t <= 4; ++t) {
        float tick = static_cast<float>(t) * 0.25f;
        if (fabsf(raw - tick) < 0.03f) {
            raw = tick;
            break;
        }
    }
    float quant = roundf(raw * 50.0f) / 50.0f;
    if (fabsf(quant - GetState().tray_scale) >= 0.015f) {
        ActionSetTrayScale(quant);
        RepaintMenu(m);
    }
}

static LRESULT CALLBACK AppleMenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *m = reinterpret_cast<MenuState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_SETCURSOR:
            if (m && LOWORD(lp) == HTCLIENT) {
                bool interactive = (m->hover_zone != HIT_NONE || m->dragging_slider);
                SetCursor(LoadCursorW(nullptr, interactive ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_TIMER:
            if (m && wp == TIMER_MENU_ANIM) {
                StepMenuAnim(*m);
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (m) {
                if (!m->mouse_tracked) {
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                    m->mouse_tracked = true;
                }
                float scale = static_cast<float>(m->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                if (m->dragging_slider) {
                    UpdateSliderFromX(*m, dx);
                    return 0;
                }
                int hz = HitTestMenu(*m, dx, dy);
                if (hz != m->hover_zone) {
                    m->hover_zone = hz;
                    SetCursor(LoadCursorW(nullptr, hz != HIT_NONE ? IDC_HAND : IDC_ARROW));
                    RepaintMenu(*m);
                }
            }
            return 0;

        case WM_MOUSELEAVE:
            if (m) {
                m->mouse_tracked = false;
                if (!m->dragging_slider && m->hover_zone != HIT_NONE) {
                    m->hover_zone = HIT_NONE;
                    RepaintMenu(*m);
                }
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (m) {
                float scale = static_cast<float>(m->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                int hz = HitTestMenu(*m, dx, dy);
                if (hz == HIT_SLIDER) {
                    m->dragging_slider = true;
                    SetCapture(hwnd);
                    UpdateSliderFromX(*m, dx);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (m) {
                if (m->dragging_slider) {
                    m->dragging_slider = false;
                    ReleaseCapture();
                    RepaintMenu(*m);
                    return 0;
                }
                float scale = static_cast<float>(m->dpi) / 96.0f;
                float dx = static_cast<float>(static_cast<short>(LOWORD(lp))) / scale;
                float dy = static_cast<float>(static_cast<short>(HIWORD(lp))) / scale;
                int hz = HitTestMenu(*m, dx, dy);

                switch (hz) {
                    case HIT_REFRESH:
                        ActionTriggerRefresh();
                        m->done = true;
                        break;
                    case HIT_SCAN_DEVICES:
                        m->pending_post_action = 3;
                        m->done = true;
                        break;
                    case HIT_PIN_DEVICE: {
                        const UiState &st = GetState();
                        if (m->device_index >= 0 && static_cast<size_t>(m->device_index) < st.device_count) {
                            ActionTogglePinDevice(st.devices[m->device_index].id);
                            if (m->mode == MENU_MODE_PANEL_DEVICE) {
                                m->done = true;
                            } else {
                                RepaintMenu(*m);
                            }
                        }
                        break;
                    }
                    case HIT_RENAME_DEVICE:
                        m->pending_post_action = 1;
                        m->done = true;
                        break;
                    case HIT_DELETE_DEVICE:
                        m->pending_post_action = 4;
                        m->done = true;
                        break;
                    case HIT_SEG_OCTANS:
                        ActionToggleTrayStyle(TRAY_STYLE_OCTANS);
                        m->seg_target = 0.0f;
                        StartMenuAnim(*m);
                        break;
                    case HIT_SEG_NEBULA:
                        ActionToggleTrayStyle(TRAY_STYLE_NEBULA);
                        m->seg_target = 1.0f;
                        StartMenuAnim(*m);
                        break;
                    case HIT_SEG_PULSAR:
                        ActionToggleTrayStyle(TRAY_STYLE_PULSAR);
                        m->seg_target = 2.0f;
                        StartMenuAnim(*m);
                        break;
                    case HIT_AUTOSTART: {
                        ActionToggleAutostart();
                        const UiState &st = GetState();
                        bool on = (st.ops.get_autostart && st.ops.get_autostart(st.backend_ctx) != 0);
                        m->auto_target = on ? 1.0f : 0.0f;
                        StartMenuAnim(*m);
                        break;
                    }
                    case HIT_DEMO_DRAIN:
                        ActionRunDemoAction(1);
                        break;
                    case HIT_DEMO_CHARGE:
                        ActionRunDemoAction(2);
                        break;
                    case HIT_DEMO_THEME: {
                        ActionRunDemoAction(3);
                        BOOL dk = GetState().is_dark ? TRUE : FALSE;
                        DwmSetWindowAttribute(hwnd, 20, &dk, sizeof(dk));
                        RepaintMenu(*m);
                        break;
                    }
                    case HIT_DEMO_COUNT:
                        ActionRunDemoAction(4);
                        break;
                    case HIT_ABOUT:
                        m->pending_post_action = 5;
                        m->done = true;
                        break;
                    case HIT_QUIT:
                        m->pending_post_action = 2;
                        m->done = true;
                        break;
                    default:
                        break;
                }
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (m && m->has_general) {
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                float step = (delta > 0) ? 0.05f : -0.05f;
                ActionSetTrayScale(ClampF(GetState().tray_scale + step, 0.0f, 1.0f));
                RepaintMenu(*m);
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            if (m && m->dragging_slider) {
                m->dragging_slider = false;
                RepaintMenu(*m);
            }
            return 0;

        case WM_KEYDOWN:
            if (m && wp == VK_ESCAPE) {
                m->done = true;
                return 0;
            }
            break;

        case WM_ACTIVATE:
            if (m && LOWORD(wp) == WA_INACTIVE && !m->dragging_slider) {
                if ((GetTickCount64() - m->created_ms) >= 160ULL) {
                    HWND activated = reinterpret_cast<HWND>(lp);
                    m->dismissed_by_outside = (activated != m->owner);
                    m->done = true;
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);
            if (m) RenderMenuToDC(*m, hdc);
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

void ShowApplePopoverMenu(HWND owner, int screen_x, int screen_y, int device_index, MenuMode mode) {
    static const wchar_t *kMenuClass = L"OctansAppleMenuWnd";
    static bool registered = false;
    HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = AppleMenuProc;
        wc.hInstance = hinst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kMenuClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    const UiState &st = GetState();
    MenuState m = {};
    m.owner = owner;
    m.mode = mode;
    m.device_index = device_index;
    m.dpi = GetWindowDpiSafe(owner);
    QueryPerformanceFrequency(&m.qpc_freq);
    m.seg_pos = m.seg_target = static_cast<float>(static_cast<int>(st.tray_style));
    bool auto_on = (st.ops.get_autostart && st.ops.get_autostart(st.backend_ctx) != 0);
    m.auto_pos = m.auto_target = auto_on ? 1.0f : 0.0f;
    LayoutMenu(m);

    if (m.logical_h < 24.0f) return;

    float scale = static_cast<float>(m.dpi) / 96.0f;
    int px_w = static_cast<int>(ceilf(m.logical_w * scale));
    int px_h = static_cast<int>(ceilf(m.logical_h * scale));
    int margin = static_cast<int>(ceilf(8.0f * scale));

    POINT pt = { screen_x, screen_y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int x = screen_x;
    int y = screen_y;
    if (x + px_w > mi.rcWork.right - margin) x = mi.rcWork.right - margin - px_w;
    if (x < mi.rcWork.left + margin) x = mi.rcWork.left + margin;
    if (y + px_h > mi.rcWork.bottom - margin) y = screen_y - px_h - margin;
    if (y + px_h > mi.rcWork.bottom - margin) y = mi.rcWork.bottom - margin - px_h;
    if (y < mi.rcWork.top + margin) y = mi.rcWork.top + margin;

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kMenuClass,
        L"OctansMenu",
        WS_POPUP,
        x, y, px_w, px_h,
        owner,
        nullptr,
        hinst,
        &m
    );
    if (!hwnd) return;
    m.hwnd = hwnd;
    DisableWindowIme(hwnd);

    BOOL dark_val = GetState().is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark_val, sizeof(dark_val));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));

    m.created_ms = GetTickCount64();
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    RepaintMenu(m);

    MSG msg = {};
    while (!m.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            m.done = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    StopMenuAnim(m);

    int post_action = m.pending_post_action;
    int rename_idx = (m.device_index >= 0) ? m.device_index : 0;
    bool dismissed_outside = m.dismissed_by_outside;

    m.rt.Reset();
    m.round_stroke.Reset();
    m.fmt_right.Reset();
    m.fmt_seg.Reset();
    m.fmt_small.Reset();
    m.fmt_item.Reset();
    m.dwrite.Reset();
    m.d2d_factory.Reset();
    DestroyWindow(hwnd);

    if (owner && owner == PanelGetHwnd() && PanelIsVisible()) {
        if (post_action == 0 && dismissed_outside) {
            PanelHide();
        } else if (post_action == 0) {
            SetForegroundWindow(owner);
        }
    }

    if (post_action == 1) {
        ActionPromptRenameDevice(rename_idx);
    } else if (post_action == 2) {
        ActionRequestQuit();
    } else if (post_action == 3) {
        ActionTriggerFullScan();
    } else if (post_action == 4) {
        const UiState &cur = GetState();
        if (rename_idx >= 0 && static_cast<size_t>(rename_idx) < cur.device_count) {
            char del_id[64] = {};
            strncpy(del_id, cur.devices[rename_idx].id, sizeof(del_id) - 1);
            ActionRemoveDevice(del_id);
        }
    } else if (post_action == 5) {
        ActionShowAboutDialog();
    }
}

} /* namespace octans_ui */
