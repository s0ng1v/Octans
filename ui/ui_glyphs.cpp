#include "ui_glyphs.h"
#include "ui_brand.h"

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

    float start_deg = -90.0f;
    float sweep_deg = fraction * 360.0f;
    if (sweep_deg >= 359.5f) {
        rt->DrawEllipse(MakeEllipse(cx, cy, radius, radius), brush, stroke_width);
        return;
    }

    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.Put()))) return;

    if (sweep_deg >= 359.5f) {
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg, 180.0f);
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg + 180.0f, sweep_deg - 180.0f);
    } else {
        AddArcSegmentToSink(sink.Get(), cx, cy, radius, start_deg, sweep_deg);
    }
    sink->Close();

    rt->DrawGeometry(geo.Get(), brush, stroke_width, round_cap_style);
}

/* ============================================================================
 * Sleek 24x24 Outline Peripheral Icons (from Octans Battery UI Kit)
 * Mapped cleanly around (cx, cy) with scale = half_size / 10.5f
 * ============================================================================ */

static void CreateRoundStroke(ID2D1Factory *factory, ComPtr<ID2D1StrokeStyle> &out) {
    if (!factory) return;
    D2D1_STROKE_STYLE_PROPERTIES sp = {};
    sp.startCap = D2D1_CAP_STYLE_ROUND;
    sp.endCap = D2D1_CAP_STYLE_ROUND;
    sp.dashCap = D2D1_CAP_STYLE_ROUND;
    sp.lineJoin = D2D1_LINE_JOIN_ROUND;
    sp.miterLimit = 10.0f;
    sp.dashStyle = D2D1_DASH_STYLE_SOLID;
    factory->CreateStrokeStyle(sp, nullptr, 0, out.Put());
}

static bool IsDarkContourInk(ID2D1Brush *brush) {
    ComPtr<ID2D1SolidColorBrush> solid_src;
    if (brush && SUCCEEDED(brush->QueryInterface(__uuidof(ID2D1SolidColorBrush), solid_src.PutVoid()))) {
        D2D1_COLOR_F ink = solid_src->GetColor();
        return (ink.r + ink.g + ink.b) < 1.5f;
    }
    return false;
}

static void DrawMouseGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Kit mouse-outline (viewBox 0 0 24 24):
     *   rect x=6.5 y=3.2 w=11 h=17.6 rx=5.5
     *   scroll line x=12 y=6.6..10.2 (round cap) */
    float u = s / (compact ? 9.4f : 10.2f);
    float ox = cx - 12.0f * u;
    float oy = cy - 12.0f * u;
    float stroke = (compact ? 2.15f : 1.85f) * u;
    if (stroke < (compact ? 1.35f : 1.20f)) stroke = compact ? 1.35f : 1.20f;

    ComPtr<ID2D1StrokeStyle> rcap;
    CreateRoundStroke(factory, rcap);

    D2D1_RECT_F body = MakeRect(ox + 6.5f * u, oy + 3.2f * u, ox + 17.5f * u, oy + 20.8f * u);
    D2D1_ROUNDED_RECT rr = MakeRoundedRect(body, 5.5f * u, 5.5f * u);

    if (compact && IsDarkContourInk(brush)) {
        ComPtr<ID2D1SolidColorBrush> wf;
        if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.96f), wf.Put()))) {
            rt->FillRoundedRectangle(rr, wf.Get());
        }
    }
    rt->DrawRoundedRectangle(rr, brush, stroke);
    rt->DrawLine(
        MakePt(ox + 12.0f * u, oy + 6.6f * u),
        MakePt(ox + 12.0f * u, oy + 10.2f * u),
        brush,
        stroke * 1.06f,
        rcap.Get()
    );
}

static void DrawKeyboardGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Kit keyboard-outline (viewBox 0 0 24 24):
     *   rect x=2.6 y=6.2 w=18.8 h=11.6 rx=2.6
     *   4 top-row key dashes at y=10 (6.2..7.6, 9.6..11.0, 13.0..14.4, 16.4..17.8)
     *   spacebar dash at y=14 (7.5..16.5) */
    float u = s / (compact ? 10.0f : 10.6f);
    float ox = cx - 12.0f * u;
    float oy = cy - 12.0f * u;
    float stroke = (compact ? 2.10f : 1.78f) * u;
    if (stroke < (compact ? 1.30f : 1.15f)) stroke = compact ? 1.30f : 1.15f;

    ComPtr<ID2D1StrokeStyle> rcap;
    CreateRoundStroke(factory, rcap);

    D2D1_RECT_F frame = MakeRect(ox + 2.6f * u, oy + 6.2f * u, ox + 21.4f * u, oy + 17.8f * u);
    D2D1_ROUNDED_RECT rr = MakeRoundedRect(frame, 2.6f * u, 2.6f * u);

    if (compact && IsDarkContourInk(brush)) {
        ComPtr<ID2D1SolidColorBrush> wf;
        if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.96f), wf.Put()))) {
            rt->FillRoundedRectangle(rr, wf.Get());
        }
    }
    rt->DrawRoundedRectangle(rr, brush, stroke);

    static const float kx[4][2] = {
        { 6.2f, 7.6f }, { 9.6f, 11.0f }, { 13.0f, 14.4f }, { 16.4f, 17.8f }
    };
    float kstroke = stroke * 0.96f;
    for (int i = 0; i < 4; ++i) {
        rt->DrawLine(
            MakePt(ox + kx[i][0] * u, oy + 10.0f * u),
            MakePt(ox + kx[i][1] * u, oy + 10.0f * u),
            brush,
            kstroke,
            rcap.Get()
        );
    }
    rt->DrawLine(
        MakePt(ox + 7.5f * u, oy + 14.0f * u),
        MakePt(ox + 16.5f * u, oy + 14.0f * u),
        brush,
        kstroke,
        rcap.Get()
    );
}

static void DrawHeadsetGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Kit headset-outline (viewBox 0 0 24 24):
     *   headband M4.5 14.5 V12 A7.5 7.5 0 0 1 19.5 12 V14.5
     *   left cup  x=3.5 y=13.2 w=3.6 h=6.4 rx=1.8
     *   right cup x=16.9 y=13.2 w=3.6 h=6.4 rx=1.8 */
    float u = s / (compact ? 9.8f : 10.4f);
    float ox = cx - 12.0f * u;
    float oy = cy - 12.0f * u;
    float stroke = (compact ? 2.15f : 1.80f) * u;
    if (stroke < (compact ? 1.35f : 1.18f)) stroke = compact ? 1.35f : 1.18f;

    ComPtr<ID2D1StrokeStyle> rcap;
    CreateRoundStroke(factory, rcap);

    if (factory) {
        ComPtr<ID2D1PathGeometry> geo;
        if (SUCCEEDED(factory->CreatePathGeometry(geo.Put()))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(geo->Open(sink.Put()))) {
                sink->BeginFigure(MakePt(ox + 4.5f * u, oy + 14.5f * u), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine(MakePt(ox + 4.5f * u, oy + 12.0f * u));
                D2D1_ARC_SEGMENT arc = {};
                arc.point = MakePt(ox + 19.5f * u, oy + 12.0f * u);
                arc.size.width = 7.5f * u;
                arc.size.height = 7.5f * u;
                arc.rotationAngle = 0.0f;
                arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                arc.arcSize = D2D1_ARC_SIZE_SMALL;
                sink->AddArc(arc);
                sink->AddLine(MakePt(ox + 19.5f * u, oy + 14.5f * u));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                rt->DrawGeometry(geo.Get(), brush, stroke, rcap.Get());
            }
        }
    }

    D2D1_RECT_F lc = MakeRect(ox + 3.5f * u, oy + 13.2f * u, ox + 7.1f * u, oy + 19.6f * u);
    D2D1_RECT_F rc = MakeRect(ox + 16.9f * u, oy + 13.2f * u, ox + 20.5f * u, oy + 19.6f * u);
    D2D1_ROUNDED_RECT lrr = MakeRoundedRect(lc, 1.8f * u, 1.8f * u);
    D2D1_ROUNDED_RECT rrr = MakeRoundedRect(rc, 1.8f * u, 1.8f * u);

    if (compact && IsDarkContourInk(brush)) {
        ComPtr<ID2D1SolidColorBrush> wf;
        if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.96f), wf.Put()))) {
            rt->FillRoundedRectangle(lrr, wf.Get());
            rt->FillRoundedRectangle(rrr, wf.Get());
        }
    }
    rt->DrawRoundedRectangle(lrr, brush, stroke);
    rt->DrawRoundedRectangle(rrr, brush, stroke);
}

static void BuildGamepadPath24(ID2D1Factory *factory, float ox, float oy, float u, ComPtr<ID2D1PathGeometry> &out_geo) {
    if (!factory || FAILED(factory->CreatePathGeometry(out_geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(out_geo->Open(sink.Put()))) return;

    auto p = [&](float x, float y) { return MakePt(ox + x * u, oy + y * u); };
    auto bez = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
        D2D1_BEZIER_SEGMENT b = { p(x1, y1), p(x2, y2), p(x3, y3) };
        sink->AddBezier(b);
    };

    sink->BeginFigure(p(7.4f, 5.8f), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(p(16.6f, 5.8f));
    bez(18.8f, 5.8f, 20.6f, 7.4f, 21.0f, 9.6f);
    sink->AddLine(p(22.1f, 15.8f));
    bez(22.4f, 17.5f, 21.1f, 19.0f, 19.4f, 19.0f);
    bez(18.4f, 19.0f, 17.5f, 18.5f, 16.9f, 17.7f);
    sink->AddLine(p(15.4f, 15.7f));
    bez(14.9f, 15.0f, 14.1f, 14.6f, 13.3f, 14.6f);
    sink->AddLine(p(10.7f, 14.6f));
    bez(9.9f, 14.6f, 9.1f, 15.0f, 8.6f, 15.7f);
    sink->AddLine(p(7.1f, 17.7f));
    bez(6.5f, 18.5f, 5.6f, 19.0f, 4.6f, 19.0f);
    bez(2.9f, 19.0f, 1.6f, 17.5f, 1.9f, 15.8f);
    sink->AddLine(p(3.0f, 9.6f));
    bez(3.4f, 7.4f, 5.2f, 5.8f, 7.4f, 5.8f);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
}

static void DrawGamepadGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Kit gamepad-outline (viewBox 0 0 24 24) */
    float u = s / (compact ? 10.2f : 10.8f);
    float ox = cx - 12.0f * u;
    float oy = cy - 12.0f * u;
    float stroke = (compact ? 2.05f : 1.75f) * u;
    if (stroke < (compact ? 1.28f : 1.15f)) stroke = compact ? 1.28f : 1.15f;

    ComPtr<ID2D1StrokeStyle> rcap;
    CreateRoundStroke(factory, rcap);

    ComPtr<ID2D1PathGeometry> geo;
    BuildGamepadPath24(factory, ox, oy, u, geo);
    if (geo) {
        if (compact && IsDarkContourInk(brush)) {
            ComPtr<ID2D1SolidColorBrush> wf;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.96f), wf.Put()))) {
                rt->FillGeometry(geo.Get(), wf.Get());
            }
        }
        rt->DrawGeometry(geo.Get(), brush, stroke, rcap.Get());
    }

    /* Left D-pad cross */
    float ds = stroke * 0.96f;
    rt->DrawLine(MakePt(ox + 6.8f * u, oy + 10.6f * u), MakePt(ox + 10.0f * u, oy + 10.6f * u), brush, ds, rcap.Get());
    rt->DrawLine(MakePt(ox + 8.4f * u, oy + 9.0f * u), MakePt(ox + 8.4f * u, oy + 12.2f * u), brush, ds, rcap.Get());

    /* Right 2 action buttons */
    float dr = 1.02f * u;
    if (dr < 0.95f) dr = 0.95f;
    rt->FillEllipse(MakeEllipse(ox + 15.4f * u, oy + 9.9f * u, dr, dr), brush);
    rt->FillEllipse(MakeEllipse(ox + 17.3f * u, oy + 11.5f * u, dr, dr), brush);
}

static void BuildSingleEarbudPath24(
    ID2D1GeometrySink *sink,
    float ox, float oy, float u,
    bool mirror_right
) {
    /* Iconic AirPods / TWS silhouette in 24x24 viewBox (compact table keeps footprint tiny). */
    static const float kBeziers[6][6] = {
        { 3.1f,  5.0f,  4.9f,  3.5f,  7.4f,  3.7f  }, /* Top dome */
        { 9.4f,  3.9f, 10.6f,  5.4f, 10.6f,  7.4f  }, /* Upper inward nozzle */
        {10.6f,  9.5f,  9.2f, 11.1f,  7.2f, 11.2f  }, /* Lower nozzle */
        { 6.5f, 11.25f, 6.15f,11.6f,  6.15f,12.4f  }, /* Stem inner shoulder */
        { 6.15f,19.8f,  5.48f,20.4f,  4.62f,20.4f  }, /* Stem bottom right */
        { 3.76f,20.4f,  3.1f, 19.8f,  3.1f, 18.8f  }  /* Stem bottom left */
    };
    auto pt = [&](float x, float y) {
        return MakePt(ox + (mirror_right ? (24.0f - x) : x) * u, oy + y * u);
    };
    sink->BeginFigure(pt(3.1f, 8.0f), D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 0; i < 6; ++i) {
        if (i == 4) sink->AddLine(pt(6.15f, 18.8f));
        D2D1_BEZIER_SEGMENT b = {
            pt(kBeziers[i][0], kBeziers[i][1]),
            pt(kBeziers[i][2], kBeziers[i][3]),
            pt(kBeziers[i][4], kBeziers[i][5])
        };
        sink->AddBezier(b);
    }
    sink->AddLine(pt(3.1f, 8.0f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

static void BuildEarbudsPath24(ID2D1Factory *factory, float ox, float oy, float u, ComPtr<ID2D1PathGeometry> &out_geo) {
    if (!factory || FAILED(factory->CreatePathGeometry(out_geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(out_geo->Open(sink.Put()))) return;
    BuildSingleEarbudPath24(sink.Get(), ox, oy, u, false);
    BuildSingleEarbudPath24(sink.Get(), ox, oy, u, true);
    sink->Close();
}

static void DrawEarbudsGlyph(ID2D1Factory *factory, ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    /* Sleek 24x24 AirPods / TWS outline pair matching Mouse/Keyboard/Headset/Gamepad style */
    float u = s / (compact ? 9.8f : 10.4f);
    float ox = cx - 12.0f * u;
    float oy = cy - 12.0f * u;
    float stroke = (compact ? 1.90f : 1.70f) * u;
    if (stroke < (compact ? 1.22f : 1.12f)) stroke = compact ? 1.22f : 1.12f;

    ComPtr<ID2D1StrokeStyle> rcap;
    CreateRoundStroke(factory, rcap);

    ComPtr<ID2D1PathGeometry> geo;
    BuildEarbudsPath24(factory, ox, oy, u, geo);
    if (geo) {
        if (compact && IsDarkContourInk(brush)) {
            ComPtr<ID2D1SolidColorBrush> wf;
            if (SUCCEEDED(rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.96f), wf.Put()))) {
                rt->FillGeometry(geo.Get(), wf.Get());
            }
        }
        rt->DrawGeometry(geo.Get(), brush, stroke, rcap.Get());
    }

    /* Subtle acoustic speaker vent dots centered inside each bulbous ear head */
    float vr = 0.85f * u;
    if (vr >= 0.72f && !compact) {
        rt->FillEllipse(MakeEllipse(ox + 7.2f * u, oy + 7.3f * u, vr, vr * 1.12f), brush);
        rt->FillEllipse(MakeEllipse(ox + 16.8f * u, oy + 7.3f * u, vr, vr * 1.12f), brush);
    }
}

static void DrawStylusGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    D2D1_MATRIX_3X2_F prev = {};
    rt->GetTransform(&prev);
    rt->SetTransform(MatrixRotation(40.0f, cx, cy));

    float w = compact ? (s * 0.36f) : (s * 0.28f);
    float h = s * 1.54f;
    D2D1_RECT_F shaft = MakeRect(cx - w * 0.5f, cy - h * 0.48f, cx + w * 0.5f, cy + h * 0.30f);
    rt->FillRoundedRectangle(MakeRoundedRect(shaft, w * 0.45f, w * 0.45f), brush);
    rt->FillEllipse(MakeEllipse(cx, cy + h * 0.42f, w * 0.26f, w * 0.36f), brush);

    rt->SetTransform(prev);
}

static void DrawSpeakerGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float w = s * 1.18f;
    float h = s * 1.58f;
    float stroke = compact ? (s * 0.22f) : (s * 0.145f);
    if (stroke < 1.15f) stroke = 1.15f;

    D2D1_RECT_F box = MakeRect(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    rt->DrawRoundedRectangle(MakeRoundedRect(box, s * 0.24f, s * 0.24f), brush, stroke);
    rt->FillEllipse(MakeEllipse(cx, cy - h * 0.22f, s * 0.14f, s * 0.14f), brush);
    rt->DrawEllipse(MakeEllipse(cx, cy + h * 0.16f, s * 0.30f, s * 0.30f), brush, stroke * 0.9f);
}

static void DrawGenericDeviceGlyph(ID2D1RenderTarget *rt, ID2D1Brush *brush, float cx, float cy, float s, bool compact) {
    float w = s * 1.46f;
    float h = s * 0.92f;
    float stroke = compact ? (s * 0.22f) : (s * 0.15f);
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
            DrawMouseGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_KEYBOARD:
            DrawKeyboardGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_HEADSET:
            DrawHeadsetGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_EARBUDS:
            DrawEarbudsGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
            break;
        case OCTANS_DEV_GAMEPAD:
            DrawGamepadGlyph(factory, rt, brush, cx, cy, half_size, compact_tray);
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

/* ============================================================================
 * Scheme 2 (Nebula): Liquid Silhouette Fill (48x48 viewBox from UI Kit)
 * ============================================================================ */

static bool BuildSolidSilhouette48(
    ID2D1Factory *factory,
    octans_device_kind_t kind,
    float ox, float oy, float u,
    ComPtr<ID2D1Geometry> &out_geo
) {
    if (!factory) return false;
    auto rrect = [&](float x, float y, float w, float h, float rx, ComPtr<ID2D1RoundedRectangleGeometry> &rr) {
        D2D1_ROUNDED_RECT d = MakeRoundedRect(MakeRect(ox + x * u, oy + y * u, ox + (x + w) * u, oy + (y + h) * u), rx * u, rx * u);
        return SUCCEEDED(factory->CreateRoundedRectangleGeometry(d, rr.Put()));
    };

    switch (kind) {
        case OCTANS_DEV_MOUSE: {
            ComPtr<ID2D1RoundedRectangleGeometry> g;
            if (!rrect(12.0f, 5.0f, 24.0f, 38.0f, 12.0f, g)) return false;
            out_geo.Reset(g.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_KEYBOARD: {
            ComPtr<ID2D1RoundedRectangleGeometry> g;
            if (!rrect(4.0f, 12.0f, 40.0f, 24.0f, 5.5f, g)) return false;
            out_geo.Reset(g.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_HEADSET: {
            /* Headband arch + left & right solid earcups grouped cleanly */
            ComPtr<ID2D1PathGeometry> arch;
            if (FAILED(factory->CreatePathGeometry(arch.Put()))) return false;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(arch->Open(sink.Put()))) return false;
            auto p = [&](float x, float y) { return MakePt(ox + x * u, oy + y * u); };
            sink->BeginFigure(p(9.0f, 28.0f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p(9.0f, 23.0f));
            D2D1_ARC_SEGMENT a_out = { p(39.0f, 23.0f), { 15.0f * u, 15.0f * u }, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
            sink->AddArc(a_out);
            sink->AddLine(p(39.0f, 28.0f));
            sink->AddLine(p(35.0f, 28.0f));
            sink->AddLine(p(35.0f, 23.0f));
            D2D1_ARC_SEGMENT a_in = { p(13.0f, 23.0f), { 11.0f * u, 11.0f * u }, 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL };
            sink->AddArc(a_in);
            sink->AddLine(p(13.0f, 28.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();

            ComPtr<ID2D1RoundedRectangleGeometry> lc, rc;
            if (!rrect(6.0f, 25.0f, 8.5f, 15.0f, 4.2f, lc)) return false;
            if (!rrect(33.5f, 25.0f, 8.5f, 15.0f, 4.2f, rc)) return false;

            ID2D1Geometry *parts[3] = { arch.Get(), lc.Get(), rc.Get() };
            ComPtr<ID2D1GeometryGroup> grp;
            if (FAILED(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 3, grp.Put()))) return false;
            out_geo.Reset(grp.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_GAMEPAD: {
            ComPtr<ID2D1PathGeometry> gp;
            BuildGamepadPath24(factory, ox, oy, u * 2.0f, gp);
            if (!gp) return false;
            out_geo.Reset(gp.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_EARBUDS: {
            ComPtr<ID2D1PathGeometry> eb;
            BuildEarbudsPath24(factory, ox, oy, u * 2.0f, eb);
            if (!eb) return false;
            out_geo.Reset(eb.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_SPEAKER: {
            ComPtr<ID2D1RoundedRectangleGeometry> g;
            if (!rrect(11.0f, 6.0f, 26.0f, 36.0f, 6.5f, g)) return false;
            out_geo.Reset(g.Get());
            out_geo->AddRef();
            return true;
        }
        case OCTANS_DEV_STYLUS: {
            /* Sleek diagonal stylus silhouette in 48x48 space */
            ComPtr<ID2D1PathGeometry> pen;
            if (FAILED(factory->CreatePathGeometry(pen.Put()))) return false;
            ComPtr<ID2D1GeometrySink> sink;
            if (FAILED(pen->Open(sink.Put()))) return false;
            auto p = [&](float x, float y) { return MakePt(ox + x * u, oy + y * u); };
            sink->BeginFigure(p(33.5f, 7.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(p(40.5f, 14.5f));
            sink->AddLine(p(19.0f, 36.0f));
            sink->AddLine(p(8.5f, 39.5f));
            sink->AddLine(p(12.0f, 29.0f));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            out_geo.Reset(pen.Get());
            out_geo->AddRef();
            return true;
        }
        default: {
            /* Generic vertical battery capsule */
            ComPtr<ID2D1RoundedRectangleGeometry> body, cap;
            if (!rrect(12.0f, 9.0f, 24.0f, 34.0f, 5.5f, body)) return false;
            if (!rrect(18.0f, 5.0f, 12.0f, 4.5f, 2.0f, cap)) return false;
            ID2D1Geometry *parts[2] = { body.Get(), cap.Get() };
            ComPtr<ID2D1GeometryGroup> grp;
            if (FAILED(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 2, grp.Put()))) return false;
            out_geo.Reset(grp.Get());
            out_geo->AddRef();
            return true;
        }
    }
}

static void GetLiquidSilhouetteBounds48(octans_device_kind_t kind, float *out_top, float *out_bot) {
    switch (kind) {
        case OCTANS_DEV_MOUSE:    *out_top = 5.0f;  *out_bot = 43.0f; break;
        case OCTANS_DEV_KEYBOARD: *out_top = 12.0f; *out_bot = 36.0f; break;
        case OCTANS_DEV_HEADSET:  *out_top = 8.0f;  *out_bot = 40.0f; break;
        case OCTANS_DEV_EARBUDS:  *out_top = 7.6f;  *out_bot = 40.8f; break;
        case OCTANS_DEV_GAMEPAD:  *out_top = 10.5f; *out_bot = 37.8f; break;
        default:                  *out_top = 6.0f;  *out_bot = 43.0f; break;
    }
}

static bool BuildLiquidWaveGeometry(
    ID2D1Factory *factory,
    float ox, float oy, float u,
    float y_fill_48,
    float amp_48,
    float shift_x_48,
    bool invert_phase,
    ComPtr<ID2D1PathGeometry> &out_geo
) {
    if (!factory || FAILED(factory->CreatePathGeometry(out_geo.Put()))) return false;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(out_geo->Open(sink.Put()))) return false;

    /* Periodic 48-unit wave spanning [-48 .. +96] so any shift in [-48 .. 0] covers [0 .. 48] seamlessly.
     * Matches octans-liquid-battery.js: Q 12 (y - amp) 24 y T 48 y ... */
    auto pt = [&](float x48, float y48) {
        return MakePt(ox + (x48 + shift_x_48) * u, oy + y48 * u);
    };
    float a = invert_phase ? (-amp_48) : amp_48;

    sink->BeginFigure(pt(0.0f, y_fill_48), D2D1_FIGURE_BEGIN_FILLED);
    for (int cyc = 0; cyc < 2; ++cyc) {
        float x0 = static_cast<float>(cyc) * 48.0f;
        D2D1_QUADRATIC_BEZIER_SEGMENT q1 = { pt(x0 + 12.0f, y_fill_48 - a), pt(x0 + 24.0f, y_fill_48) };
        D2D1_QUADRATIC_BEZIER_SEGMENT q2 = { pt(x0 + 36.0f, y_fill_48 + a), pt(x0 + 48.0f, y_fill_48) };
        sink->AddQuadraticBezier(q1);
        sink->AddQuadraticBezier(q2);
    }
    sink->AddLine(pt(96.0f, 52.0f));
    sink->AddLine(pt(0.0f, 52.0f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    return true;
}

void DrawLiquidSilhouetteGlyph(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1StrokeStyle *round_cap_style,
    octans_device_kind_t kind,
    float cx, float cy,
    float size,
    float fraction,
    const D2D1_COLOR_F &liq_top,
    const D2D1_COLOR_F &liq_mid,
    const D2D1_COLOR_F &liq_bot,
    const D2D1_COLOR_F &wave_back,
    const D2D1_COLOR_F &track_col,
    const D2D1_COLOR_F &detail_col,
    float wave_time_sec,
    float alpha
) {
    if (!factory || !rt || size <= 2.0f || alpha <= 0.004f) return;
    float u = size / 48.0f;
    float ox = cx - size * 0.5f;
    float oy = cy - size * 0.5f;

    ComPtr<ID2D1Geometry> mask_geo;
    if (!BuildSolidSilhouette48(factory, kind, ox, oy, u, mask_geo)) return;

    ComPtr<ID2D1Layer> layer;
    if (FAILED(rt->CreateLayer(layer.Put()))) return;

    auto mul_a = [&](D2D1_COLOR_F c, float extra = 1.0f) {
        c.a *= (alpha * extra);
        return c;
    };

    ComPtr<ID2D1SolidColorBrush> br;
    if (FAILED(rt->CreateSolidColorBrush(mul_a(track_col), br.Put()))) return;

    D2D1_LAYER_PARAMETERS lp = {};
    lp.contentBounds = MakeRect(ox - 2.0f, oy - 2.0f, ox + size + 2.0f, oy + size + 2.0f);
    lp.geometricMask = mask_geo.Get();
    lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    lp.maskTransform = MatrixIdentity();
    lp.opacity = 1.0f;
    lp.opacityBrush = nullptr;
    lp.layerOptions = D2D1_LAYER_OPTIONS_NONE;

    rt->PushLayer(lp, layer.Get());

    /* 1. Empty silhouette track */
    br->SetColor(mul_a(track_col));
    rt->FillRectangle(MakeRect(ox, oy, ox + size, oy + size), br.Get());

    /* 2. Bottom-up translucent 3-stop vertical gradient (+ gentle dual-layer water wave in panel) */
    float f = ClampF(fraction, 0.0f, 1.0f);
    if (f > 0.005f) {
        float b_top = 5.0f, b_bot = 43.0f;
        GetLiquidSilhouetteBounds48(kind, &b_top, &b_bot);
        float y_fill_48 = b_bot - f * (b_bot - b_top);
        bool animated = (wave_time_sec >= 0.0f);

        float grad_y0_48 = animated ? ((y_fill_48 > 2.0f) ? (y_fill_48 - 2.0f) : 0.0f) : y_fill_48;
        D2D1_GRADIENT_STOP stops[3] = {
            { 0.00f, mul_a(liq_top) },
            { animated ? 0.38f : 0.45f, mul_a(liq_mid) },
            { 1.00f, mul_a(liq_bot) }
        };
        ComPtr<ID2D1GradientStopCollection> stop_coll;
        ComPtr<ID2D1LinearGradientBrush> grad_br;
        if (SUCCEEDED(rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, stop_coll.Put()))) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lgp = {};
            lgp.startPoint = MakePt(ox, oy + grad_y0_48 * u);
            lgp.endPoint   = MakePt(ox, oy + b_bot * u);
            rt->CreateLinearGradientBrush(lgp, stop_coll.Get(), grad_br.Put());
        }
        ID2D1Brush *fill_brush = grad_br ? static_cast<ID2D1Brush *>(grad_br.Get()) : static_cast<ID2D1Brush *>(br.Get());
        if (!grad_br) br->SetColor(mul_a(liq_mid));

        if (animated && f < 0.995f) {
            float amp = (f >= 0.98f || f <= 0.03f) ? 0.6f : 2.1f;

            /* Back wave: drifts gently right over 5.6s */
            float pb = fmodf(wave_time_sec / 5.6f, 1.0f);
            if (pb < 0.0f) pb += 1.0f;
            float shift_b = -48.0f + pb * 48.0f;
            ComPtr<ID2D1PathGeometry> wb_geo;
            if (BuildLiquidWaveGeometry(factory, ox, oy, u, y_fill_48 - 0.8f, amp * 0.9f, shift_b, true, wb_geo)) {
                br->SetColor(mul_a(wave_back, 0.48f));
                rt->FillGeometry(wb_geo.Get(), br.Get());
            }

            /* Front wave: drifts smoothly left over 3.8s with 3-stop gradient */
            float pf = fmodf(wave_time_sec / 3.8f, 1.0f);
            if (pf < 0.0f) pf += 1.0f;
            float shift_f = -pf * 48.0f;
            ComPtr<ID2D1PathGeometry> wf_geo;
            if (BuildLiquidWaveGeometry(factory, ox, oy, u, y_fill_48, amp, shift_f, false, wf_geo)) {
                rt->FillGeometry(wf_geo.Get(), fill_brush);
            }
        } else {
            float fill_top = (f >= 0.995f) ? oy : (oy + y_fill_48 * u);
            rt->FillRectangle(MakeRect(ox, fill_top, ox + size, oy + size), fill_brush);
        }
    }

    rt->PopLayer();

    /* 3. Surface-colored inner detail overlay (scroll wheel, keys, D-pad/buttons) */
    float ds = 2.6f * u;
    if (ds < 1.3f) ds = 1.3f;

    if (kind == OCTANS_DEV_MOUSE) {
        br->SetColor(mul_a(detail_col, 0.85f));
        rt->DrawLine(MakePt(ox + 24.0f * u, oy + 12.0f * u), MakePt(ox + 24.0f * u, oy + 19.0f * u), br.Get(), ds, round_cap_style);
    } else if (kind == OCTANS_DEV_KEYBOARD) {
        static const float kx[5][2] = {
            { 11.0f, 14.0f }, { 17.5f, 20.5f }, { 24.0f, 27.0f }, { 30.5f, 33.5f }, { 36.8f, 37.2f }
        };
        float ks = 2.4f * u;
        if (ks < 1.2f) ks = 1.2f;
        br->SetColor(mul_a(detail_col, 0.82f));
        for (int i = 0; i < 5; ++i) {
            rt->DrawLine(MakePt(ox + kx[i][0] * u, oy + 20.0f * u), MakePt(ox + kx[i][1] * u, oy + 20.0f * u), br.Get(), ks, round_cap_style);
        }
        rt->DrawLine(MakePt(ox + 14.0f * u, oy + 28.0f * u), MakePt(ox + 34.0f * u, oy + 28.0f * u), br.Get(), ks, round_cap_style);
    } else if (kind == OCTANS_DEV_GAMEPAD) {
        float gs = 2.6f * u;
        if (gs < 1.2f) gs = 1.2f;
        br->SetColor(mul_a(detail_col, 0.88f));
        rt->DrawLine(MakePt(ox + 12.5f * u, oy + 20.5f * u), MakePt(ox + 19.1f * u, oy + 20.5f * u), br.Get(), gs, round_cap_style);
        rt->DrawLine(MakePt(ox + 15.8f * u, oy + 17.2f * u), MakePt(ox + 15.8f * u, oy + 23.8f * u), br.Get(), gs, round_cap_style);
        float dr = 1.9f * u;
        if (dr < 1.0f) dr = 1.0f;
        rt->FillEllipse(MakeEllipse(ox + 30.6f * u, oy + 18.8f * u, dr, dr), br.Get());
        rt->FillEllipse(MakeEllipse(ox + 34.6f * u, oy + 22.2f * u, dr, dr), br.Get());
    } else if (kind == OCTANS_DEV_EARBUDS) {
        /* Subtle acoustic vent ovals centered on each inward-facing earbud head */
        float vx = 1.65f * u;
        float vy = 2.1f * u;
        if (vx >= 0.85f) {
            br->SetColor(mul_a(detail_col, 0.84f));
            rt->FillEllipse(MakeEllipse(ox + 14.8f * u, oy + 14.6f * u, vx, vy), br.Get());
            rt->FillEllipse(MakeEllipse(ox + 33.2f * u, oy + 14.6f * u, vx, vy), br.Get());
        }
    } else if (kind == OCTANS_DEV_SPEAKER) {
        br->SetColor(mul_a(detail_col, 0.85f));
        rt->FillEllipse(MakeEllipse(ox + 24.0f * u, oy + 15.0f * u, 3.0f * u, 3.0f * u), br.Get());
        rt->DrawEllipse(MakeEllipse(ox + 24.0f * u, oy + 30.0f * u, 6.2f * u, 6.2f * u), br.Get(), ds * 0.85f);
    }
}

/* ============================================================================
 * Scheme 3 (Pulsar): 12-Segment Precision Radial Dial (-215 deg to +35 deg)
 * ============================================================================ */

void DrawPrecisionDialArc(
    ID2D1RenderTarget *rt,
    ID2D1Brush *active_brush,
    ID2D1Brush *track_brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float inner_r,
    float outer_r,
    float stroke_width,
    float fraction
) {
    if (!rt || !active_brush || !track_brush) return;
    static constexpr int kSegments = 12;
    float f = ClampF(fraction, 0.0f, 1.0f);
    int active = (f <= 0.005f) ? 0 : static_cast<int>(roundf(f * static_cast<float>(kSegments)));
    if (f > 0.005f && active < 1) active = 1;
    if (active > kSegments) active = kSegments;

    for (int i = 0; i < kSegments; ++i) {
        float deg = -215.0f + (static_cast<float>(i) * 250.0f) / static_cast<float>(kSegments - 1);
        float rad = deg * (kPi / 180.0f);
        float c = cosf(rad);
        float s = sinf(rad);
        rt->DrawLine(
            MakePt(cx + inner_r * c, cy + inner_r * s),
            MakePt(cx + outer_r * c, cy + outer_r * s),
            (i < active) ? active_brush : track_brush,
            stroke_width,
            round_cap_style
        );
    }
}

/* ============================================================================
 * Scan Radar Icon & Bespoke Octans Celestial Starlight Scan Animation
 * ============================================================================ */

void DrawScanRadarIcon(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float r
) {
    if (!factory || !rt || !brush || r <= 1.0f) return;
    float stroke = 1.45f;

    /* Center beacon dot */
    rt->FillEllipse(MakeEllipse(cx, cy, r * 0.24f, r * 0.24f), brush);

    /* Two symmetric wireless sonar waves (left & right arcs) */
    ComPtr<ID2D1PathGeometry> geo;
    if (SUCCEEDED(factory->CreatePathGeometry(geo.Put()))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(sink.Put()))) {
            AddArcSegmentToSink(sink.Get(), cx, cy, r * 0.62f, -45.0f, 90.0f);
            AddArcSegmentToSink(sink.Get(), cx, cy, r * 0.62f, 135.0f, 90.0f);
            AddArcSegmentToSink(sink.Get(), cx, cy, r * 1.02f, -52.0f, 104.0f);
            AddArcSegmentToSink(sink.Get(), cx, cy, r * 1.02f, 128.0f, 104.0f);
            sink->Close();
            rt->DrawGeometry(geo.Get(), brush, stroke, round_cap_style);
        }
    }
}

static void DrawFourPointSparkle(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float r
) {
    if (!factory || !rt || !brush || r < 0.6f) return;
    ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(factory->CreatePathGeometry(geo.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.Put()))) return;

    float inner = r * 0.24f;
    sink->BeginFigure(MakePt(cx, cy - r), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(MakePt(cx + inner, cy - inner));
    sink->AddLine(MakePt(cx + r, cy));
    sink->AddLine(MakePt(cx + inner, cy + inner));
    sink->AddLine(MakePt(cx, cy + r));
    sink->AddLine(MakePt(cx - inner, cy + inner));
    sink->AddLine(MakePt(cx - r, cy));
    sink->AddLine(MakePt(cx - inner, cy - inner));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(geo.Get(), brush);
}

void DrawCelestialScanAnim(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float base_radius,
    bool is_dark,
    float elapsed_sec,
    float alpha
) {
    if (!factory || !rt || alpha <= 0.01f) return;

    D2D1_COLOR_F star_col = is_dark ? HexColor(0x56C4F5, 1.0f) : HexColor(0x1B9AD6, 1.0f);
    D2D1_COLOR_F accent_col = is_dark ? HexColor(0x34D399, 1.0f) : HexColor(0x10B981, 1.0f);

    ComPtr<ID2D1SolidColorBrush> br;
    if (FAILED(rt->CreateSolidColorBrush(star_col, br.Put()))) return;

    /* 1. Soft breathing celestial aura halo behind the Octans Logo */
    float breath = 0.5f + 0.5f * sinf(elapsed_sec * 2.6f);
    D2D1_COLOR_F aura = star_col;
    aura.a = (is_dark ? 0.13f : 0.10f) * (0.65f + 0.35f * breath) * alpha;
    br->SetColor(aura);
    rt->FillEllipse(MakeEllipse(cx, cy, base_radius * (1.25f + 0.14f * breath), base_radius * (1.25f + 0.14f * breath)), br.Get());

    /* 2. Three staggered expanding polar starlight sonar pulses */
    for (int i = 0; i < 3; ++i) {
        float phase = fmodf(elapsed_sec * 0.46f + static_cast<float>(i) / 3.0f, 1.0f);
        float ease = EaseOutCubic(phase);
        float r = LerpF(base_radius * 0.92f, base_radius * 2.55f, ease);
        float wave_a = sinf(phase * kPi) * (1.0f - phase * 0.35f) * (is_dark ? 0.44f : 0.36f) * alpha;
        D2D1_COLOR_F wc = (i == 1) ? accent_col : star_col;
        wc.a = wave_a;
        br->SetColor(wc);
        rt->DrawEllipse(MakeEllipse(cx, cy, r, r), br.Get(), LerpF(1.6f, 0.75f, ease));
    }

    /* 3. Two delicate inclined orbital tracks + 4 drifting twinkling starlight sparkles */
    D2D1_MATRIX_3X2_F prev_xf = {};
    rt->GetTransform(&prev_xf);
    static const float tilts[2] = { -22.0f, 22.0f };
    for (int o = 0; o < 2; ++o) {
        D2D1_MATRIX_3X2_F rot = MatrixRotation(tilts[o], cx, cy);
        D2D1_MATRIX_3X2_F comb = {};
        comb._11 = rot._11 * prev_xf._11 + rot._12 * prev_xf._21;
        comb._12 = rot._11 * prev_xf._12 + rot._12 * prev_xf._22;
        comb._21 = rot._21 * prev_xf._11 + rot._22 * prev_xf._21;
        comb._22 = rot._21 * prev_xf._12 + rot._22 * prev_xf._22;
        comb._31 = rot._31 * prev_xf._11 + rot._32 * prev_xf._21 + prev_xf._31;
        comb._32 = rot._31 * prev_xf._12 + rot._32 * prev_xf._22 + prev_xf._32;
        rt->SetTransform(comb);

        float rx = base_radius * (o == 0 ? 1.98f : 1.72f);
        float ry = base_radius * (o == 0 ? 0.84f : 0.72f);
        D2D1_COLOR_F oc = star_col;
        oc.a = (is_dark ? 0.16f : 0.14f) * alpha;
        br->SetColor(oc);
        rt->DrawEllipse(MakeEllipse(cx, cy, rx, ry), br.Get(), 0.9f);

        for (int k = 0; k < 2; ++k) {
            float dir = (o == 0) ? 1.0f : -1.0f;
            float ang = elapsed_sec * dir * (1.25f + 0.35f * o) + static_cast<float>(k) * kPi + static_cast<float>(o) * 0.9f;
            float sx = cx + cosf(ang) * rx;
            float sy = cy + sinf(ang) * ry;
            float twinkle = 0.55f + 0.45f * sinf(elapsed_sec * 4.5f + static_cast<float>(o * 2 + k) * 1.7f);
            D2D1_COLOR_F sc = (k == 0) ? star_col : accent_col;
            sc.a = twinkle * 0.92f * alpha;
            br->SetColor(sc);
            DrawFourPointSparkle(factory, rt, br.Get(), sx, sy, 2.2f + 1.4f * twinkle);
        }
    }
    rt->SetTransform(prev_xf);
    (void)round_cap_style;

    /* 4. Centered gently breathing Octans Logo */
    float logo_r = base_radius * (1.0f + 0.032f * sinf(elapsed_sec * 2.6f));
    DrawOctansLogo(factory, rt, cx, cy, logo_r, is_dark, false, 1.0f, alpha);
}

void DrawScanCompleteAnim(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float radius,
    bool is_dark,
    float progress,
    float alpha
) {
    if (!factory || !rt || alpha <= 0.01f) return;
    float p = ClampF(progress, 0.0f, 1.0f);

    D2D1_COLOR_F accent = is_dark ? HexColor(0x34D399, 1.0f) : HexColor(0x10B981, 1.0f);
    ComPtr<ID2D1SolidColorBrush> br;
    if (FAILED(rt->CreateSolidColorBrush(accent, br.Put()))) return;

    /* 1. Soft expanding confirmation halo */
    float halo_p = EaseOutCubic(ClampF(p * 1.35f, 0.0f, 1.0f));
    float halo_r = LerpF(radius * 0.90f, radius * 1.95f, halo_p);
    D2D1_COLOR_F hc = accent;
    hc.a = (1.0f - halo_p) * (is_dark ? 0.42f : 0.34f) * alpha;
    br->SetColor(hc);
    rt->DrawEllipse(MakeEllipse(cx, cy, halo_r, halo_r), br.Get(), LerpF(2.2f, 0.8f, halo_p));

    /* 2. Soft filled badge disc with spring overshoot */
    float pop = EaseOutCubic(ClampF(p / 0.36f, 0.0f, 1.0f));
    float spring = pop + sinf(pop * kPi) * 0.08f;
    float disc_r = radius * spring;

    D2D1_COLOR_F bg = accent;
    bg.a = (is_dark ? 0.16f : 0.13f) * alpha;
    br->SetColor(bg);
    rt->FillEllipse(MakeEllipse(cx, cy, disc_r, disc_r), br.Get());

    /* 3. Crisp circular ring sweep */
    float ring_f = EaseOutCubic(ClampF(p / 0.45f, 0.0f, 1.0f));
    D2D1_COLOR_F rc = accent;
    rc.a = 0.94f * alpha;
    br->SetColor(rc);
    DrawBatteryRingArc(factory, rt, br.Get(), round_cap_style, cx, cy, disc_r, 2.2f, ring_f, false);

    /* 4. Animated two-stroke checkmark drawing smoothly left-to-right */
    float ck_t = ClampF((p - 0.18f) / 0.42f, 0.0f, 1.0f);
    if (ck_t > 0.01f) {
        D2D1_POINT_2F p0 = MakePt(cx - disc_r * 0.36f, cy + disc_r * 0.02f);
        D2D1_POINT_2F p1 = MakePt(cx - disc_r * 0.08f, cy + disc_r * 0.30f);
        D2D1_POINT_2F p2 = MakePt(cx + disc_r * 0.40f, cy - disc_r * 0.26f);

        float leg1 = EaseOutCubic(ClampF(ck_t / 0.42f, 0.0f, 1.0f));
        D2D1_POINT_2F m1 = MakePt(LerpF(p0.x, p1.x, leg1), LerpF(p0.y, p1.y, leg1));
        rt->DrawLine(p0, m1, br.Get(), 2.4f, round_cap_style);

        if (ck_t > 0.40f) {
            float leg2 = EaseOutCubic(ClampF((ck_t - 0.40f) / 0.60f, 0.0f, 1.0f));
            D2D1_POINT_2F m2 = MakePt(LerpF(p1.x, p2.x, leg2), LerpF(p1.y, p2.y, leg2));
            rt->DrawLine(p1, m2, br.Get(), 2.4f, round_cap_style);
        }
    }
}

void DrawTrashIcon(
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float s
) {
    if (!rt || !brush || s <= 1.0f) return;
    float stroke = 1.35f;
    /* Top lid line + handle */
    rt->DrawLine(MakePt(cx - s * 0.80f, cy - s * 0.48f), MakePt(cx + s * 0.80f, cy - s * 0.48f), brush, stroke, round_cap_style);
    rt->DrawLine(MakePt(cx - s * 0.30f, cy - s * 0.78f), MakePt(cx + s * 0.30f, cy - s * 0.78f), brush, stroke, round_cap_style);
    /* Can body */
    D2D1_RECT_F body = MakeRect(cx - s * 0.58f, cy - s * 0.46f, cx + s * 0.58f, cy + s * 0.82f);
    rt->DrawRoundedRectangle(MakeRoundedRect(body, s * 0.18f, s * 0.18f), brush, stroke);
    /* Two subtle vertical ribs */
    rt->DrawLine(MakePt(cx - s * 0.20f, cy - s * 0.12f), MakePt(cx - s * 0.20f, cy + s * 0.48f), brush, stroke * 0.85f, round_cap_style);
    rt->DrawLine(MakePt(cx + s * 0.20f, cy - s * 0.12f), MakePt(cx + s * 0.20f, cy + s * 0.48f), brush, stroke * 0.85f, round_cap_style);
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
    D2D1_RECT_F head = MakeRect(cx - s * 0.46f, cy - s * 0.80f, cx + s * 0.46f, cy - s * 0.46f);
    if (filled) {
        rt->FillRoundedRectangle(MakeRoundedRect(head, s * 0.16f, s * 0.16f), brush);
    } else {
        rt->DrawRoundedRectangle(MakeRoundedRect(head, s * 0.16f, s * 0.16f), brush, stroke);
    }

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
            AddArcSegmentToSink(sink.Get(), cx, cy, r, 35.0f, 280.0f);
            sink->Close();
            rt->DrawGeometry(geo.Get(), brush, stroke, round_cap_style);
        }
    }

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
