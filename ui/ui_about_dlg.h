#ifndef OCTANS_UI_ABOUT_DLG_H
#define OCTANS_UI_ABOUT_DLG_H

#include "ui_internal.h"

namespace octans_ui {

/* Show Apple-style "关于 Octans" modal sheet with version, author, and in-app SHA256 auto-updater.
 * Returns true if the silent installer was launched and Octans should quit immediately to upgrade. */
bool ShowAboutDialog(HWND owner, bool is_dark);

} /* namespace octans_ui */

#endif /* OCTANS_UI_ABOUT_DLG_H */
