#ifndef OCTANS_UI_TRAY_H
#define OCTANS_UI_TRAY_H

#include "ui_internal.h"

namespace octans_ui {

bool TrayInit(HWND host_hwnd);
void TrayShutdown();

/* Sync all tray icons (1 fallback icon if 0 pinned devices are active, or 1 dedicated icon per pinned device). */
void TraySyncAll(
    const octans_device_view_t *devices,
    size_t count,
    const char (*pinned_ids)[64],
    size_t pinned_count,
    TrayStyle style,
    float tray_scale
);

/* Re-add all active tray icons after Explorer restart ("TaskbarCreated"). */
void TrayHandleTaskbarCreated();

/* Query screen anchor point for a specific tray icon uID (0 = first active icon, falls back to cursor). */
POINT TrayGetAnchorPoint(UINT tray_uid = 0);

/* Resolve which device index in UiState::devices corresponds to a clicked tray icon uID (-1 if fallback/none). */
int TrayFindDeviceIndexForUid(UINT tray_uid);

} /* namespace octans_ui */

#endif /* OCTANS_UI_TRAY_H */
