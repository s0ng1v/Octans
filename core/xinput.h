#ifndef OCTANS_XINPUT_H
#define OCTANS_XINPUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_MAX_XINPUT_SLOTS 4

typedef struct octans_xinput_battery {
    uint8_t slot;
    uint8_t battery_type;
    int8_t battery_percent; /**< 0..100, or -1 if unsupported/disconnected */
    int is_wired;
} octans_xinput_battery_t;

/** Pure battery type/level mapper (unit-testable without hardware). */
int octans_xinput_map_battery(uint8_t type, uint8_t level, int *out_pct, int *out_wired);

/**
 * Dynamically load xinput1_4.dll / xinput9_1_0.dll and query slots 0..3.
 * Returns count of active wireless/wired controllers with battery info.
 */
size_t octans_xinput_enumerate(octans_xinput_battery_t *out, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_XINPUT_H */
