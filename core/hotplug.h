#ifndef OCTANS_HOTPLUG_H
#define OCTANS_HOTPLUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct octans_hotplug_snapshot {
    uint64_t hash;
    uint32_t iface_count;
    int initialized;
} octans_hotplug_snapshot_t;

/**
 * Compute zero-open HID device interface list signature via cfgmgr32.
 * Returns 1 if the present HID path set changed (or snapshot wasn't initialized), 0 otherwise.
 */
int octans_hotplug_changed(octans_hotplug_snapshot_t *prev);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_HOTPLUG_H */
