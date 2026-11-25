#ifndef OCTANS_UI_MENU_H
#define OCTANS_UI_MENU_H

#include "ui_internal.h"

namespace octans_ui {

/* Show themed Apple-style Direct2D popover menu anchored near (screen_x, screen_y).
 * - MENU_MODE_TRAY          : Full tray right-click menu
 * - MENU_MODE_PANEL_DEVICE  : Minimal 2-item card menu (Pin/Unpin + Rename)
 * - MENU_MODE_PANEL_GENERAL : General settings menu (no device-specific Pin/Rename) */
void ShowApplePopoverMenu(HWND owner, int screen_x, int screen_y, int device_index, MenuMode mode);

} /* namespace octans_ui */

#endif /* OCTANS_UI_MENU_H */
