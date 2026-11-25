#ifndef OCTANS_BLUETOOTH_H
#define OCTANS_BLUETOOTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_MAX_BT_DEVICES 32

typedef struct octans_bt_battery {
    wchar_t name[64];
    wchar_t instance_id[128];
    int8_t battery_percent; /**< 0..100, or -1 if absent/zero */
    int has_property;
    int is_handsfree;
} octans_bt_battery_t;

/**
 * Enumerate Windows Bluetooth devices and query DEVPKEY_Bluetooth_Battery
 * ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, pid=2). Zero-write read-only.
 */
size_t octans_bt_enumerate(octans_bt_battery_t *out, size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_BLUETOOTH_H */
