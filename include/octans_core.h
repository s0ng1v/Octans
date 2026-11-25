#ifndef OCTANS_CORE_H
#define OCTANS_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "octans_ui_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of simultaneous merged devices managed by the backend. */
#define OCTANS_MAX_DEVICES 32

/** Standard backend error codes (0 = success, negative = failure). */
enum {
    OCTANS_OK = 0,
    OCTANS_ERR_INVALID_ARG = -1,
    OCTANS_ERR_NOMEM = -2,
    OCTANS_ERR_IO = -3,
    OCTANS_ERR_TIMEOUT = -4,
    OCTANS_ERR_NOT_FOUND = -5,
    OCTANS_ERR_BUFFER_TOO_SMALL = -6
};

typedef struct octans_engine octans_engine_t;

/** Probe CLI / diagnostic options. */
typedef struct octans_probe_opts {
    int dump_caps;         /**< Dump HidP value capabilities per interface. */
    int raw_feature_dump;  /**< Safely read Feature reports + brief passive read-only listen. */
    int include_bluetooth; /**< Include SetupDi Bluetooth battery scan. */
} octans_probe_opts_t;

/** Optional callback signature matching octans_ui_on_devices_changed(). */
typedef void (*octans_ui_notify_fn)(const octans_device_view_t *devices, size_t count);

/** Engine creation configuration. Pass NULL for defaults. */
typedef struct octans_config {
    uint32_t poll_interval_ms;    /**< Default poll interval (0 = 15000 ms). */
    uint32_t passive_listen_ms;   /**< Passive listen budget per silent device (0 = 250 ms). */
    int debug_log;                /**< Non-zero to log diagnostics to stderr. */
    octans_ui_notify_fn ui_notify; /**< Optional UI push hook (NULL for headless CLI). */
} octans_config_t;

/** Create and initialize an Octans backend engine instance. */
octans_engine_t *octans_engine_create(const octans_config_t *cfg);

/** Destroy engine instance and release internal resources. */
void octans_engine_destroy(octans_engine_t *eng);

/**
 * Run one discovery + telemetry poll cycle.
 * Uses cfgmgr32 zero-open path diffing unless force_rescan != 0.
 * Returns OCTANS_OK or negative error code.
 */
int octans_engine_poll(octans_engine_t *eng, int force_rescan);

/** Reload external user rules (`devices.ini`) and reset broad-scan negative cache. */
void octans_engine_reload_rules(octans_engine_t *eng);

/** Copy current merged device snapshot into caller buffer; returns total device count. */
size_t octans_engine_get_devices(const octans_engine_t *eng,
                                 octans_device_view_t *out,
                                 size_t max_count);

/** Serialize current device snapshot into UTF-8 JSON. Returns bytes written or negative error. */
int octans_engine_format_json(const octans_engine_t *eng, char *buf, size_t buf_cap);

/** Populate octans_backend_ops_t callbacks bound to this engine instance. */
void octans_engine_bind_ui(octans_engine_t *eng, octans_backend_ops_t *out_ops);

/** Check whether UI/user requested quit via octans_backend_ops_t.request_quit. */
int octans_engine_quit_requested(const octans_engine_t *eng);

/** Run full probe dump (raw_feature_dump != 0 enables read-only Feature/passive inspection). */
int octans_probe_dump(int raw_feature_dump, FILE *out);

/** Run probe with explicit options struct. */
int octans_probe_run(const octans_probe_opts_t *opts, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_CORE_H */
