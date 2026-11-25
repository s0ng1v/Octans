#ifndef OCTANS_UI_RENAME_DLG_H
#define OCTANS_UI_RENAME_DLG_H

#include "ui_internal.h"

namespace octans_ui {

/* Show a minimal Win11-styled native modal prompt to edit a device alias.
 * Returns true if the user confirmed a new non-empty alias (copied to out_alias). */
bool ShowRenameAliasDialog(
    HWND owner,
    bool is_dark,
    const wchar_t *current_name,
    wchar_t *out_alias,
    size_t out_cap
);

} /* namespace octans_ui */

#endif /* OCTANS_UI_RENAME_DLG_H */
