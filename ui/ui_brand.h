#ifndef OCTANS_UI_BRAND_H
#define OCTANS_UI_BRAND_H

#include "ui_internal.h"

namespace octans_ui {

/**
 * Draw the full Octans vector wordmark (tapered O-ring + polar star + "ctans")
 * fitted inside `bounds` (left-aligned, vertically centered).
 *
 * `progress` in [0.0f .. 1.0f] drives the synchronized writing entrance animation
 * where the O-ring, polar star, and all letter strokes draw simultaneously.
 */
void DrawOctansWordmark(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    const D2D1_RECT_F &bounds,
    bool is_dark,
    float progress,
    float alpha
);

/**
 * Draw the standalone Octans O-mark + polar star logo centered at `(cx, cy)`
 * with outer radius `radius` and completely transparent background (no backdrop).
 *
 * Set `compact_tray = true` for crisp taskbar-optimized proportions/contrast,
 * or `false` for panel empty-state display with brand gradients and polar star accent.
 */
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
);

} /* namespace octans_ui */

#endif /* OCTANS_UI_BRAND_H */
