#ifndef OCTANS_HID_ENUM_H
#define OCTANS_HID_ENUM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_MAX_HID_IFACES 128

typedef struct octans_hid_info {
    wchar_t path[512];
    uint16_t vid;
    uint16_t pid;
    uint16_t version;
    uint16_t usage_page;
    uint16_t usage;
    uint16_t input_len;
    uint16_t output_len;
    uint16_t feature_len;
    uint16_t num_input_value_caps;
    uint16_t num_output_value_caps;
    uint16_t num_feature_value_caps;
    int interface_num;       /**< USB MI_xx interface index, or -1 if absent. */
    int is_bluetooth;        /**< Non-zero if Bluetooth HID interface. */
    int is_system_exclusive; /**< Non-zero if Windows reserves writes (must open access=0). */
    int open_error;          /**< Win32 GetLastError() if metadata open failed, else 0. */
    wchar_t manufacturer[64];
    wchar_t product[64];
    wchar_t serial[64];
    char group_key[64];      /**< Physical device grouping key (strips mi_XX/colXX). */
} octans_hid_info_t;

/** Enumerate currently present HID device interfaces safely (uses dwDesiredAccess=0). */
size_t octans_hid_enumerate(octans_hid_info_t *out, size_t max_count);

/** Return 1 if (usage_page, usage) is an OS-exclusive top-level collection. */
int octans_hid_is_exclusive_usage(uint16_t usage_page, uint16_t usage);

/** Convert wide string to UTF-8 safely (always NUL-terminated). */
void octans_wide_to_utf8(const wchar_t *src, char *dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_HID_ENUM_H */
