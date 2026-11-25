#include "ui_tray.h"
#include "ui_theme.h"
#include "ui_glyphs.h"
#include "ui_brand.h"
#include "ui_fonts.h"
#include <stdio.h>

namespace octans_ui {

static constexpr UINT kSlotUidBase = 10;

/* Stable per-slot GUIDs so Windows 11 permanently remembers taskbar promotion once dragged out of '^' */
static GUID MakeSlotGuid(uint8_t slot_index) {
    GUID g = {
        0x7A4C9E10u,
        0x3B2Fu,
        0x4E81u,
        { 0x9Du, 0x20u, 0x4Fu, 0x61u, 0x8Cu, 0x10u, 0x01u, slot_index }
    };
    return g;
}

struct TraySlot {
    bool active = false;
    bool use_guid = false;
    UINT uid = 0;
    GUID guid = {};
    char device_id[64] = {};
    HICON hicon = nullptr;
    /* Render signature to avoid redundant GDI/D2D redraws and taskbar flicker */
    int sig_kind = -1;
    int sig_pct = -99;
    int sig_pwr = -1;
    int sig_low = -1;
    int sig_style = -1;
    int sig_size = -1;
    int sig_dark = -1;
    int sig_px = 0;
    wchar_t last_tip[128] = {};
};

struct TrayContext {
    HWND host_hwnd = nullptr;
    /* Slot 0 is the primary/first pinned device (and fallback when 0 devices exist).
     * Slots 1..MAX_DEVICES-1 hold additional simultaneously pinned devices. */
    TraySlot slots[MAX_DEVICES];
    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<IDWriteFactory> dwrite_factory;
    ComPtr<ID2D1StrokeStyle> round_stroke;
};

static TrayContext g_tray;

static bool DetectTaskbarDarkMode() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    LONG rc = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size
    );
    if (rc == ERROR_SUCCESS) {
        return value == 0;
    }
    return DetectSystemDarkMode();
}

static UINT GetTaskbarDpi() {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbar) {
        return GetWindowDpiSafe(taskbar);
    }
    return GetWindowDpiSafe(g_tray.host_hwnd);
}

/* Render at high-res 32/40/48px edge-to-edge so Windows 11 notification area scales
 * crisply to fill the maximum tray icon footprint without dead outer margins. */
static int GetTrayIconPx(UINT dpi) {
    if (dpi >= 168) return 48;
    if (dpi >= 132) return 40;
    return 32;
}

static bool EnsureTrayFactories() {
    if (!g_tray.d2d_factory) {
        D2D1_FACTORY_OPTIONS opts = {};
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                __uuidof(ID2D1Factory),
                &opts,
                g_tray.d2d_factory.PutVoid()))) {
            return false;
        }
    }
    if (!g_tray.round_stroke) {
        D2D1_STROKE_STYLE_PROPERTIES props = {};
        props.startCap = D2D1_CAP_STYLE_ROUND;
        props.endCap = D2D1_CAP_STYLE_ROUND;
        props.dashCap = D2D1_CAP_STYLE_ROUND;
        props.lineJoin = D2D1_LINE_JOIN_ROUND;
        props.miterLimit = 10.0f;
        props.dashStyle = D2D1_DASH_STYLE_SOLID;
        g_tray.d2d_factory->CreateStrokeStyle(props, nullptr, 0, g_tray.round_stroke.Put());
    }
    if (!g_tray.dwrite_factory) {
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown **>(g_tray.dwrite_factory.Put())
        );
    }
    return true;
}

/* Convert Direct2D premultiplied BGRA pixels -> Win32 HICON straight (un-premultiplied) BGRA.
 * Prevents Explorer's Shell_NotifyIconW from turning semi-transparent light colors into dark grey! */
static void UnpremultiplyBgra(void *bits, int w, int h) {
    auto *px = static_cast<uint8_t *>(bits);
    size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (size_t i = 0; i < count; ++i) {
        uint8_t a = px[3];
        if (a > 0 && a < 255) {
            uint32_t b = (static_cast<uint32_t>(px[0]) * 255u + (a / 2u)) / a;
            uint32_t g = (static_cast<uint32_t>(px[1]) * 255u + (a / 2u)) / a;
            uint32_t r = (static_cast<uint32_t>(px[2]) * 255u + (a / 2u)) / a;
            px[0] = static_cast<uint8_t>(b > 255u ? 255u : b);
            px[1] = static_cast<uint8_t>(g > 255u ? 255u : g);
            px[2] = static_cast<uint8_t>(r > 255u ? 255u : r);
        }
        px += 4;
    }
}

/* Render maximum-radius transparent-interior crisp anti-aliased D2D tray icon -> HICON.
 * Supports:
 *   - TRAY_STYLE_RING_GLYPH  : Full-bleed circular battery ring + centered panel-style white device icon
 *   - TRAY_STYLE_ICON_NUMBER : Split-stack top device icon (+ charging bolt) + bottom bold colored battery number */
static HICON RenderTrayIcon(
    const octans_device_view_t *dev,
    TrayStyle style,
    float tray_scale,
    int icon_px,
    bool tb_dark
) {
    if (!EnsureTrayFactories()) return nullptr;
    if (icon_px < 16) icon_px = 16;
    if (icon_px > 64) icon_px = 64;
    float s = ClampF(tray_scale, 0.0f, 1.0f);

    ThemePalette pal = GetThemePalette(tb_dark);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = icon_px;
    bmi.bmiHeader.biHeight = -icon_px; /* top-down DIB */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP color_bmp = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (screen_dc) ReleaseDC(nullptr, screen_dc);

    if (!mem_dc || !color_bmp || !bits) {
        if (color_bmp) DeleteObject(color_bmp);
        if (mem_dc) DeleteDC(mem_dc);
        return nullptr;
    }

    memset(bits, 0, static_cast<size_t>(icon_px) * static_cast<size_t>(icon_px) * 4u);
    HGDIOBJ old_bmp = SelectObject(mem_dc, color_bmp);

    D2D1_RENDER_TARGET_PROPERTIES rt_props = {};
    rt_props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    rt_props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rt_props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    rt_props.dpiX = 96.0f;
    rt_props.dpiY = 96.0f;
    rt_props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rt_props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    ComPtr<ID2D1DCRenderTarget> rt;
    HRESULT hr = g_tray.d2d_factory->CreateDCRenderTarget(&rt_props, rt.Put());
    if (SUCCEEDED(hr)) {
        RECT rc = { 0, 0, icon_px, icon_px };
        hr = rt->BindDC(mem_dc, &rc);
        if (SUCCEEDED(hr)) {
            rt->BeginDraw();
            rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            /* Pure 100% transparent background — ZERO grey circle! */
            rt->Clear(MakeColor(0.0f, 0.0f, 0.0f, 0.0f));

            float sz = static_cast<float>(icon_px);
            float cx = sz * 0.5f;
            float cy = sz * 0.5f;

            if (!dev) {
                /* No peripheral detected: render standalone transparent Octans Logo (no background). */
                float logo_r = sz * LerpF(0.38f, 0.48f, s);
                DrawOctansLogo(
                    g_tray.d2d_factory.Get(),
                    rt.Get(),
                    cx, cy,
                    logo_r,
                    tb_dark,
                    true,
                    1.0f,
                    1.0f
                );
            } else if (style == TRAY_STYLE_ICON_NUMBER && g_tray.dwrite_factory) {
                /* =====================================================================
                 * SPLIT-STACK COMPLICATION driven smoothly by continuous tray_scale [0..1]:
                 *   0.50 (Default) = old 100% scale (with boosted Mouse + unclipped bottom)
                 *   1.00 (Max)     = ultra-bold full-canvas presence with zero clipping
                 * ===================================================================== */
                D2D1_COLOR_F glyph_ink = IsDeviceMuted(*dev)
                    ? (tb_dark ? HexColor(0xAEAEB2, 0.72f) : HexColor(0x636366, 0.78f))
                    : (tb_dark ? HexColor(0xFFFFFF, 0.98f) : HexColor(0x141416, 0.96f));

                D2D1_COLOR_F num_col;
                if (IsDeviceMuted(*dev) || dev->battery_percent < 0) {
                    num_col = tb_dark ? HexColor(0xAEAEB2, 0.85f) : HexColor(0x6E6E73, 0.92f);
                } else if (dev->power_state == OCTANS_PWR_CHARGING || dev->power_state == OCTANS_PWR_FULL) {
                    num_col = tb_dark ? HexColor(0x30D158, 1.0f) : HexColor(0x149B38, 1.0f);
                } else if (dev->is_low_battery || dev->battery_percent <= 20) {
                    num_col = tb_dark ? HexColor(0xFF453A, 1.0f) : HexColor(0xDC2620, 1.0f);
                } else {
                    num_col = tb_dark ? HexColor(0x30D158, 1.0f) : HexColor(0x149B38, 1.0f);
                }

                ComPtr<ID2D1SolidColorBrush> ink_brush;
                ComPtr<ID2D1SolidColorBrush> num_brush;
                rt->CreateSolidColorBrush(glyph_ink, ink_brush.Put());
                rt->CreateSolidColorBrush(num_col, num_brush.Put());

                bool charging = (dev->power_state == OCTANS_PWR_CHARGING);
                bool is_tall_glyph = (dev->kind == OCTANS_DEV_MOUSE ||
                                      dev->kind == OCTANS_DEV_SPEAKER ||
                                      dev->kind == OCTANS_DEV_STYLUS ||
                                      dev->kind == OCTANS_DEV_EARBUDS);

                /* Piecewise smooth curve: 0.50 = old 100% default; 0.50..1.00 scales further up */
                float g_half;
                if (is_tall_glyph) {
                    g_half = (s <= 0.50f)
                        ? (sz * LerpF(0.215f, 0.270f, s * 2.0f))
                        : (sz * LerpF(0.270f, 0.292f, (s - 0.50f) * 2.0f));
                } else {
                    g_half = (s <= 0.50f)
                        ? (sz * LerpF(0.285f, 0.368f, s * 2.0f))
                        : (sz * LerpF(0.368f, 0.402f, (s - 0.50f) * 2.0f));
                }

                float h_factor = is_tall_glyph ? 0.93f : ((dev->kind == OCTANS_DEV_HEADSET) ? 0.74f : 0.65f);
                float visual_half_h = g_half * h_factor;
                float g_cy = visual_half_h + LerpF(0.95f, 0.35f, s);
                float icon_bottom = g_cy + visual_half_h;

                if (ink_brush) {
                    DrawDeviceGlyph(g_tray.d2d_factory.Get(), rt.Get(), ink_brush.Get(), dev->kind, cx, g_cy, g_half, true);
                }

                if (charging && num_brush) {
                    float br = sz * LerpF(0.145f, 0.168f, s);
                    float bx = sz - br - 0.30f;
                    float by = g_cy + visual_half_h * 0.34f;
                    ComPtr<ID2D1SolidColorBrush> bg_b;
                    rt->CreateSolidColorBrush(
                        tb_dark ? HexColor(0x18181A, 0.96f) : HexColor(0xFFFFFF, 0.96f),
                        bg_b.Put()
                    );
                    if (bg_b) {
                        rt->FillEllipse(MakeEllipse(bx, by, br, br), bg_b.Get());
                    }
                    DrawLightningBolt(g_tray.d2d_factory.Get(), rt.Get(), num_brush.Get(), bx, by, br * 0.90f);
                }

                wchar_t num_buf[8] = {};
                if (dev->power_state == OCTANS_PWR_OFFLINE || dev->battery_percent < 0) {
                    wcscpy(num_buf, L"--");
                } else {
                    _snwprintf(num_buf, 7, L"%d", static_cast<int>(dev->battery_percent));
                }

                /* Bold Anthropic Serif digits with guaranteed breathing gap & safe bottom margin */
                float base_ratio = (s <= 0.50f)
                    ? LerpF(0.415f, 0.510f, s * 2.0f)
                    : LerpF(0.510f, 0.565f, (s - 0.50f) * 2.0f);
                float font_ratio = (dev->battery_percent >= 100) ? (base_ratio * 0.78f) : base_ratio;
                float font_sz = sz * font_ratio;
                if (font_sz < 10.0f) font_sz = 10.0f;

                ComPtr<IDWriteTextFormat> fmt;
                if (num_brush && CreateOctansDigitFormat(g_tray.dwrite_factory.Get(), DWRITE_FONT_WEIGHT_EXTRA_BOLD, font_sz, fmt)) {
                    float gap_px = sz * LerpF(0.080f, 0.052f, s);
                    float bottom_pad = sz * LerpF(0.055f, 0.032f, s);
                    float top_num_y = icon_bottom + gap_px;
                    if (top_num_y < sz * 0.49f) top_num_y = sz * 0.49f;
                    if (top_num_y > sz * 0.58f) top_num_y = sz * 0.58f;
                    D2D1_RECT_F tr = MakeRect(-2.0f, top_num_y, sz + 2.0f, sz - bottom_pad);
                    rt->DrawText(
                        num_buf,
                        static_cast<UINT32>(wcslen(num_buf)),
                        fmt.Get(),
                        tr,
                        num_brush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_NONE
                    );
                }
            } else {
                /* =====================================================================
                 * DEFAULT RING + GLYPH driven smoothly by continuous tray_scale [0..1]:
                 *   0.50 (Default) = old 100% scale (edge-to-edge crisp ring + bold glyph)
                 *   1.00 (Max)     = extra-bold heavy ring + larger glyph, zero overlap
                 * ===================================================================== */
                float radius_ratio = (s <= 0.50f)
                    ? LerpF(0.420f, 0.500f, s * 2.0f)
                    : 0.500f;
                float stroke_ratio = (s <= 0.50f)
                    ? LerpF(0.076f, 0.098f, s * 2.0f)
                    : LerpF(0.098f, 0.122f, (s - 0.50f) * 2.0f);
                float stroke = sz * stroke_ratio;
                if (stroke < 2.2f) stroke = 2.2f;
                float edge_inset = (s <= 0.50f)
                    ? LerpF(0.50f, 0.43f, s * 2.0f)
                    : LerpF(0.43f, 0.34f, (s - 0.50f) * 2.0f);
                float radius = (sz * radius_ratio) - (stroke * edge_inset);
                float inner_r = radius - (stroke * 0.5f);

                D2D1_COLOR_F track_col = tb_dark
                    ? MakeColor(1.0f, 1.0f, 1.0f, 0.24f)
                    : MakeColor(0.0f, 0.0f, 0.0f, 0.16f);
                ComPtr<ID2D1SolidColorBrush> track_brush;
                rt->CreateSolidColorBrush(track_col, track_brush.Put());
                if (track_brush) {
                    rt->DrawEllipse(MakeEllipse(cx, cy, radius, radius), track_brush.Get(), stroke);
                }

                if (dev) {
                    float pct = (dev->battery_percent >= 0)
                        ? ClampF(static_cast<float>(dev->battery_percent) / 100.0f, 0.0f, 1.0f)
                        : 0.0f;
                    D2D1_COLOR_F ring_col = GetDeviceAccentColor(*dev, pal);
                    ComPtr<ID2D1SolidColorBrush> ring_brush;
                    rt->CreateSolidColorBrush(ring_col, ring_brush.Put());
                    if (ring_brush && pct > 0.0f) {
                        DrawBatteryRingArc(
                            g_tray.d2d_factory.Get(),
                            rt.Get(),
                            ring_brush.Get(),
                            g_tray.round_stroke.Get(),
                            cx, cy,
                            radius,
                            stroke,
                            pct,
                            false
                        );
                    }

                    D2D1_COLOR_F glyph_ink = IsDeviceMuted(*dev)
                        ? (tb_dark ? HexColor(0xAEAEB2, 0.72f) : HexColor(0x636366, 0.78f))
                        : (tb_dark ? HexColor(0xFFFFFF, 0.98f) : HexColor(0x2C2C2E, 0.92f));
                    ComPtr<ID2D1SolidColorBrush> ink_brush;
                    rt->CreateSolidColorBrush(glyph_ink, ink_brush.Put());

                    if (ink_brush) {
                        bool wide_diag = (dev->kind == OCTANS_DEV_KEYBOARD ||
                                          dev->kind == OCTANS_DEV_GAMEPAD  ||
                                          dev->kind == OCTANS_DEV_HEADSET);
                        float glyph_ratio;
                        if (wide_diag) {
                            glyph_ratio = (s <= 0.50f)
                                ? LerpF(0.68f, 0.78f, s * 2.0f)
                                : LerpF(0.78f, 0.82f, (s - 0.50f) * 2.0f);
                        } else {
                            glyph_ratio = (s <= 0.50f)
                                ? LerpF(0.72f, 0.85f, s * 2.0f)
                                : LerpF(0.85f, 0.89f, (s - 0.50f) * 2.0f);
                        }
                        float glyph_half = inner_r * glyph_ratio;
                        DrawDeviceGlyph(
                            g_tray.d2d_factory.Get(),
                            rt.Get(),
                            ink_brush.Get(),
                            dev->kind,
                            cx, cy,
                            glyph_half,
                            true
                        );
                    }

                    if (dev->power_state == OCTANS_PWR_CHARGING) {
                        float bx = sz - sz * 0.19f;
                        float by = sz - sz * 0.19f;
                        float br = sz * 0.21f;
                        ComPtr<ID2D1SolidColorBrush> bg_b;
                        rt->CreateSolidColorBrush(
                            tb_dark ? HexColor(0x18181A, 0.98f) : HexColor(0xFFFFFF, 0.98f),
                            bg_b.Put()
                        );
                        if (bg_b) {
                            rt->FillEllipse(MakeEllipse(bx, by, br, br), bg_b.Get());
                        }
                        if (ring_brush) {
                            DrawLightningBolt(g_tray.d2d_factory.Get(), rt.Get(), ring_brush.Get(), bx, by, br * 0.88f);
                        }
                    }
                }
            }

            rt->EndDraw();
        }
    }

    SelectObject(mem_dc, old_bmp);
    DeleteDC(mem_dc);

    UnpremultiplyBgra(bits, icon_px, icon_px);

    HBITMAP mask_bmp = CreateBitmap(icon_px, icon_px, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp;
    ii.hbmColor = color_bmp;
    HICON icon = CreateIconIndirect(&ii);

    if (mask_bmp) DeleteObject(mask_bmp);
    DeleteObject(color_bmp);
    return icon;
}

static void FormatSingleDeviceLine(const octans_device_view_t &d, wchar_t *line, size_t cap, bool prefix_newline) {
    const wchar_t *nl = prefix_newline ? L"\n" : L"";
    const wchar_t *nm = d.name[0] ? d.name : L"Device";
    const wchar_t *est = d.is_estimated ? L"~" : L"";
    if (d.power_state == OCTANS_PWR_OFFLINE) {
        _snwprintf(line, cap - 1, L"%ls%ls: \u79bb\u7ebf", nl, nm);
    } else if (d.power_state == OCTANS_PWR_SLEEPING) {
        if (d.battery_percent >= 0) {
            _snwprintf(line, cap - 1, L"%ls%ls: %ls%d%% (\u4f11\u7720)", nl, nm, est, static_cast<int>(d.battery_percent));
        } else {
            _snwprintf(line, cap - 1, L"%ls%ls: \u4f11\u7720", nl, nm);
        }
    } else if (d.power_state == OCTANS_PWR_CHARGING) {
        _snwprintf(line, cap - 1, L"%ls%ls: %ls%d%% (\u5145\u7535\u4e2d)", nl, nm, est, static_cast<int>(d.battery_percent));
    } else if (d.battery_percent >= 0) {
        _snwprintf(line, cap - 1, L"%ls%ls: %ls%d%%", nl, nm, est, static_cast<int>(d.battery_percent));
    } else {
        _snwprintf(line, cap - 1, L"%ls%ls: --", nl, nm);
    }
    line[cap - 1] = L'\0';
}

static void BuildPinnedDeviceTooltip(wchar_t *out, size_t out_cap, const octans_device_view_t &d) {
    FormatSingleDeviceLine(d, out, out_cap, false);
    if (d.voltage_mv > 0) {
        wchar_t extra[32] = {};
        _snwprintf(extra, 31, L"\n%.2fV", static_cast<double>(d.voltage_mv) / 1000.0);
        size_t pos = wcslen(out);
        size_t elen = wcslen(extra);
        if (pos + elen < out_cap) {
            wmemcpy(out + pos, extra, elen);
            out[pos + elen] = L'\0';
        }
    }
}

static void RemoveTraySlot(TraySlot &slot) {
    if (slot.active && g_tray.host_hwnd) {
        NOTIFYICONDATAW nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_tray.host_hwnd;
        nid.uID = slot.uid;
        if (slot.use_guid) {
            nid.uFlags = NIF_GUID;
            nid.guidItem = slot.guid;
        }
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    if (slot.hicon) {
        DestroyIcon(slot.hicon);
        slot.hicon = nullptr;
    }
    slot.active = false;
    slot.use_guid = false;
    slot.device_id[0] = '\0';
    slot.sig_px = 0;
    slot.last_tip[0] = L'\0';
}

static void UpsertTraySlot(
    size_t slot_index,
    const char *device_id,
    const octans_device_view_t *dev,
    const wchar_t *tooltip,
    TrayStyle style,
    float tray_scale,
    int icon_px,
    bool tb_dark
) {
    if (!g_tray.host_hwnd || slot_index >= MAX_DEVICES) return;
    TraySlot &slot = g_tray.slots[slot_index];
    UINT uid = kSlotUidBase + static_cast<UINT>(slot_index);
    GUID guid = MakeSlotGuid(static_cast<uint8_t>(slot_index));

    int k_kind  = dev ? static_cast<int>(dev->kind) : -1;
    int k_pct   = dev ? static_cast<int>(dev->battery_percent) : -99;
    int k_pwr   = dev ? static_cast<int>(dev->power_state) : -1;
    int k_low   = dev ? static_cast<int>(dev->is_low_battery) : -1;
    int k_style = static_cast<int>(style);
    int k_size  = static_cast<int>(roundf(ClampF(tray_scale, 0.0f, 1.0f) * 100.0f));
    int k_dark  = tb_dark ? 1 : 0;

    bool icon_dirty = (!slot.active || !slot.hicon ||
                       slot.sig_kind != k_kind || slot.sig_pct != k_pct ||
                       slot.sig_pwr != k_pwr || slot.sig_low != k_low ||
                       slot.sig_style != k_style || slot.sig_size != k_size ||
                       slot.sig_dark != k_dark || slot.sig_px != icon_px);
    bool tip_dirty  = (!slot.active || wcscmp(slot.last_tip, tooltip) != 0);

    if (!icon_dirty && !tip_dirty) {
        if (device_id) {
            strncpy(slot.device_id, device_id, sizeof(slot.device_id) - 1);
            slot.device_id[sizeof(slot.device_id) - 1] = '\0';
        } else {
            slot.device_id[0] = '\0';
        }
        return;
    }

    HICON new_icon = nullptr;
    if (icon_dirty) {
        new_icon = RenderTrayIcon(dev, style, tray_scale, icon_px, tb_dark);
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_tray.host_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    if (icon_dirty || !slot.active) {
        nid.uFlags |= NIF_ICON;
        nid.hIcon = new_icon ? new_icon : (slot.hicon ? slot.hicon : LoadIconW(nullptr, IDI_APPLICATION));
    }
    wcsncpy(nid.szTip, tooltip ? tooltip : L"Octans", 127);
    nid.szTip[127] = L'\0';

    if (!slot.active) {
        nid.uFlags |= NIF_MESSAGE | NIF_GUID;
        nid.uCallbackMessage = WM_APP_TRAY_CALLBACK;
        nid.guidItem = guid;
        BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
        bool used_guid = true;
        if (!ok) {
            /* Fallback without NIF_GUID if GUID was previously bound to another exe path */
            nid.uFlags &= ~static_cast<UINT>(NIF_GUID);
            ok = Shell_NotifyIconW(NIM_ADD, &nid);
            used_guid = false;
        }
        if (ok) {
            NOTIFYICONDATAW vn = {};
            vn.cbSize = sizeof(vn);
            vn.hWnd = g_tray.host_hwnd;
            vn.uID = uid;
            vn.uVersion = NOTIFYICON_VERSION_4;
            if (used_guid) {
                vn.uFlags = NIF_GUID;
                vn.guidItem = guid;
            }
            Shell_NotifyIconW(NIM_SETVERSION, &vn);
            slot.active = true;
            slot.use_guid = used_guid;
            slot.uid = uid;
            slot.guid = guid;
        }
    } else {
        if (slot.use_guid) {
            nid.uFlags |= NIF_GUID;
            nid.guidItem = slot.guid;
        }
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    if (device_id) {
        strncpy(slot.device_id, device_id, sizeof(slot.device_id) - 1);
        slot.device_id[sizeof(slot.device_id) - 1] = '\0';
    } else {
        slot.device_id[0] = '\0';
    }

    if (new_icon) {
        if (slot.hicon) DestroyIcon(slot.hicon);
        slot.hicon = new_icon;
    }

    slot.sig_kind = k_kind;
    slot.sig_pct = k_pct;
    slot.sig_pwr = k_pwr;
    slot.sig_low = k_low;
    slot.sig_style = k_style;
    slot.sig_size = k_size;
    slot.sig_dark = k_dark;
    slot.sig_px = icon_px;
    wcsncpy(slot.last_tip, nid.szTip, 127);
    slot.last_tip[127] = L'\0';
}

bool TrayInit(HWND host_hwnd) {
    g_tray.host_hwnd = host_hwnd;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        g_tray.slots[i] = {};
    }

    UINT dpi = GetTaskbarDpi();
    int icon_px = GetTrayIconPx(dpi);
    bool tb_dark = DetectTaskbarDarkMode();
    const UiState &st = GetState();
    /* Initialize Slot 0 immediately so our primary taskbar slot stays stable from launch! */
    UpsertTraySlot(0, nullptr, nullptr, L"Octans", st.tray_style, st.tray_scale, icon_px, tb_dark);
    return g_tray.slots[0].active;
}

void TrayShutdown() {
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        RemoveTraySlot(g_tray.slots[i]);
    }
    g_tray.round_stroke.Reset();
    g_tray.dwrite_factory.Reset();
    g_tray.d2d_factory.Reset();
    g_tray.host_hwnd = nullptr;
}

void TraySyncAll(
    const octans_device_view_t *devices,
    size_t count,
    const char (*pinned_ids)[64],
    size_t pinned_count,
    TrayStyle style,
    float tray_scale
) {
    if (!g_tray.host_hwnd) return;
    if (count > MAX_DEVICES) count = MAX_DEVICES;
    if (pinned_count > MAX_DEVICES) pinned_count = MAX_DEVICES;

    UINT dpi = GetTaskbarDpi();
    int icon_px = GetTrayIconPx(dpi);
    bool tb_dark = DetectTaskbarDarkMode();

    /* Resolve active pinned devices in user-pinned order */
    int matched[MAX_DEVICES];
    size_t matched_count = 0;
    for (size_t p = 0; p < pinned_count; ++p) {
        if (!pinned_ids[p][0]) continue;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(devices[i].id, pinned_ids[p]) == 0) {
                matched[matched_count++] = static_cast<int>(i);
                break;
            }
        }
    }

    if (matched_count == 0) {
        int fb = SelectFallbackTrayDeviceIndex(devices, count);
        const octans_device_view_t *fb_dev = (fb >= 0 && static_cast<size_t>(fb) < count) ? &devices[fb] : nullptr;
        wchar_t tip[128] = {};
        if (fb_dev) {
            BuildPinnedDeviceTooltip(tip, 128, *fb_dev);
        } else {
            wcsncpy(tip, L"Octans - \u672a\u68c0\u6d4b\u5230\u65e0\u7ebf\u5916\u8bbe", 127);
        }
        UpsertTraySlot(0, fb_dev ? fb_dev->id : nullptr, fb_dev, tip, style, tray_scale, icon_px, tb_dark);
        for (size_t s = 1; s < MAX_DEVICES; ++s) {
            if (g_tray.slots[s].active) RemoveTraySlot(g_tray.slots[s]);
        }
        return;
    }

    for (size_t s = 0; s < matched_count; ++s) {
        const octans_device_view_t &dev = devices[matched[s]];
        wchar_t tip[128] = {};
        BuildPinnedDeviceTooltip(tip, 128, dev);
        UpsertTraySlot(s, dev.id, &dev, tip, style, tray_scale, icon_px, tb_dark);
    }

    for (size_t s = matched_count; s < MAX_DEVICES; ++s) {
        if (g_tray.slots[s].active) {
            RemoveTraySlot(g_tray.slots[s]);
        }
    }
}

void TrayHandleTaskbarCreated() {
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        g_tray.slots[i].active = false;
        g_tray.slots[i].sig_px = 0;
    }
    UiState &st = GetState();
    TraySyncAll(st.devices, st.device_count, st.pinned_ids, st.pinned_count, st.tray_style, st.tray_scale);
}

static int FindSlotAtCursor() {
    POINT cur = {};
    if (!GetCursorPos(&cur) || !g_tray.host_hwnd) return -1;
    int best = -1;
    long best_dist = 0x7FFFFFFF;
    for (size_t i = 0; i < MAX_DEVICES; ++i) {
        const TraySlot &slot = g_tray.slots[i];
        if (!slot.active) continue;
        NOTIFYICONIDENTIFIER nii = {};
        nii.cbSize = sizeof(nii);
        nii.hWnd = g_tray.host_hwnd;
        if (slot.use_guid) nii.guidItem = slot.guid;
        else nii.uID = slot.uid;
        RECT rc = {};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &rc))) {
            if (PtInRect(&rc, cur)) return static_cast<int>(i);
            long cx = (rc.left + rc.right) / 2;
            long cy = (rc.top + rc.bottom) / 2;
            long d = abs(cur.x - cx) + abs(cur.y - cy);
            if (d < best_dist && d < 64) {
                best_dist = d;
                best = static_cast<int>(i);
            }
        }
    }
    return best;
}

POINT TrayGetAnchorPoint(UINT tray_uid) {
    int slot_idx = -1;
    if (tray_uid >= kSlotUidBase && (tray_uid - kSlotUidBase) < MAX_DEVICES) {
        size_t idx = static_cast<size_t>(tray_uid - kSlotUidBase);
        if (g_tray.slots[idx].active) slot_idx = static_cast<int>(idx);
    }
    if (slot_idx < 0) {
        slot_idx = FindSlotAtCursor();
    }
    if (slot_idx < 0) {
        for (size_t i = 0; i < MAX_DEVICES; ++i) {
            if (g_tray.slots[i].active) {
                slot_idx = static_cast<int>(i);
                break;
            }
        }
    }

    if (slot_idx >= 0 && g_tray.host_hwnd) {
        const TraySlot &slot = g_tray.slots[slot_idx];
        NOTIFYICONIDENTIFIER nii = {};
        nii.cbSize = sizeof(nii);
        nii.hWnd = g_tray.host_hwnd;
        if (slot.use_guid) {
            nii.guidItem = slot.guid;
        } else {
            nii.uID = slot.uid;
        }
        RECT rc = {};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&nii, &rc))) {
            POINT pt = { (rc.left + rc.right) / 2, rc.top };
            return pt;
        }
    }
    POINT cur = {};
    GetCursorPos(&cur);
    return cur;
}

int TrayFindDeviceIndexForUid(UINT tray_uid) {
    int slot_idx = -1;
    if (tray_uid >= kSlotUidBase && (tray_uid - kSlotUidBase) < MAX_DEVICES) {
        slot_idx = static_cast<int>(tray_uid - kSlotUidBase);
    } else {
        slot_idx = FindSlotAtCursor();
    }
    if (slot_idx < 0 || static_cast<size_t>(slot_idx) >= MAX_DEVICES) return -1;
    const TraySlot &slot = g_tray.slots[slot_idx];
    if (!slot.active || !slot.device_id[0]) return -1;
    const UiState &st = GetState();
    for (size_t i = 0; i < st.device_count; ++i) {
        if (strcmp(st.devices[i].id, slot.device_id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} /* namespace octans_ui */
