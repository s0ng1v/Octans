#ifndef OCTANS_UI_API_H
#define OCTANS_UI_API_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file octans_ui_api.h
 * @brief Stable C ABI contract between Octans C11 backend (core/engine) and Route A UI.
 *
 * Threading & Ownership Rules:
 * - All strings inside octans_device_view_t are NUL-terminated fixed buffers owned by value.
 * - Backend -> UI calls (octans_ui_*) run on or safely marshal to the UI Win32 thread.
 * - UI -> Backend callbacks (octans_backend_ops_t) must return quickly without blocking
 *   the UI render/message thread on HID I/O; backend dispatches async work internally.
 */
#define OCTANS_UI_API_VERSION 1

/** Peripheral category used by UI to render device vector glyphs. */
typedef enum octans_device_kind {
    OCTANS_DEV_UNKNOWN = 0,
    OCTANS_DEV_MOUSE,
    OCTANS_DEV_KEYBOARD,
    OCTANS_DEV_HEADSET,
    OCTANS_DEV_EARBUDS,
    OCTANS_DEV_GAMEPAD,
    OCTANS_DEV_STYLUS,
    OCTANS_DEV_SPEAKER,
    OCTANS_DEV_OTHER
} octans_device_kind_t;

/** Active transport link for the merged device view. */
typedef enum octans_conn_kind {
    OCTANS_CONN_UNKNOWN = 0,
    OCTANS_CONN_DONGLE_24G,
    OCTANS_CONN_BLUETOOTH,
    OCTANS_CONN_WIRED
} octans_conn_kind_t;

/** Power & connectivity state of the peripheral. */
typedef enum octans_power_state {
    OCTANS_PWR_UNKNOWN = 0,
    OCTANS_PWR_DISCHARGING,
    OCTANS_PWR_CHARGING,
    OCTANS_PWR_FULL,
    OCTANS_PWR_SLEEPING,
    OCTANS_PWR_OFFLINE
} octans_power_state_t;

/** Immutable snapshot of one merged peripheral for UI display. */
typedef struct octans_device_view {
    char id[64];                       /**< Stable cross-channel ID (e.g. unitid/serial/vidpid). */
    wchar_t name[64];                  /**< Display name or user alias. */
    wchar_t brand[32];                 /**< Manufacturer / brand display label. */
    uint16_t vid;                      /**< USB/BT Vendor ID (0 if unavailable). */
    uint16_t pid;                      /**< USB/BT Product ID (0 if unavailable). */
    octans_device_kind_t kind;         /**< Peripheral form factor for glyph selection. */
    octans_conn_kind_t conn;           /**< Current active transport channel. */
    octans_power_state_t power_state;  /**< Charging / discharging / sleep / offline state. */
    int8_t battery_percent;            /**< 0..100, or -1 if unknown/unavailable. */
    uint16_t voltage_mv;               /**< Millivolts, or 0 if unavailable. */
    int8_t signal_percent;             /**< 0..100 RF signal quality, or -1 if unavailable. */
    uint8_t is_low_battery;            /**< 1 if battery_percent >= 0 && <= 20, else 0. */
    uint8_t is_estimated;              /**< 1 if derived from voltage curve or coarse step. */
    uint64_t updated_ms;               /**< GetTickCount64() timestamp of last valid sample. */
} octans_device_view_t;

/** Callbacks provided by the backend for UI-initiated actions. */
typedef struct octans_backend_ops {
    void (*request_refresh)(void *ctx);
    int (*set_pinned_device)(void *ctx, const char *device_id);
    int (*set_device_alias)(void *ctx, const char *device_id, const wchar_t *alias);
    int (*set_autostart)(void *ctx, int enabled);
    int (*get_autostart)(void *ctx);
    void (*request_quit)(void *ctx);
} octans_backend_ops_t;

/** Initialize UI subsystem and tray integration. Returns 0 on success, non-zero on error. */
int octans_ui_init(const octans_backend_ops_t *ops, void *backend_ctx);

/** Tear down UI windows, D2D/DComp resources, and tray icon cleanly. */
void octans_ui_shutdown(void);

/** Push latest merged device list snapshot to the UI. */
void octans_ui_on_devices_changed(const octans_device_view_t *devices, size_t count);

/** Show or toggle the floating battery panel anchored near (anchor_x, anchor_y) in screen px. */
void octans_ui_show_panel(int anchor_x, int anchor_y);

/** Hide the floating battery panel with exit transition. */
void octans_ui_hide_panel(void);

/** Notify UI when system light/dark theme changes (1 = dark, 0 = light). */
void octans_ui_on_theme_changed(int is_dark_mode);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_UI_API_H */
