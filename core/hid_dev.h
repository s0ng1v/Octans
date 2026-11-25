#ifndef OCTANS_HID_DEV_H
#define OCTANS_HID_DEV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dual-handle Windows HID session.
 * Maintains separate control/write handle and OVERLAPPED read handle to prevent
 * Windows HID driver crashes when reads and writes/IOCTLs overlap.
 */
typedef struct octans_hid_dev {
    void *h_write;
    void *h_read;
    void *h_event;
    void *preparsed;
    uint16_t input_len;
    uint16_t output_len;
    uint16_t feature_len;
    int can_write;
    int can_read;
    int last_error;
} octans_hid_dev_t;

/**
 * Open a HID interface.
 * - want_write: attempts GENERIC_READ|GENERIC_WRITE and falls back to 0 if denied.
 * - want_read: opens an independent FILE_FLAG_OVERLAPPED read handle for Input reports.
 * Returns 0 on success (control handle open), -1 on failure.
 */
int octans_hid_open(const wchar_t *path, int want_write, int want_read, octans_hid_dev_t *out);

/** Close all handles, cancel any pending I/O, and free preparsed data. */
void octans_hid_close(octans_hid_dev_t *dev);

/** Read Feature report (sets buf[0] = report_id). Returns bytes read or -1. */
int octans_hid_get_feature(octans_hid_dev_t *dev, uint8_t report_id, uint8_t *buf, size_t len);

/** Send Feature report (buf[0] = report_id). Returns 0 on success or -1. */
int octans_hid_set_feature(octans_hid_dev_t *dev, const uint8_t *buf, size_t len);

/** Send Output report via HidD_SetOutputReport (fallback WriteFile). Returns 0 or -1. */
int octans_hid_write_output(octans_hid_dev_t *dev, const uint8_t *buf, size_t len);

/** Send Output report via WriteFile first (fallback HidD_SetOutputReport). Returns 0 or -1. */
int octans_hid_write_output_intr(octans_hid_dev_t *dev, const uint8_t *buf, size_t len);

/** Read one Input report with OVERLAPPED timeout. Returns >0 bytes, 0 timeout, -1 error. */
int octans_hid_read_input(octans_hid_dev_t *dev, uint8_t *buf, size_t cap, uint32_t timeout_ms);

/** Drain up to max_packets (clamped <= 64) queued Input reports with 8 ms timeout each. */
void octans_hid_drain(octans_hid_dev_t *dev, int max_packets);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_HID_DEV_H */
