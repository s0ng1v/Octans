#ifndef OCTANS_UI_PANEL_H
#define OCTANS_UI_PANEL_H

#include "ui_internal.h"

namespace octans_ui {

bool PanelInit();
void PanelShutdown();

/* Show or toggle floating panel anchored near (anchor_x, anchor_y) in physical screen px. */
void PanelShow(int anchor_x, int anchor_y, bool toggle_if_visible = false);

/* Dismiss floating panel with smooth exit transition. */
void PanelHide();

/* True if panel is currently visible (and not dismissing). */
bool PanelIsVisible();

/* Update device list and smoothly animate battery ring transitions. */
void PanelOnDevicesChanged(const octans_device_view_t *devices, size_t count);

/* Update pinned device visual indicator immediately. */
void PanelOnPinnedChanged();

/* Update display style (Octans / Nebula / Pulsar) immediately. */
void PanelOnStyleChanged();

/* Update scanning animation state immediately. */
void PanelOnScanStateChanged();

/* Update light/dark theme and DWM backdrop immediately. */
void PanelOnThemeChanged(bool is_dark);

/* Trigger header refresh icon spin animation. */
void PanelTriggerRefreshSpin();

HWND PanelGetHwnd();

} /* namespace octans_ui */

#endif /* OCTANS_UI_PANEL_H */
