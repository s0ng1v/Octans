#include "ui_glyphs.h"

namespace octans_ui {

static constexpr float kPi = 3.14159265358979323846f;

static void AddArcSegmentToSink(
    ID2D1GeometrySink *sink,
    float cx, float cy,
    float radius,
    float start_deg,
    float sweep_deg
) {
    float s_rad = start_deg * (kPi / 180.0f);
    float e_rad = (start_deg + sweep_deg) * (kPi / 180.0f);

    D2D1_POINT_2F start_pt = MakePt(cx + cosf(s_rad) * radius, cy + sinf(s_rad) * radius);
    D2D1_POINT_2F end_pt   = MakePt(cx + cosf(e_rad) * radius, cy + sinf(e_rad) * radius);

    sink->BeginFigure(start_pt, D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc = {};
    arc.point = end_pt;
    arc.size.width = radius;
    arc.size.height = radius;
    arc.rotationAngle = 0.0f;
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = (sweep_deg > 180.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
}

void DrawBatteryRingArc(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float radius,
    float stroke_width,
    float fraction,
    bool leave_bottom_notch
) {
    if (!factory || !rt || !brush) return;
    fraction = ClampF(fraction, 0.0f, 1.0f);
    if (fraction <= 0.004f) return;

    if (!leave_bottom_notch && fraction >= 0.996f) {
        rt->DrawEllipse(MakeEllipse(cx, cy, radius, radius), brush, stroke_width);
        return;
    }

    float max_sweep = leave_bottom_notch ? 330.0f : 360.0f;
    float start_deg = leave_bottom_notch ? -90.0f : -90.0f;
    float sweep_deg = fraction * max_sweep;

    /* When charging with a bottom notch, center the 30 deg gap around 6 o'clock (+90 deg).
     * That means full arc sweeps from +105 deg clockwise to +75 deg (330 deg), OR
     * for intuitive clockwise battery fill from 12 o'clock (-90 deg), we draw full 360 deg
     * and only punch the badge cleanly on top. Keeping clockwise from -90 deg looks most natural! */
    if (leave_bottom_notch) {
        sweep_deg = fraction * 360.0f;
        if (sweep_deg >= 359.5f) {
            rt->DrawEllipse(MakeEllipse(cx, cy, radius, radius), brush, stroke_width);
            return;
        }
    }

    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.Put()))) return;

    if (sweep_deg >= 359.5f) {
        /* Split near-full arc to avoid endpoint collapse */
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg, 180.0f);
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg + 180.0f, sweep_deg - 180.0f);
    } else {
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg, sweep_deg);
    }
    sink->Close();

    rt->DrawGeometry(geo.Get(), brush, stroke_width, round_cap_style);
}

static void DrawMouseGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    if (compact) {
        /* Panel-matching crisp white wireless mouse (clean white interior + crisp contour/wheel).
         * Notice brush carries the contour/ink color; on light taskbar we fill bright white inside
         * so it looks white & panel-identical rather than a dark black blob. */
        float w = s * 1.32f;
        float h = s * 1.74f;
        float stroke = s * 0.165f;
        if (stroke < 1.35f) stroke = 1.35f;

        D2D1_RECT_F body = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
        D2D1_ROUNDED_RECT rr = MakeRoundedRect(body, w * 0.48f, w * 0.48f);

        ComPtr<ID2D1SolidColorBrush> solid_src;
        D2D1_COLOR_F ink = MakeColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (SUCCEEDED(brush->QueryInterface(__uuidof(ID2D1SolidColorBrush), solid_src.PutVoid()))) {
            ink = solid_src->GetColor();
        }
        bool is_dark_contour = (ink.r + ink.g + ink.b) < 1.5f;

        if (is_dark_contour) {
            /* Light taskbar: bright pure-white mouse body + crisp dark panel-style outline & scroll wheel */
            ComPtr<ID2D1SolidColorBrush> white_fill;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.98f), white_fill.Put()))) {
                rt->FillRoundedRectangle(rr, white_fill.Get());
            }
            rt->DrawRoundedRectangle(rr, brush, stroke);
            float sw_w = s * 0.21f;
            float sw_h = s * 0.42f;
            float sw_y = cy - h * 0.19f;
            rt->FillRoundedRectangle(
                MakeRoundedRect(MakeRect(cx - sw_w * 0.5f, sw_y - sw_h * 0.5f, cx + sw_w * 0.5f, sw_y + sw_h * 0.5f), sw_w * 0.5f, sw_w * 0.5f),
                brush
            );
            float seam_y = cy - h * 0.02f;
            rt->DrawLine(MakePt(cx - w * 0.42f, seam_y), MakePt(cx + w * 0.42f, seam_y), brush, stroke * 0.72f);
        } else {
            /* Dark taskbar: crisp bright white panel-style outline + scroll wheel */
            rt->DrawRoundedRectangle(rr, brush, stroke);
            float sw_w = s * 0.21f;
            float sw_h = s * 0.42f;
            float sw_y = cy - h * 0.19f;
            rt->FillRoundedRectangle(
                MakeRoundedRect(MakeRect(cx - sw_w * 0.5f, sw_y - sw_h * 0.5f, cx + sw_w * 0.5f, sw_y + sw_h * 0.5f), sw_w * 0.5f, sw_w * 0.5f),
                brush
            );
            float seam_y = cy - h * 0.02f;
            rt->DrawLine(MakePt(cx - w * 0.42f, seam_y), MakePt(cx + w * 0.42f, seam_y), brush, stroke * 0.72f);
        }
        return;
    }

    float w = s * 1.12f;
    float h = s * 1.62f;
    float stroke = s * 0.145f;
    if (stroke < 1.15f) stroke = 1.15f;

    D2D1_RECT_F body = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    rt->DrawRoundedRectangle(MakeRoundedRect(body, w * 0.48f, w * 0.48f), brush, stroke);

    /* Scroll wheel capsule */
    float sw_w = s * 0.18f;
    float sw_h = s * 0.38f;
    float sw_y = cy - h * 0.18f;
    D2D1_RECT_F wheel = MakeRect(cx - sw_w * 0.5f, sw_y - sw_h * 0.5f, cx + sw_w * 0.5f, sw_y + sw_h * 0.5f);
    rt->FillRoundedRectangle(MakeRoundedRect(wheel, sw_w * 0.5f, sw_w * 0.5f), brush);

    /* Subtle horizontal top button seam */
    float seam_y = cy - h * 0.04f;
    rt->DrawLine(
        MakePt(cx - w * 0.42f, seam_y),
        MakePt(cx + w * 0.42f, seam_y),
        brush,
        stroke * 0.65f
    );
}

static void DrawKeyboardGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    if (compact) {
        /* Panel-matching crisp white wireless keyboard (white interior + clean outline & keycap rows) */
        float w = s * 1.88f;
        float h = s * 1.20f;
        float stroke = s * 0.16f;
        if (stroke < 1.35f) stroke = 1.35f;

        D2D1_RECT_F frame = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
        D2D1_ROUNDED_RECT rr = MakeRoundedRect(frame, s * 0.24f, s * 0.24f);

        ComPtr<ID2D1SolidColorBrush> solid_src;
        D2D1_COLOR_F ink = MakeColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (SUCCEEDED(brush->QueryInterface(__uuidof(ID2D1SolidColorBrush), solid_src.PutVoid()))) {
            ink = solid_src->GetColor();
        }
        bool is_dark_contour = (ink.r + ink.g + ink.b) < 1.5f;

        if (is_dark_contour) {
            ComPtr<ID2D1SolidColorBrush> white_fill;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.98f), white_fill.Put()))) {
                rt->FillRoundedRectangle(rr, white_fill.Get());
            }
        }
        rt->DrawRoundedRectangle(rr, brush, stroke);

        /* Crisp 2-row keycaps + wide spacebar inside */
        float pad_x = w * 0.14f;
        float iw = w - pad_x * 2.0f;
        float gap = iw * 0.08f;
        float kw = (iw - gap * 3.0f) / 4.0f;
        float kh = h * 0.17f;
        if (kh < 1.2f) kh = 1.2f;
        float r1y = frame.top + h * 0.22f;
        for (int i = 0; i < 4; ++i) {
            float x0 = frame.left + pad_x + i * (kw + gap);
            rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(x0, r1y, x0 + kw, r1y + kh), kh * 0.3f, kh * 0.3f), brush);
        }
        float r2y = frame.bottom - h * 0.22f - kh;
        rt->FillRoundedRectangle(
            MakeRoundedRect(MakeRect(frame.left + w * 0.22f, r2y, frame.right - w * 0.22f, r2y + kh), kh * 0.3f, kh * 0.3f),
            brush
        );
        return;
    }

    float w = s * 1.82f;
    float h = s * 1.18f;
    float stroke = s * 0.145f;
    if (stroke < 1.15f) stroke = 1.15f;

    D2D1_RECT_F frame = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    float rad = s * 0.24f;
    rt->DrawRoundedRectangle(MakeRoundedRect(frame, rad, rad), brush, stroke);

    /* Crisp 3-row keycap layout inside frame */
    float inner_l = frame.left + w * 0.12f;
    float inner_r = frame.right - w * 0.12f;
    float inner_w = inner_r - inner_l;
    float key_h   = h * 0.15f;
    float gap_x   = inner_w * 0.06f;

    /* Row 1: 5 keys */
    float r1_y = cy - h * 0.28f;
    float k5_w = (inner_w - gap_x * 4.0f) / 5.0f;
    for (int i = 0; i < 5; ++i) {
        float x0 = inner_l + i * (k5_w + gap_x);
        rt->FillRoundedRectangle(
            MakeRoundedRect(MakeRect(x0, r1_y, x0 + k5_w, r1_y + key_h), key_h * 0.32f, key_h * 0.32f),
            brush
        );
    }

    /* Row 2: 4 keys */
    float r2_y = cy - key_h * 0.5f;
    float k4_w = (inner_w - gap_x * 3.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        float x0 = inner_l + i * (k4_w + gap_x);
        rt->FillRoundedRectangle(
            MakeRoundedRect(MakeRect(x0, r2_y, x0 + k4_w, r2_y + key_h), key_h * 0.32f, key_h * 0.32f),
            brush
        );
    }

    /* Row 3: side key + wide spacebar + side key */
    float r3_y = cy + h * 0.28f - key_h;
    float side_w = inner_w * 0.16f;
    float sp_l = inner_l + side_w + gap_x;
    float sp_r = inner_r - side_w - gap_x;
    rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(inner_l, r3_y, inner_l + side_w, r3_y + key_h), key_h * 0.32f, key_h * 0.32f), brush);
    rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(sp_l, r3_y, sp_r, r3_y + key_h), key_h * 0.32f, key_h * 0.32f), brush);
    rt->FillRoundedRectangle(MakeRoundedRect(MakeRect(inner_r - side_w, r3_y, inner_r, r3_y + key_h), key_h * 0.32f, key_h * 0.32f), brush);
}

static void DrawHeadsetGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float stroke = compact ? (s * 0.20f) : (s * 0.16f);
    if (stroke < 1.3f) stroke = 1.3f;

    float arch_r = s * 0.72f;
    float arch_cy = cy + s * 0.08f;

    /* Headband top semicircle */
    ComPtr<ID2D1PathGeometry> geo;
    if (SUCCEEDED(factory->CreatePathGeometry(geo.Put()))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(sink.Put()))) {
            AddArcSegmentToSink(sink.Get(), cx, arch_cy, arch_r, 180.0f, 180.0f);
            sink->Close();
            rt->DrawGeometry(geo.Get(), brush, stroke);
        }
    }

    /* Left & right earcups */
    float cup_w = s * 0.38f;
    float cup_h = s * 0.70f;
    float cup_y0 = arch_cy - s * 0.06f;
    D2D1_RECT_F left_cup  = MakeRect(cx - arch_r - cup_w * 0.35f, cup_y0, cx - arch_r + cup_w * 0.65f, cup_y0 + cup_h);
    D2D1_RECT_F right_cup = MakeRect(cx + arch_r - cup_w * 0.65f, cup_y0, cx + arch_r + cup_w * 0.35f, cup_y0 + cup_h);
    D2D1_ROUNDED_RECT lrr = MakeRoundedRect(left_cup, cup_w * 0.45f, cup_w * 0.45f);
    D2D1_ROUNDED_RECT rrr = MakeRoundedRect(right_cup, cup_w * 0.45f, cup_w * 0.45f);

    if (compact) {
        ComPtr<ID2D1SolidColorBrush> solid_src;
        D2D1_COLOR_F ink = MakeColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (SUCCEEDED(brush->QueryInterface(__uuidof(ID2D1SolidColorBrush), solid_src.PutVoid()))) {
            ink = solid_src->GetColor();
        }
        bool is_dark_contour = (ink.r + ink.g + ink.b) < 1.5f;
        if (is_dark_contour) {
            ComPtr<ID2D1SolidColorBrush> white_fill;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.98f), white_fill.Put()))) {
                rt->FillRoundedRectangle(lrr, white_fill.Get());
                rt->FillRoundedRectangle(rrr, white_fill.Get());
            }
            rt->DrawRoundedRectangle(lrr, brush, stroke * 0.85f);
            rt->DrawRoundedRectangle(rrr, brush, stroke * 0.85f);
            return;
        }
    }

    rt->FillRoundedRectangle(lrr, brush);
    rt->FillRoundedRectangle(rrr, brush);
}

static void DrawEarbudsGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float dx = s * 0.42f;
    float head_r = s * 0.32f;
    float stem_w = compact ? (s * 0.24f) : (s * 0.19f);
    float stem_h = s * 0.76f;

    /* Left earbud */
    rt->FillEllipse(MakeEllipse(cx - dx, cy - s * 0.24f, head_r, head_r * 0.88f), brush);
    D2D1_RECT_F lstem = MakeRect(cx - dx - stem_w * 0.5f, cy - s * 0.10f, cx - dx + stem_w * 0.5f, cy - s * 0.10f + stem_h);
    rt->FillRoundedRectangle(MakeRoundedRect(lstem, stem_w * 0.5f, stem_w * 0.5f), brush);

    /* Right earbud */
    rt->FillEllipse(MakeEllipse(cx + dx, cy - s * 0.24f, head_r, head_r * 0.88f), brush);
    D2D1_RECT_F rstem = MakeRect(cx + dx - stem_w * 0.5f, cy - s * 0.10f, cx + dx + stem_w * 0.5f, cy - s * 0.10f + stem_h);
    rt->FillRoundedRectangle(MakeRoundedRect(rstem, stem_w * 0.5f, stem_w * 0.5f), brush);
}

static void DrawGamepadGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float w = s * 1.82f;
    float h = s * 1.10f;
    float stroke = compact ? (s * 0.16f) : (s * 0.15f);
    if (stroke < 1.3f) stroke = 1.3f;

    D2D1_RECT_F body = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    D2D1_ROUNDED_RECT brr = MakeRoundedRect(body, h * 0.48f, h * 0.48f);

    if (compact) {
        ComPtr<ID2D1SolidColorBrush> solid_src;
        D2D1_COLOR_F ink = MakeColor(1.0f, 1.0f, 1.0f, 1.0f);
        if (SUCCEEDED(brush->QueryInterface(__uuidof(ID2D1SolidColorBrush), solid_src.PutVoid()))) {
            ink = solid_src->GetColor();
        }
        if ((ink.r + ink.g + ink.b) < 1.5f) {
            ComPtr<ID2D1SolidColorBrush> white_fill;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.98f), white_fill.Put()))) {
                rt->FillRoundedRectangle(brr, white_fill.Get());
            }
        }
    }

    rt->DrawRoundedRectangle(brr, brush, stroke);

    /* Left D-pad */
    float dcx = cx - w * 0.22f;
    float arm = s * 0.23f;
    float dstroke = compact ? (s * 0.17f) : (s * 0.14f);
    if (dstroke < 1.15f) dstroke = 1.15f;
    rt->DrawLine(MakePt(dcx - arm, cy), MakePt(dcx + arm, cy), brush, dstroke);
    rt->DrawLine(MakePt(dcx, cy - arm), MakePt(dcx, cy + arm), brush, dstroke);

    /* Right action buttons */
    float bcx = cx + w * 0.22f;
    float dot = s * 0.12f;
    if (dot < 1.05f) dot = 1.05f;
    rt->FillEllipse(MakeEllipse(bcx - s * 0.13f, cy + s * 0.11f, dot, dot), brush);
    rt->FillEllipse(MakeEllipse(bcx + s * 0.13f, cy - s * 0.11f, dot, dot), brush);
}

static void DrawStylusGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    D2D1_MATRIX_3X2_F prev = {};
    rt->GetTransform(&prev);
    rt->SetTransform(MatrixRotation(40.0f, cx, cy));

    float w = compact ? (s * 0.36f) : (s * 0.28f);
    float h = s * 1.54f;
    D2D1_RECT_F shaft = MakeRect(cx - w * 0.5f, cy - h * 0.48f, cx + w * 0.5f, cy + h * 0.30f);
    rt->FillRoundedRectangle(MakeRoundedRect(shaft, w * 0.45f, w * 0.45f), brush);

    /* Tip triangle approximated by compact tapered ellipse/dot */
    rt->FillEllipse(MakeEllipse(cx, cy + h * 0.42f, w * 0.26f, w * 0.36f), brush);

    rt->SetTransform(prev);
}

static void DrawSpeakerGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float w = s * 1.18f;
    float h = s * 1.58f;
    float stroke = compact ? (s * 0.24f) : (s * 0.145f);
    if (stroke < 1.15f) stroke = 1.15f;

    D2D1_RECT_F box = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    rt->DrawRoundedRectangle(MakeRoundedRect(box, s * 0.24f, s * 0.24f), brush, stroke);

    /* Tweeter dot */
    rt->FillEllipse(MakeEllipse(cx, cy - h * 0.22f, s * 0.14f, s * 0.14f), brush);
    /* Woofer ring */
    rt->DrawEllipse(MakeEllipse(cx, cy + h * 0.16f, s * 0.30f, s * 0.30f), brush, stroke * 0.9f);
}

static void DrawGenericDeviceGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Sleek battery/dongle capsule */
    float w = s * 1.46f;
    float h = s * 0.92f;
    float stroke = compact ? (s * 0.24f) : (s * 0.15f);
    if (stroke < 1.15f) stroke = 1.15f;

    D2D1_RECT_F body = MakeRect(cx - w * 0.52f, cy - h * 0.5f, cx + w * 0.42f, cy + h * 0.5f);
    rt->DrawRoundedRectangle(MakeRoundedRect(body, s * 0.22f, s * 0.22f), brush, stroke);

    D2D1_RECT_F cap = MakeRect(body.right + s * 0.08f, cy - h * 0.22f, body.right + s * 0.24f, cy + h * 0.22f);
    rt->FillRoundedRectangle(MakeRoundedRect(cap, s * 0.07f, s * 0.07f), brush);

    float fill_pad = stroke * 1.25f;
    D2D1_RECT_F core = MakeRect(body.left + fill_pad, body.top + fill_pad, body.right - fill_pad, body.bottom - fill_pad);
    if (core.right > core.left && core.bottom > core.top) {
        rt->FillRoundedRectangle(MakeRoundedRect(core, s * 0.08f, s * 0.08f), brush);
    }
}

void DrawDeviceGlyph(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    octans_device_kind_t kind,
    float cx, float cy,
    float half_size,
    bool compact_tray
) {
    if (!rt || !brush || half_size <= 0.5f) return;

    switch (kind) {
        case OCTANS_DEV_MOUSE:
            DrawMouseGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_KEYBOARD:
            DrawKeyboardGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_HEADSET:
            DrawHeadsetGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_EARBUDS:
            DrawEarbudsGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_GAMEPAD:
            DrawGamepadGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_STYLUS:
            DrawStylusGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_SPEAKER:
            DrawSpeakerGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_OTHER:
        case OCTANS_DEV_UNKNOWN:
        default:
            DrawGenericDeviceGlyph(rt, brush, cx, cy, half_size, compact_tray);
            break;
    }
}

void DrawLightningBolt(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float h
) {
    if (!factory || !rt || !brush || h <= 0.5f) return;

    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.Put()))) return;

    /* Apple-style crisp lightning bolt proportions */
    float w = h * 0.68f;
    sink->BeginFigure(MakePt(cx + w * 0.16f, cy - h), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(MakePt(cx - w * 0.72f, cy + h * 0.10f));
    sink->AddLine(MakePt(cx - w * 0.04f, cy + h * 0.10f));
    sink->AddLine(MakePt(cx - w * 0.18f, cy + h));
    sink->AddLine(MakePt(cx + w * 0.72f, cy - h * 0.12f));
    sink->AddLine(MakePt(cx + w * 0.04f, cy - h * 0.12f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();

    rt->FillGeometry(geo.Get(), brush);
}

void DrawPinIcon(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float s,
    bool filled
) {
    if (!factory || !rt || !brush) return;

    D2D1_MATRIX_3X2_F prev = {};
    rt->GetTransform(&prev);
    rt->SetTransform(MatrixRotation(38.0f, cx, cy));

    float stroke = 1.25f;
    /* Pin head */
    D2D1_RECT_F head = MakeRect(cx - s * 0.46f, cy - s * 0.80f, cx + s * 0.46f, cy - s * 0.46f);
    if (filled) {
        rt->FillRoundedRectangle(MakeRoundedRect(head, s * 0.16f, s * 0.16f), brush);
    } else {
        rt->DrawRoundedRectangle(MakeRoundedRect(head, s * 0.16f, s * 0.16f), brush, stroke);
    }

    /* Pin waist + flange */
    D2D1_RECT_F flange = MakeRect(cx - s * 0.62f, cy - s * 0.08f, cx + s * 0.62f, cy + s * 0.18f);
    if (filled) {
        rt->FillRoundedRectangle(MakeRoundedRect(flange, s * 0.12f, s * 0.12f), brush);
        D2D1_RECT_F neck = MakeRect(cx - s * 0.28f, cy - s * 0.50f, cx + s * 0.28f, cy - s * 0.04f);
        rt->FillRectangle(neck, brush);
    } else {
        rt->DrawLine(MakePt(cx - s * 0.28f, cy - s * 0.46f), MakePt(cx - s * 0.34f, cy - s * 0.08f), brush, stroke);
        rt->DrawLine(MakePt(cx + s * 0.28f, cy - s * 0.46f), MakePt(cx + s * 0.34f, cy - s * 0.08f), brush, stroke);
        rt->DrawLine(MakePt(cx - s * 0.58f, cy - s * 0.02f), MakePt(cx + s * 0.58f, cy - s * 0.02f), brush, stroke);
    }

    /* Needle */
    rt->DrawLine(MakePt(cx, cy + s * 0.16f), MakePt(cx, cy + s * 0.85f), brush, stroke);

    rt->SetTransform(prev);
}

void DrawRefreshIcon(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float r,
    float rotation_deg
) {
    if (!factory || !rt || !brush) return;

    D2D1_MATRIX_3X2_F prev = {};
    rt->GetTransform(&prev);
    if (fabsf(rotation_deg) > 0.1f) {
        rt->SetTransform(MatrixRotation(rotation_deg, cx, cy));
    }

    float stroke = 1.55f;
    ComPtr<ID2D1PathGeometry> geo;
    if (SUCCEEDED(factory->CreatePathGeometry(geo.Put()))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(sink.Put()))) {
            /* 280-degree clockwise arc ending near top-right (-45 deg) */
            AddArcSegmentToSink(sink.Get(), cx, cy, r, 35.0f, 280.0f);
            sink->Close();
            rt->DrawGeometry(geo.Get(), brush, stroke, round_cap_style);
        }
    }

    /* Crisp SF-Symbols-style filled arrowhead tangent at 315 deg (top-right) */
    float ang = 315.0f * (kPi / 180.0f);
    float tip_x = cx + cosf(ang) * r;
    float tip_y = cy + sinf(ang) * r;
    float ah = r * 0.50f;
    ComPtr<ID2D1PathGeometry> head;
    if (SUCCEEDED(factory->CreatePathGeometry(head.Put()))) {
        ComPtr<ID2D1GeometrySink> hs;
        if (SUCCEEDED(head->Open(hs.Put()))) {
            hs->BeginFigure(MakePt(tip_x + ah * 0.35f, tip_y + ah * 0.35f), D2D1_FIGURE_BEGIN_FILLED);
            hs->AddLine(MakePt(tip_x - ah * 0.85f, tip_y + ah * 0.22f));
            hs->AddLine(MakePt(tip_x + ah * 0.22f, tip_y - ah * 0.85f));
            hs->EndFigure(D2D1_FIGURE_END_CLOSED);
            hs->Close();
            rt->FillGeometry(head.Get(), brush);
        }
    }

    rt->SetTransform(prev);
}

void DrawMoreIcon(
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float r
) {
    if (!rt || !brush) return;
    float dot_r = r * 0.24f;
    if (dot_r < 1.3f) dot_r = 1.3f;
    float gap = r * 0.72f;
    rt->FillEllipse(MakeEllipse(cx - gap, cy, dot_r, dot_r), brush);
    rt->FillEllipse(MakeEllipse(cx, cy, dot_r, dot_r), brush);
    rt->FillEllipse(MakeEllipse(cx + gap, cy, dot_r, dot_r), brush);
}

} /* namespace octans_ui */
