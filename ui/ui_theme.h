#ifndef OCTANS_UI_THEME_H
#define OCTANS_UI_THEME_H

#include "ui_internal.h"

namespace octans_ui {

struct ThemePalette {
    bool is_dark;

    /* Panel surface & glass tint over Win11 Acrylic */
    D2D1_COLOR_F panel_tint;
    D2D1_COLOR_F panel_fallback_bg;
    D2D1_COLOR_F panel_outer_border;
    D2D1_COLOR_F panel_inner_specular;

    /* Card surface */
    D2D1_COLOR_F card_bg;
    D2D1_COLOR_F card_bg_hover;
    D2D1_COLOR_F card_border;
    D2D1_COLOR_F card_border_pinned;

    /* Header & button surface */
    D2D1_COLOR_F btn_hover_bg;
    D2D1_COLOR_F btn_icon;
    D2D1_COLOR_F btn_icon_hover;

    /* Text hierarchy */
    D2D1_COLOR_F text_primary;
    D2D1_COLOR_F text_secondary;
    D2D1_COLOR_F text_muted;

    /* Battery ring semantic tokens (Scheme 1 Octans classic + Kit 4-tier Nebula/Pulsar) */
    D2D1_COLOR_F ring_track;
    D2D1_COLOR_F ring_normal;       /* >20%: #34C759 / #30D158 */
    D2D1_COLOR_F ring_low;          /* <=20%: #FF3B30 / #FF453A */
    D2D1_COLOR_F ring_charging;     /* Green + lightning bolt */
    D2D1_COLOR_F ring_muted;        /* Sleeping / Offline */
    D2D1_COLOR_F kit_normal;        /* >45%: #10B981 / #34D399 */
    D2D1_COLOR_F kit_mid;           /* 21-45%: #F59E0B / #FBBF24 */
    D2D1_COLOR_F kit_low;           /* <=20%: #EF4444 / #F87171 */
    D2D1_COLOR_F kit_charging;      /* Charging: #0EA5E9 / #38BDF8 */
    D2D1_COLOR_F kit_track;         /* Liquid / dial inactive track */
    D2D1_COLOR_F kit_detail_ink;    /* Inner detail cutout color for liquid fill */
    D2D1_COLOR_F glyph_active;
    D2D1_COLOR_F glyph_muted;
    D2D1_COLOR_F bolt_badge_bg;
    D2D1_COLOR_F bolt_color;

    /* Tooltip pill & scrollbar */
    D2D1_COLOR_F tooltip_bg;
    D2D1_COLOR_F tooltip_border;
    D2D1_COLOR_F scroll_thumb;
};

/* Query Windows Registry AppsUseLightTheme (returns true if dark mode active). */
bool DetectSystemDarkMode();

/* Retrieve Apple-inspired light or dark palette. */
ThemePalette GetThemePalette(bool is_dark);

/* Semantic accent color for a given device view and display style. */
D2D1_COLOR_F GetDeviceAccentColor(const octans_device_view_t &dev, const ThemePalette &pal, TrayStyle style = TRAY_STYLE_OCTANS);
/* Lighter highlight tint for liquid meniscus glow in Nebula style. */
D2D1_COLOR_F GetDeviceSoftAccentColor(const octans_device_view_t &dev, const ThemePalette &pal);

/* True if device is sleeping or offline. */
bool IsDeviceMuted(const octans_device_view_t &dev);

/* Apply Win11 DWM rounded corners, immersive dark mode, and Transient Acrylic backdrop.
 * Returns true if native system backdrop was accepted by DWM. */
bool ApplyWin11Backdrop(HWND hwnd, bool is_dark);

/* Temporarily disable DWM backdrop & corner clip so DirectComposition can smoothly shrink & fade
 * the card with pure per-pixel transparency (zero lingering Acrylic rectangle). */
void ClearWin11BackdropForExit(HWND hwnd);

} /* namespace octans_ui */

#endif /* OCTANS_UI_THEME_H */
