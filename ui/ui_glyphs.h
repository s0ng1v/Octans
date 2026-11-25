#ifndef OCTANS_UI_GLYPHS_H
#define OCTANS_UI_GLYPHS_H

#include "ui_internal.h"

namespace octans_ui {

/* Draw a smooth circular progress arc centered at (cx, cy), starting at 12 o'clock (-90 deg).
 * Handles 0%, 100%, and optional bottom-notch gap cleanly without SVG/D2D 360-deg degeneracy. */
void DrawBatteryRingArc(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float radius,
    float stroke_width,
    float fraction,
    bool leave_bottom_notch = false
);

/* Draw bespoke vector peripheral glyph centered at (cx, cy) within radius `size` (half-extent).
 * Scales cleanly from 6px (tray icon) up to 24dp (panel card). */
void DrawDeviceGlyph(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    octans_device_kind_t kind,
    float cx, float cy,
    float half_size,
    bool compact_tray = false
);

/* Draw crisp Apple-style charging lightning bolt centered at (cx, cy). */
void DrawLightningBolt(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float half_height
);

/* Draw minimalist thumbtack/pin indicator for pinned tray device. */
void DrawPinIcon(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float half_size,
    bool filled
);

/* Draw minimalist circular refresh arrow (rotatable via rotation_deg). */
void DrawRefreshIcon(
    ID2D1Factory *factory,
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    ID2D1StrokeStyle *round_cap_style,
    float cx, float cy,
    float radius,
    float rotation_deg
);

/* Draw three horizontal ellipsis dots ("..."). */
void DrawMoreIcon(
    ID2D1RenderTarget *rt,
    ID2D1Brush *brush,
    float cx, float cy,
    float radius
);

} /* namespace octans_ui */

#endif /* OCTANS_UI_GLYPHS_H */
