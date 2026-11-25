#include "ui_brand.h"

namespace octans_ui {

struct RingSample {
    float ox, oy;
    float ix, iy;
};

/* Exact 65-sample tapered O-ring extracted from assets/octans-wordmark-light.svg
 * (center: 160.82, 114.69, outer radius ~84.80). */
static constexpr RingSample kWmRingSamples[65] = {
    {234.12f,73.46f,232.24f,74.48f}, {237.62f,80.01f,235.12f,81.06f}, {240.55f,86.86f,237.39f,87.84f},
    {242.68f,94.01f,239.22f,94.73f}, {244.18f,101.34f,240.43f,101.74f}, {245.01f,108.77f,240.99f,108.83f},
    {245.20f,116.27f,240.93f,115.93f}, {244.73f,123.76f,240.22f,122.97f}, {243.60f,131.20f,238.88f,129.92f},
    {241.81f,138.52f,236.93f,136.70f}, {239.37f,145.66f,234.40f,143.27f}, {236.30f,152.58f,231.29f,149.58f},
    {232.63f,159.21f,227.64f,155.57f}, {228.36f,165.49f,223.48f,161.20f}, {223.55f,171.38f,218.85f,166.44f},
    {218.21f,176.81f,213.80f,171.24f}, {212.40f,181.76f,208.34f,175.56f}, {206.15f,186.17f,202.55f,179.39f},
    {199.51f,190.00f,196.45f,182.69f}, {192.56f,193.21f,190.10f,185.46f}, {185.32f,195.79f,183.54f,187.64f},
    {177.87f,197.69f,176.82f,189.26f}, {170.28f,198.92f,170.00f,190.29f}, {162.61f,199.44f,163.12f,190.71f},
    {154.91f,199.26f,156.23f,190.56f}, {147.26f,198.38f,149.38f,189.81f}, {139.72f,196.80f,142.62f,188.47f},
    {132.36f,194.53f,136.00f,186.55f}, {125.24f,191.61f,129.57f,184.07f}, {118.41f,188.05f,123.38f,181.05f},
    {111.96f,183.88f,117.45f,177.49f}, {105.91f,179.16f,111.86f,173.43f}, {100.32f,173.90f,106.63f,168.90f},
    {95.24f,168.17f,101.81f,163.92f}, {90.70f,162.00f,97.43f,158.54f}, {86.75f,155.47f,93.53f,152.78f},
    {83.41f,148.62f,90.15f,146.69f}, {80.70f,141.50f,87.29f,140.32f}, {78.65f,134.19f,85.02f,133.71f},
    {77.25f,126.74f,83.34f,126.90f}, {76.53f,119.20f,82.25f,119.97f}, {76.49f,111.65f,81.80f,112.94f},
    {77.11f,104.13f,81.96f,105.90f}, {78.40f,96.71f,82.77f,98.88f}, {80.32f,89.45f,84.20f,91.94f},
    {82.88f,82.40f,86.24f,85.16f}, {86.04f,75.62f,88.90f,78.57f}, {89.78f,69.14f,92.14f,72.24f},
    {94.06f,63.02f,95.94f,66.22f}, {98.87f,57.32f,100.28f,60.55f}, {104.14f,52.06f,105.13f,55.31f},
    {109.84f,47.29f,110.42f,50.51f}, {115.94f,43.03f,116.14f,46.21f}, {122.39f,39.33f,122.22f,42.43f},
    {129.12f,36.22f,128.63f,39.22f}, {136.11f,33.71f,135.29f,36.60f}, {143.28f,31.81f,142.17f,34.59f},
    {150.60f,30.55f,149.21f,33.20f}, {157.99f,29.96f,156.34f,32.45f}, {165.41f,30.02f,163.51f,32.34f},
    {172.79f,30.72f,170.67f,32.87f}, {180.08f,32.09f,177.75f,34.03f}, {187.22f,34.11f,184.69f,35.82f},
    {194.09f,36.89f,191.50f,38.11f}, {200.67f,40.27f,198.10f,40.95f}
};

/* Exact 49-sample tray-optimized O-ring extracted from assets/octans-tray-white.svg
 * (24x24 viewBox, center: 12.0, 12.0). */
static constexpr RingSample kTrayRingSamples[49] = {
    {20.33f,8.80f,19.35f,9.18f}, {20.80f,9.72f,19.46f,10.06f}, {21.09f,10.72f,19.54f,10.93f},
    {21.21f,11.76f,19.58f,11.78f}, {21.22f,12.81f,19.52f,12.63f}, {21.09f,13.85f,19.37f,13.46f},
    {20.87f,14.88f,19.12f,14.27f}, {20.52f,15.88f,18.79f,15.04f}, {20.06f,16.84f,18.37f,15.76f},
    {19.50f,17.75f,17.87f,16.43f}, {18.82f,18.59f,17.31f,17.03f}, {18.05f,19.34f,16.69f,17.56f},
    {17.21f,20.02f,16.01f,18.03f}, {16.27f,20.58f,15.30f,18.41f}, {15.29f,21.04f,14.55f,18.72f},
    {14.25f,21.37f,13.77f,18.94f}, {13.18f,21.58f,12.98f,19.08f}, {12.08f,21.66f,12.17f,19.13f},
    {10.99f,21.62f,11.38f,19.10f}, {9.90f,21.44f,10.59f,18.98f}, {8.85f,21.14f,9.81f,18.78f},
    {7.84f,20.71f,9.05f,18.50f}, {6.89f,20.18f,8.34f,18.14f}, {6.01f,19.53f,7.65f,17.71f},
    {5.22f,18.78f,7.03f,17.20f}, {4.52f,17.95f,6.45f,16.63f}, {3.92f,17.05f,5.94f,16.00f},
    {3.44f,16.09f,5.50f,15.31f}, {3.06f,15.08f,5.14f,14.57f}, {2.81f,14.04f,4.85f,13.79f},
    {2.68f,12.98f,4.66f,12.99f}, {2.67f,11.92f,4.54f,12.15f}, {2.77f,10.87f,4.54f,11.32f},
    {3.00f,9.84f,4.63f,10.47f}, {3.33f,8.85f,4.81f,9.64f}, {3.78f,7.90f,5.09f,8.83f},
    {4.32f,7.01f,5.46f,8.07f}, {4.96f,6.19f,5.93f,7.34f}, {5.67f,5.44f,6.47f,6.67f},
    {6.46f,4.79f,7.09f,6.07f}, {7.32f,4.22f,7.78f,5.54f}, {8.24f,3.75f,8.53f,5.10f},
    {9.20f,3.38f,9.32f,4.75f}, {10.20f,3.13f,10.15f,4.49f}, {11.21f,2.98f,11.01f,4.32f},
    {12.23f,2.96f,11.88f,4.25f}, {13.26f,3.04f,12.74f,4.28f}, {14.25f,3.29f,13.61f,4.41f},
    {15.18f,3.72f,14.49f,4.52f}
};

static inline D2D1_MATRIX_3X2_F MultiplyMatrix(const D2D1_MATRIX_3X2_F &a, const D2D1_MATRIX_3X2_F &b) {
    D2D1_MATRIX_3X2_F r = {};
    r._11 = a._11 * b._11 + a._12 * b._21;
    r._12 = a._11 * b._12 + a._12 * b._22;
    r._21 = a._21 * b._11 + a._22 * b._21;
    r._22 = a._21 * b._12 + a._22 * b._22;
    r._31 = a._31 * b._11 + a._32 * b._21 + b._31;
    r._32 = a._31 * b._12 + a._32 * b._22 + b._32;
    return r;
}

static inline D2D1_MATRIX_3X2_F MatrixTransformRotateScale(
    float cx, float cy, float scale, float deg
) {
    float rad = deg * 0.017453292519943295f;
    float c = cosf(rad) * scale;
    float s = sinf(rad) * scale;
    D2D1_MATRIX_3X2_F m = {};
    m._11 = c;  m._12 = s;
    m._21 = -s; m._22 = c;
    m._31 = cx - cx * c + cy * s;
    m._32 = cy - cx * s - cy * c;
    return m;
}

static void CreateBrandLinearBrush(
    ID2D1RenderTarget *rt,
    float x1, float y1,
    float x2, float y2,
    const D2D1_GRADIENT_STOP *stops,
    UINT32 stop_count,
    float alpha,
    ComPtr<ID2D1LinearGradientBrush> &out
) {
    ComPtr<ID2D1GradientStopCollection> col;
    if (FAILED(rt->CreateGradientStopCollection(
            stops, stop_count, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, col.Put()))) {
        return;
    }
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lg = { MakePt(x1, y1), MakePt(x2, y2) };
    D2D1_BRUSH_PROPERTIES bp = { ClampF(alpha, 0.0f, 1.0f), MatrixIdentity() };
    rt->CreateLinearGradientBrush(&lg, &bp, col.Get(), out.Put());
}

/* Build a closed tapered ring path revealed clockwise from index 0 up to `progress` [0..1]. */
static bool BuildTaperedRingGeometry(
    ID2D1Factory *factory,
    const RingSample *samples,
    size_t count,
    float progress,
    ComPtr<ID2D1PathGeometry> &out
) {
    float p = ClampF(progress, 0.0f, 1.0f);
    if (p < 0.01f || !factory || count < 3) return false;
    if (FAILED(factory->CreatePathGeometry(out.Put()))) return false;

    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(out->Open(sink.Put()))) return false;

    float f_end = p * static_cast<float>(count - 1);
    size_t idx = static_cast<size_t>(floorf(f_end));
    float frac = f_end - static_cast<float>(idx);
    if (idx >= count - 1) {
        idx = count - 1;
        frac = 0.0f;
    }

    D2D1_POINT_2F tip_out = (idx < count - 1)
        ? MakePt(LerpF(samples[idx].ox, samples[idx + 1].ox, frac),
                 LerpF(samples[idx].oy, samples[idx + 1].oy, frac))
        : MakePt(samples[idx].ox, samples[idx].oy);
    D2D1_POINT_2F tip_in = (idx < count - 1)
        ? MakePt(LerpF(samples[idx].ix, samples[idx + 1].ix, frac),
                 LerpF(samples[idx].iy, samples[idx + 1].iy, frac))
        : MakePt(samples[idx].ix, samples[idx].iy);

    sink->BeginFigure(MakePt(samples[0].ox, samples[0].oy), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i <= idx; ++i) {
        sink->AddLine(MakePt(samples[i].ox, samples[i].oy));
    }
    if (frac > 0.01f) {
        sink->AddLine(tip_out);
    }

    /* Rounded leading endcap from outer tip to inner tip */
    float dx_tip = tip_out.x - tip_in.x;
    float dy_tip = tip_out.y - tip_in.y;
    float tip_r = sqrtf(dx_tip * dx_tip + dy_tip * dy_tip) * 0.5f;
    if (tip_r < 0.1f) tip_r = 0.1f;
    D2D1_ARC_SEGMENT cap_end = {
        tip_in,
        { tip_r, tip_r },
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        D2D1_ARC_SIZE_SMALL
    };
    sink->AddArc(&cap_end);

    for (size_t k = idx + 1; k > 0; --k) {
        sink->AddLine(MakePt(samples[k - 1].ix, samples[k - 1].iy));
    }

    /* Rounded tail endcap from inner[0] back to outer[0] */
    float dx0 = samples[0].ox - samples[0].ix;
    float dy0 = samples[0].oy - samples[0].iy;
    float r0 = sqrtf(dx0 * dx0 + dy0 * dy0) * 0.5f;
    if (r0 < 0.1f) r0 = 0.1f;
    D2D1_ARC_SEGMENT cap_start = {
        MakePt(samples[0].ox, samples[0].oy),
        { r0, r0 },
        0.0f,
        D2D1_SWEEP_DIRECTION_CLOCKWISE,
        D2D1_ARC_SIZE_SMALL
    };
    sink->AddArc(&cap_start);

    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close());
}

static bool BuildPolarStarGeometry(
    ID2D1Factory *factory,
    float sx, float sy,
    float rx, float ry,
    float kx, float ky,
    ComPtr<ID2D1PathGeometry> &out
) {
    if (!factory || FAILED(factory->CreatePathGeometry(out.Put()))) return false;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(out->Open(sink.Put()))) return false;

    sink->BeginFigure(MakePt(sx, sy - ry), D2D1_FIGURE_BEGIN_FILLED);
    D2D1_BEZIER_SEGMENT q1 = { MakePt(sx, sy - ky), MakePt(sx + kx, sy), MakePt(sx + rx, sy) };
    D2D1_BEZIER_SEGMENT q2 = { MakePt(sx + kx, sy), MakePt(sx, sy + ky), MakePt(sx, sy + ry) };
    D2D1_BEZIER_SEGMENT q3 = { MakePt(sx, sy + ky), MakePt(sx - kx, sy), MakePt(sx - rx, sy) };
    D2D1_BEZIER_SEGMENT q4 = { MakePt(sx - kx, sy), MakePt(sx, sy - ky), MakePt(sx, sy - ry) };
    sink->AddBezier(&q1);
    sink->AddBezier(&q2);
    sink->AddBezier(&q3);
    sink->AddBezier(&q4);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    return SUCCEEDED(sink->Close());
}

/* De Casteljau prefix split [0..u] of cubic Bezier (p0, p1, p2, p3). */
static void AddPartialCubic(
    ID2D1GeometrySink *sink,
    D2D1_POINT_2F p0,
    D2D1_POINT_2F p1,
    D2D1_POINT_2F p2,
    D2D1_POINT_2F p3,
    float u
) {
    u = ClampF(u, 0.0f, 1.0f);
    if (u >= 0.999f) {
        D2D1_BEZIER_SEGMENT seg = { p1, p2, p3 };
        sink->AddBezier(&seg);
        return;
    }
    D2D1_POINT_2F q0 = MakePt(LerpF(p0.x, p1.x, u), LerpF(p0.y, p1.y, u));
    D2D1_POINT_2F q1 = MakePt(LerpF(p1.x, p2.x, u), LerpF(p1.y, p2.y, u));
    D2D1_POINT_2F q2 = MakePt(LerpF(p2.x, p3.x, u), LerpF(p2.y, p3.y, u));
    D2D1_POINT_2F r0 = MakePt(LerpF(q0.x, q1.x, u), LerpF(q0.y, q1.y, u));
    D2D1_POINT_2F r1 = MakePt(LerpF(q1.x, q2.x, u), LerpF(q1.y, q2.y, u));
    D2D1_POINT_2F s0 = MakePt(LerpF(r0.x, r1.x, u), LerpF(r0.y, r1.y, u));
    D2D1_BEZIER_SEGMENT seg = { q0, r0, s0 };
    sink->AddBezier(&seg);
}

static void RenderPolarStar(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    float sx, float sy,
    float rx, float ry,
    float kx, float ky,
    D2D1_COLOR_F fill_col,
    D2D1_COLOR_F glow_col,
    bool has_glow,
    float progress,
    float alpha,
    const D2D1_MATRIX_3X2_F &base_xf
) {
    float p = ClampF(progress, 0.0f, 1.0f);
    if (p <= 0.01f) return;

    float star_a = alpha;
    float star_scale = 1.0f;
    float star_deg = 0.0f;
    if (p < 0.999f) {
        if (p < 0.60f) {
            float k = EaseOutCubic(p / 0.60f);
            star_a *= ClampF(p / 0.60f, 0.0f, 1.0f);
            star_scale = LerpF(0.15f, 1.12f, k);
            star_deg = LerpF(-35.0f, 3.0f, k);
        } else {
            float k = EaseOutCubic((p - 0.60f) / 0.40f);
            star_scale = LerpF(1.12f, 1.0f, k);
            star_deg = LerpF(3.0f, 0.0f, k);
        }
    }

    ComPtr<ID2D1PathGeometry> star_geom;
    if (!BuildPolarStarGeometry(factory, sx, sy, rx, ry, kx, ky, star_geom)) return;

    if (p < 0.999f) {
        D2D1_MATRIX_3X2_F anim_xf = MatrixTransformRotateScale(sx, sy, star_scale, star_deg);
        rt->SetTransform(MultiplyMatrix(anim_xf, base_xf));
    }

    if (has_glow) {
        ComPtr<ID2D1SolidColorBrush> gb;
        glow_col.a *= (star_a * 0.35f);
        rt->CreateSolidColorBrush(glow_col, gb.Put());
        if (gb) {
            ComPtr<ID2D1StrokeStyle> rjoin;
            D2D1_STROKE_STYLE_PROPERTIES sp = {};
            sp.lineJoin = D2D1_LINE_JOIN_ROUND;
            sp.miterLimit = 2.0f;
            factory->CreateStrokeStyle(sp, nullptr, 0, rjoin.Put());
            rt->DrawGeometry(star_geom.Get(), gb.Get(), ry * 0.08f, rjoin.Get());
        }
    }

    ComPtr<ID2D1SolidColorBrush> sb;
    fill_col.a *= star_a;
    rt->CreateSolidColorBrush(fill_col, sb.Put());
    if (sb) {
        rt->FillGeometry(star_geom.Get(), sb.Get());
    }

    if (p < 0.999f) {
        rt->SetTransform(base_xf);
    }
}

/* Build & stroke the 8 synchronized "ctans" wordmark paths in SVG coordinates. */
static void DrawWordmarkLetters(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float stroke_w,
    float progress
) {
    float p = ClampF(progress, 0.0f, 1.0f);
    if (p < 0.01f || !factory || !rt || !brush) return;

    ComPtr<ID2D1PathGeometry> geom;
    if (FAILED(factory->CreatePathGeometry(geom.Put()))) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geom->Open(sink.Put()))) return;

    constexpr float kPi = 3.14159265358979323846f;

    /* 1. 'c': center (310.4167, 154.0), r=40.90, CCW from -27.998 deg by -304.004 deg */
    {
        constexpr float cx = 310.4167f, cy = 154.0f, r = 40.90f;
        constexpr float a0 = -0.48866f;
        constexpr float sweep = -5.30586f;
        float a1 = a0 + sweep * p;
        D2D1_POINT_2F start = MakePt(346.53f, 134.80f);
        D2D1_POINT_2F end = (p >= 0.999f)
            ? MakePt(346.53f, 173.20f)
            : MakePt(cx + r * cosf(a1), cy + r * sinf(a1));
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        D2D1_ARC_SEGMENT arc = {
            end,
            { r, r },
            0.0f,
            D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
            (fabsf(sweep * p) > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
        };
        sink->AddArc(&arc);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 2. 't-stem': vertical (95.80) + CCW 90-deg fillet (25.13) + foot (12.00) = 132.93 */
    {
        constexpr float l0 = 95.80f, l1 = 25.1327f, l2 = 12.00f;
        constexpr float total = l0 + l1 + l2;
        float dist = p * total;
        sink->BeginFigure(MakePt(388.53f, 83.10f), D2D1_FIGURE_BEGIN_HOLLOW);
        if (dist <= l0) {
            sink->AddLine(MakePt(388.53f, 83.10f + dist));
        } else {
            sink->AddLine(MakePt(388.53f, 178.90f));
            float rem = dist - l0;
            if (rem <= l1) {
                float u = rem / l1;
                float ang = kPi - u * (kPi * 0.5f);
                D2D1_POINT_2F ep = MakePt(404.53f + 16.00f * cosf(ang), 178.90f + 16.00f * sinf(ang));
                D2D1_ARC_SEGMENT arc = {
                    ep, { 16.00f, 16.00f }, 0.0f,
                    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL
                };
                sink->AddArc(&arc);
            } else {
                D2D1_ARC_SEGMENT arc = {
                    MakePt(404.53f, 194.90f), { 16.00f, 16.00f }, 0.0f,
                    D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL
                };
                sink->AddArc(&arc);
                float tail = ClampF(rem - l1, 0.0f, l2);
                sink->AddLine(MakePt(404.53f + tail, 194.90f));
            }
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 3. 't-bar': (372.53, 115.10) -> (414.53, 115.10) */
    {
        sink->BeginFigure(MakePt(372.53f, 115.10f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(MakePt(LerpF(372.53f, 414.53f, p), 115.10f));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 4. 'a-bowl': full circle center (469.43, 154.00), r=40.90, split cleanly into 2 semicircles */
    {
        constexpr float cx = 469.43f, cy = 154.00f, r = 40.90f;
        sink->BeginFigure(MakePt(510.33f, 154.00f), D2D1_FIGURE_BEGIN_HOLLOW);
        if (p <= 0.50f) {
            float a1 = -2.0f * kPi * p;
            D2D1_ARC_SEGMENT arc = {
                MakePt(cx + r * cosf(a1), cy + r * sinf(a1)),
                { r, r }, 0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL
            };
            sink->AddArc(&arc);
        } else {
            D2D1_ARC_SEGMENT half = {
                MakePt(cx - r, cy),
                { r, r }, 0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL
            };
            sink->AddArc(&half);
            float a2 = -kPi - 2.0f * kPi * (p - 0.50f);
            D2D1_POINT_2F ep = (p >= 0.999f)
                ? MakePt(510.33f, 154.01f)
                : MakePt(cx + r * cosf(a2), cy + r * sinf(a2));
            D2D1_ARC_SEGMENT rest = {
                ep, { r, r }, 0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL
            };
            sink->AddArc(&rest);
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 5. 'a-stem': (510.33, 113.10) -> (510.33, 197.69) */
    {
        sink->BeginFigure(MakePt(510.33f, 113.10f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(MakePt(510.33f, LerpF(113.10f, 197.69f, p)));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 6. 'n-stem': (536.33, 110.93) -> (536.33, 197.69) */
    {
        sink->BeginFigure(MakePt(536.33f, 110.93f), D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(MakePt(536.33f, LerpF(110.93f, 197.69f, p)));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 7. 'n-arch': CW elliptical arch (~111.79) + right stem (48.60) = 160.39 */
    {
        constexpr float cx = 571.505f, cy = 149.09f, rx = 35.175f, ry = 35.99f;
        constexpr float l_arch = 111.79f, l_stem = 48.60f;
        float dist = p * (l_arch + l_stem);
        sink->BeginFigure(MakePt(536.33f, 149.09f), D2D1_FIGURE_BEGIN_HOLLOW);
        if (dist <= l_arch) {
            float u = dist / l_arch;
            float ang = kPi + u * kPi;
            D2D1_ARC_SEGMENT arc = {
                MakePt(cx + rx * cosf(ang), cy + ry * sinf(ang)),
                { rx, ry }, 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL
            };
            sink->AddArc(&arc);
        } else {
            D2D1_ARC_SEGMENT arc = {
                MakePt(606.68f, 149.09f),
                { rx, ry }, 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL
            };
            sink->AddArc(&arc);
            float stem_len = ClampF(dist - l_arch, 0.0f, l_stem);
            sink->AddLine(MakePt(606.68f, 149.09f + stem_len));
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    /* 8. 's': 6 cubics + 1 central spine line, normalized by segment weights */
    {
        struct SSeg {
            bool is_line;
            D2D1_POINT_2F p0, p1, p2, p3;
            float len;
        };
        static const SSeg kSSegs[7] = {
            { false, {684.56f,130.41f}, {681.02f,119.52f}, {673.28f,113.10f}, {660.28f,113.10f}, 31.0f },
            { false, {660.28f,113.10f}, {645.90f,113.10f}, {635.67f,120.90f}, {635.67f,133.01f}, 34.0f },
            { false, {635.67f,133.01f}, {635.67f,142.52f}, {641.81f,148.99f}, {654.25f,152.04f}, 28.0f },
            { true,  {654.25f,152.04f}, {667.58f,155.30f}, {0.0f,0.0f},       {0.0f,0.0f},       13.7f },
            { false, {667.58f,155.30f}, {679.31f,158.23f}, {685.28f,164.76f}, {685.28f,174.16f}, 27.5f },
            { false, {685.28f,174.16f}, {685.28f,186.33f}, {674.49f,194.90f}, {658.68f,194.90f}, 36.0f },
            { false, {658.68f,194.90f}, {644.52f,194.90f}, {635.34f,188.15f}, {632.68f,175.65f}, 34.5f }
        };
        constexpr float kTotalS = 204.7f;
        float rem = p * kTotalS;
        sink->BeginFigure(kSSegs[0].p0, D2D1_FIGURE_BEGIN_HOLLOW);
        for (int i = 0; i < 7 && rem > 0.01f; ++i) {
            const SSeg &sg = kSSegs[i];
            float u = (rem >= sg.len) ? 1.0f : (rem / sg.len);
            if (sg.is_line) {
                sink->AddLine(MakePt(LerpF(sg.p0.x, sg.p1.x, u), LerpF(sg.p0.y, sg.p1.y, u)));
            } else {
                AddPartialCubic(sink.Get(), sg.p0, sg.p1, sg.p2, sg.p3, u);
            }
            rem -= sg.len;
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    if (SUCCEEDED(sink->Close())) {
        ComPtr<ID2D1StrokeStyle> round_join;
        D2D1_STROKE_STYLE_PROPERTIES sp = {};
        sp.startCap = D2D1_CAP_STYLE_FLAT;
        sp.endCap = D2D1_CAP_STYLE_FLAT;
        sp.dashCap = D2D1_CAP_STYLE_FLAT;
        sp.lineJoin = D2D1_LINE_JOIN_ROUND;
        sp.miterLimit = 10.0f;
        factory->CreateStrokeStyle(sp, nullptr, 0, round_join.Put());
        rt->DrawGeometry(geom.Get(), brush, stroke_w, round_join.Get());
    }
}

void DrawOctansWordmark(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    const D2D1_RECT_F &bounds,
    bool is_dark,
    float progress,
    float alpha
) {
    if (!factory || !rt || alpha <= 0.01f) return;

    float bw = bounds.right - bounds.left;
    float bh = bounds.bottom - bounds.top;
    if (bw <= 4.0f || bh <= 4.0f) return;

    /* SVG ink bounds: x=[76.42..688.60], y=[28.62..200.80] */
    constexpr float kMinX = 76.42f;
    constexpr float kMinY = 28.62f;
    constexpr float kInkW = 612.18f;
    constexpr float kInkH = 172.18f;

    float sx = bw / kInkW;
    float sy = bh / kInkH;
    float scale = (sx < sy) ? sx : sy;
    if (scale <= 0.001f) return;

    float draw_h = kInkH * scale;
    float ox = bounds.left;
    float oy = bounds.top + (bh - draw_h) * 0.5f;

    D2D1_MATRIX_3X2_F prev_xf = {};
    rt->GetTransform(&prev_xf);

    D2D1_MATRIX_3X2_F svg_xf = {};
    svg_xf._11 = scale;
    svg_xf._22 = scale;
    svg_xf._31 = ox - kMinX * scale;
    svg_xf._32 = oy - kMinY * scale;
    D2D1_MATRIX_3X2_F combined_xf = MultiplyMatrix(svg_xf, prev_xf);
    rt->SetTransform(combined_xf);

    float p = ClampF(progress, 0.0f, 1.0f);
    float eased = EaseOutCubic(p);

    /* 1. Tapered O-Ring */
    ComPtr<ID2D1PathGeometry> ring_geom;
    if (BuildTaperedRingGeometry(factory, kWmRingSamples, 65, eased, ring_geom)) {
        if (is_dark) {
            ComPtr<ID2D1SolidColorBrush> glow_b;
            rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.06f * alpha * eased), glow_b.Put());
            if (glow_b) {
                rt->DrawGeometry(ring_geom.Get(), glow_b.Get(), 1.8f / scale);
            }
        }

        static const D2D1_GRADIENT_STOP kDarkRingStops[4] = {
            { 0.00f, HexColor(0x7B889D, 1.0f) },
            { 0.48f, HexColor(0xCBD4E2, 1.0f) },
            { 0.82f, HexColor(0xF2F6FC, 1.0f) },
            { 1.00f, HexColor(0xFFFFFF, 1.0f) }
        };
        static const D2D1_GRADIENT_STOP kLightRingStops[4] = {
            { 0.00f, HexColor(0x94A3B8, 1.0f) },
            { 0.46f, HexColor(0x3B4963, 1.0f) },
            { 0.82f, HexColor(0x141E33, 1.0f) },
            { 1.00f, HexColor(0x090F1E, 1.0f) }
        };
        ComPtr<ID2D1LinearGradientBrush> ring_brush;
        CreateBrandLinearBrush(
            rt,
            218.5f, 51.6f, 144.7f, 191.0f,
            is_dark ? kDarkRingStops : kLightRingStops,
            4,
            alpha,
            ring_brush
        );
        if (ring_brush) {
            rt->FillGeometry(ring_geom.Get(), ring_brush.Get());
        }
    }

    /* 2. Polar Star at upper-right notch (219.10, 55.12) */
    RenderPolarStar(
        factory,
        rt,
        219.10f, 55.12f,
        22.00f, 26.50f,
        3.08f, 3.71f,
        is_dark ? HexColor(0x84F8FF, 1.0f) : HexColor(0x0EA5E9, 1.0f),
        HexColor(0x5CEEFF, 0.30f),
        is_dark,
        p,
        alpha,
        combined_xf
    );

    /* 3. Wordmark letters "ctans" */
    static const D2D1_GRADIENT_STOP kDarkTextStops[2] = {
        { 0.00f, HexColor(0xE6ECF5, 1.0f) },
        { 1.00f, HexColor(0xBAC4D4, 1.0f) }
    };
    static const D2D1_GRADIENT_STOP kLightTextStops[2] = {
        { 0.00f, HexColor(0x1E293B, 1.0f) },
        { 1.00f, HexColor(0x0F172A, 1.0f) }
    };
    ComPtr<ID2D1LinearGradientBrush> text_brush;
    CreateBrandLinearBrush(
        rt,
        160.0f, 78.0f, 160.0f, 198.0f,
        is_dark ? kDarkTextStops : kLightTextStops,
        2,
        alpha,
        text_brush
    );
    if (text_brush) {
        /* Guarantee crisp readable stroke weight (>= 1.05 DIP) at compact header scale */
        float stroke_w = 1.12f / scale;
        if (stroke_w < 6.8f) stroke_w = 6.8f;
        if (stroke_w > 9.4f) stroke_w = 9.4f;
        DrawWordmarkLetters(factory, rt, text_brush.Get(), stroke_w, eased);
    }

    rt->SetTransform(prev_xf);
}

void DrawOctansLogo(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    float cx,
    float cy,
    float radius,
    bool is_dark,
    bool compact_tray,
    float progress,
    float alpha
) {
    if (!factory || !rt || radius <= 1.0f || alpha <= 0.01f) return;

    float p = ClampF(progress, 0.0f, 1.0f);
    float eased = EaseOutCubic(p);

    D2D1_MATRIX_3X2_F prev_xf = {};
    rt->GetTransform(&prev_xf);

    if (compact_tray) {
        /* Exact 24x24 tray-optimized O-mark (pure transparent background) */
        constexpr float kTrayRefX = 12.0f;
        constexpr float kTrayRefY = 12.0f;
        constexpr float kTrayRefR = 9.85f;
        float scale = radius / kTrayRefR;

        D2D1_MATRIX_3X2_F svg_xf = {};
        svg_xf._11 = scale;
        svg_xf._22 = scale;
        svg_xf._31 = cx - kTrayRefX * scale;
        svg_xf._32 = cy - kTrayRefY * scale;
        D2D1_MATRIX_3X2_F combined_xf = MultiplyMatrix(svg_xf, prev_xf);
        rt->SetTransform(combined_xf);

        D2D1_COLOR_F ink = is_dark ? HexColor(0xFFFFFF, 0.98f * alpha) : HexColor(0x141416, 0.96f * alpha);

        ComPtr<ID2D1PathGeometry> ring_geom;
        if (BuildTaperedRingGeometry(factory, kTrayRingSamples, 49, eased, ring_geom)) {
            ComPtr<ID2D1SolidColorBrush> rb;
            rt->CreateSolidColorBrush(ink, rb.Put());
            if (rb) {
                rt->FillGeometry(ring_geom.Get(), rb.Get());
            }
        }

        RenderPolarStar(
            factory,
            rt,
            17.94f, 6.06f,
            3.50f, 4.10f,
            0.56f, 0.66f,
            ink,
            ink,
            false,
            p,
            alpha,
            combined_xf
        );
    } else {
        /* Full-fidelity O-mark + polar star (transparent background) */
        constexpr float kWmRefX = 160.82f;
        constexpr float kWmRefY = 114.69f;
        constexpr float kWmRefR = 85.20f;
        float scale = radius / kWmRefR;

        D2D1_MATRIX_3X2_F svg_xf = {};
        svg_xf._11 = scale;
        svg_xf._22 = scale;
        svg_xf._31 = cx - kWmRefX * scale;
        svg_xf._32 = cy - kWmRefY * scale;
        D2D1_MATRIX_3X2_F combined_xf = MultiplyMatrix(svg_xf, prev_xf);
        rt->SetTransform(combined_xf);

        ComPtr<ID2D1PathGeometry> ring_geom;
        if (BuildTaperedRingGeometry(factory, kWmRingSamples, 65, eased, ring_geom)) {
            if (is_dark) {
                ComPtr<ID2D1SolidColorBrush> glow_b;
                rt->CreateSolidColorBrush(MakeColor(1.0f, 1.0f, 1.0f, 0.08f * alpha * eased), glow_b.Put());
                if (glow_b) {
                    rt->DrawGeometry(ring_geom.Get(), glow_b.Get(), 1.8f / scale);
                }
            }

            static const D2D1_GRADIENT_STOP kDarkRingStops[4] = {
                { 0.00f, HexColor(0x7B889D, 1.0f) },
                { 0.48f, HexColor(0xCBD4E2, 1.0f) },
                { 0.82f, HexColor(0xF2F6FC, 1.0f) },
                { 1.00f, HexColor(0xFFFFFF, 1.0f) }
            };
            static const D2D1_GRADIENT_STOP kLightRingStops[4] = {
                { 0.00f, HexColor(0x94A3B8, 1.0f) },
                { 0.46f, HexColor(0x3B4963, 1.0f) },
                { 0.82f, HexColor(0x141E33, 1.0f) },
                { 1.00f, HexColor(0x090F1E, 1.0f) }
            };
            ComPtr<ID2D1LinearGradientBrush> ring_brush;
            CreateBrandLinearBrush(
                rt,
                218.5f, 51.6f, 144.7f, 191.0f,
                is_dark ? kDarkRingStops : kLightRingStops,
                4,
                alpha,
                ring_brush
            );
            if (ring_brush) {
                rt->FillGeometry(ring_geom.Get(), ring_brush.Get());
            }
        }

        RenderPolarStar(
            factory,
            rt,
            219.10f, 55.12f,
            22.00f, 26.50f,
            3.08f, 3.71f,
            is_dark ? HexColor(0x84F8FF, 1.0f) : HexColor(0x0EA5E9, 1.0f),
            HexColor(0x5CEEFF, 0.32f),
            is_dark,
            p,
            alpha,
            combined_xf
        );
    }

    rt->SetTransform(prev_xf);
}

} /* namespace octans_ui */
