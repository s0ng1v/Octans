#include "ui_theme.h"

namespace octans_ui {

bool DetectSystemDarkMode() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    LONG rc = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size
    );
    if (rc == ERROR_SUCCESS) {
        return value == 0;
    }
    return true;
}

ThemePalette GetThemePalette(bool is_dark) {
    ThemePalette p = {};
    p.is_dark = is_dark;

    if (is_dark) {
        p.panel_tint           = HexColor(0x1C1C20, 0.68f);
        p.panel_fallback_bg    = HexColor(0x1C1C20, 0.96f);
        p.panel_outer_border   = MakeColor(1.0f, 1.0f, 1.0f, 0.14f);
        p.panel_inner_specular = MakeColor(1.0f, 1.0f, 1.0f, 0.10f);

        p.card_bg              = MakeColor(1.0f, 1.0f, 1.0f, 0.075f);
        p.card_bg_hover        = MakeColor(1.0f, 1.0f, 1.0f, 0.130f);
        p.card_border          = MakeColor(1.0f, 1.0f, 1.0f, 0.090f);
        p.card_border_pinned   = HexColor(0x30D158, 0.56f);

        p.btn_hover_bg         = MakeColor(1.0f, 1.0f, 1.0f, 0.12f);
        p.btn_icon             = HexColor(0xEBEBF5, 0.74f);
        p.btn_icon_hover       = HexColor(0xFFFFFF, 0.98f);

        p.text_primary         = HexColor(0xF5F5F7, 0.98f);
        p.text_secondary       = HexColor(0xEBEBF5, 0.66f);
        p.text_muted           = HexColor(0xEBEBF5, 0.44f);

        p.ring_track           = MakeColor(1.0f, 1.0f, 1.0f, 0.14f);
        p.ring_normal          = HexColor(0x30D158, 1.0f);
        p.ring_mid             = HexColor(0xFF9F0A, 1.0f);
        p.ring_low             = HexColor(0xFF453A, 1.0f);
        p.ring_charging        = HexColor(0x30D158, 1.0f);
        p.ring_muted           = HexColor(0x8E8E93, 0.48f);
        p.kit_track            = MakeColor(1.0f, 1.0f, 1.0f, 0.15f);
        p.kit_detail_ink       = HexColor(0x1C1C20, 0.96f);
        p.glyph_active         = HexColor(0xF5F5F7, 0.94f);
        p.glyph_muted          = HexColor(0xAEAEB2, 0.52f);
        p.bolt_badge_bg        = HexColor(0x1C1C20, 0.94f);
        p.bolt_color           = HexColor(0x30D158, 1.0f);

        p.tooltip_bg           = MakeColor(1.0f, 1.0f, 1.0f, 0.06f);
        p.tooltip_border       = MakeColor(1.0f, 1.0f, 1.0f, 0.09f);
        p.scroll_thumb         = MakeColor(1.0f, 1.0f, 1.0f, 0.30f);
    } else {
        /* Crisp bright Apple frosted milk-white (eliminates muddy grey cast over Win11 Acrylic) */
        p.panel_tint           = HexColor(0xFFFFFF, 0.74f);
        p.panel_fallback_bg    = HexColor(0xFAFAFC, 0.97f);
        p.panel_outer_border   = MakeColor(0.0f, 0.0f, 0.0f, 0.08f);
        p.panel_inner_specular = MakeColor(1.0f, 1.0f, 1.0f, 0.88f);

        p.card_bg              = MakeColor(1.0f, 1.0f, 1.0f, 0.86f);
        p.card_bg_hover        = MakeColor(1.0f, 1.0f, 1.0f, 0.98f);
        p.card_border          = MakeColor(0.0f, 0.0f, 0.0f, 0.055f);
        p.card_border_pinned   = HexColor(0x34C759, 0.58f);

        p.btn_hover_bg         = MakeColor(0.0f, 0.0f, 0.0f, 0.06f);
        p.btn_icon             = HexColor(0x3C3C43, 0.72f);
        p.btn_icon_hover       = HexColor(0x1C1C1E, 0.96f);

        p.text_primary         = HexColor(0x1C1C1E, 0.98f);
        p.text_secondary       = HexColor(0x3C3C43, 0.68f);
        p.text_muted           = HexColor(0x3C3C43, 0.45f);

        p.ring_track           = HexColor(0x787880, 0.16f);
        p.ring_normal          = HexColor(0x34C759, 1.0f);
        p.ring_mid             = HexColor(0xFF9500, 1.0f);
        p.ring_low             = HexColor(0xFF3B30, 1.0f);
        p.ring_charging        = HexColor(0x34C759, 1.0f);
        p.ring_muted           = HexColor(0x8E8E93, 0.42f);
        p.kit_track            = HexColor(0x94A3B8, 0.24f);
        p.kit_detail_ink       = HexColor(0xFFFFFF, 0.96f);
        p.glyph_active         = HexColor(0x1C1C1E, 0.88f);
        p.glyph_muted          = HexColor(0x8E8E93, 0.65f);
        p.bolt_badge_bg        = HexColor(0xFFFFFF, 0.98f);
        p.bolt_color           = HexColor(0x34C759, 1.0f);

        p.tooltip_bg           = MakeColor(1.0f, 1.0f, 1.0f, 0.66f);
        p.tooltip_border       = MakeColor(0.0f, 0.0f, 0.0f, 0.055f);
        p.scroll_thumb         = MakeColor(0.0f, 0.0f, 0.0f, 0.24f);
    }

    return p;
}

bool IsDeviceMuted(const octans_device_view_t &dev) {
    return dev.power_state == OCTANS_PWR_SLEEPING ||
           dev.power_state == OCTANS_PWR_OFFLINE;
}

D2D1_COLOR_F GetDeviceAccentColor(const octans_device_view_t &dev, const ThemePalette &pal, TrayStyle style) {
    (void)style;
    /* Unified semantic thresholds across ALL 3 styles (Octans / Nebula / Pulsar):
     *   Charging / Full : Green (#34C759 / #30D158)
     *   <= 20%          : Red   (#FF3B30 / #FF453A)
     *   21% .. 40%      : Orange(#FF9500 / #FF9F0A)
     *   > 40%           : Green (#34C759 / #30D158) */
    if (IsDeviceMuted(dev) || dev.battery_percent < 0) {
        return pal.ring_muted;
    }
    if (dev.power_state == OCTANS_PWR_CHARGING || dev.power_state == OCTANS_PWR_FULL) {
        return pal.ring_charging;
    }
    if (dev.is_low_battery || dev.battery_percent <= 20) {
        return pal.ring_low;
    }
    if (dev.battery_percent <= 40) {
        return pal.ring_mid;
    }
    return pal.ring_normal;
}

D2D1_COLOR_F GetDeviceSoftAccentColor(const octans_device_view_t &dev, const ThemePalette &pal) {
    if (IsDeviceMuted(dev) || dev.battery_percent < 0) {
        return pal.ring_muted;
    }
    if (dev.power_state == OCTANS_PWR_CHARGING || dev.power_state == OCTANS_PWR_FULL) {
        return HexColor(0x86EFAC, 0.92f);
    }
    if (dev.is_low_battery || dev.battery_percent <= 20) return HexColor(0xFCA5A5, 0.92f);
    if (dev.battery_percent <= 40) return HexColor(0xFCD34D, 0.92f);
    return HexColor(0x86EFAC, 0.92f);
}

bool ApplyWin11Backdrop(HWND hwnd, bool is_dark) {
    if (!hwnd) return false;

    /* Extend glass across full client area so DWM Acrylic shows beneath DirectComposition. */
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    /* DWMWA_USE_IMMERSIVE_DARK_MODE = 20 */
    BOOL dark_val = is_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &dark_val, sizeof(dark_val));

    /* DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2 */
    DWORD corner_pref = 2;
    DwmSetWindowAttribute(hwnd, 33, &corner_pref, sizeof(corner_pref));

    /* DWMWA_SYSTEMBACKDROP_TYPE = 38:
     * 3 = DWMSBT_TRANSIENTWINDOW (Acrylic - closest to iOS frosted glass)
     * 4 = DWMSBT_TABBEDWINDOW (Mica Alt fallback) */
    DWORD backdrop = 3;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    if (FAILED(hr)) {
        backdrop = 4;
        hr = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    }
    return SUCCEEDED(hr);
}

void ClearWin11BackdropForExit(HWND hwnd) {
    if (!hwnd) return;
    /* DWMSBT_NONE = 1, DWMWCP_DONOTROUND = 1 -> pure transparent DirectComposition surface */
    DWORD none = 1;
    DwmSetWindowAttribute(hwnd, 38, &none, sizeof(none));
    DwmSetWindowAttribute(hwnd, 33, &none, sizeof(none));
}

} /* namespace octans_ui */
