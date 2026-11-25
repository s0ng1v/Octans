#ifndef OCTANS_BROAD_SCAN_H
#define OCTANS_BROAD_SCAN_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "user_rules.h"
#include "../core/hid_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_BROAD_CACHE_CAP 64

typedef struct octans_broad_cache {
    uint64_t group_hashes[OCTANS_BROAD_CACHE_CAP];
    size_t count;
} octans_broad_cache_t;

/** Clear negative-probe cache (e.g. on USB hotplug or rule reload). */
void octans_broad_cache_clear(octans_broad_cache_t *c);

/** Map octans_device_kind_t to lowercase INI keyword ("mouse", "keyboard", "headset", etc.). */
const char *octans_kind_to_ini_str(octans_device_kind_t k);

/** Format a clean INI `[Section]` template string for a matched interface + protocol template. */
void octans_broad_format_rule_ini(const octans_hid_info_t *info,
                                  octans_device_kind_t kind,
                                  const char *proto_name,
                                  int is_auto,
                                  wchar_t *out,
                                  size_t out_cap);

/**
 * Broadly probe unhit vendor-HID interfaces (`UsagePage >= 0xFF00`) against matching
 * ODM protocol templates.
 *
 * - Skips groups already in `group_hit[]` or in `cache`.
 * - Only accepts readings with valid 1..100% battery.
 * - If `auto_save_ini_path` is non-NULL, saves matched devices to `devices.ini`.
 * - Caches misses in `cache` so steady-state polls never re-probe unresponsive hardware.
 * Returns number of newly produced readings written to `out_readings[0..max_readings-1]`.
 */
size_t octans_broad_scan_unknowns(const octans_hid_info_t *ifaces,
                                  size_t iface_count,
                                  int *group_hit,
                                  octans_broad_cache_t *cache,
                                  const wchar_t *auto_save_ini_path,
                                  int debug_log,
                                  octans_proto_reading_t *out_readings,
                                  size_t max_readings);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_BROAD_SCAN_H */
