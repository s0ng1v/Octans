#ifndef OCTANS_HIDPP_H
#define OCTANS_HIDPP_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "../core/hid_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_LOGITECH_VID 0x046Du

/** Check if PID/product string corresponds to a Logitech wireless receiver. */
int octans_hidpp_is_receiver_pid(uint16_t pid, const wchar_t *product);

/** Build a 20-byte HID++ 2.0 long report (Report ID 0x11). */
void octans_hidpp_build_long(uint8_t slot,
                             uint8_t feat_idx,
                             uint8_t fn,
                             uint8_t swid,
                             const uint8_t *params,
                             size_t param_len,
                             uint8_t out20[20]);

/** Build a 7-byte HID++ 1.0 short register report (Report ID 0x10). */
void octans_hidpp_build_short(uint8_t slot, uint8_t sub_id, uint8_t reg, uint8_t out7[7]);

/** Parse HID++ 1.0 (0x10 slot 0x8F) or 2.0 (0x11 slot 0xFF) error frame. Returns 1 if error frame. */
int octans_hidpp_parse_error(const uint8_t *frame, size_t len, uint8_t *out_err);

/** Pure parsers for HID++ 2.0 battery feature reply params (bytes after [0..3] header). */
int octans_hidpp_parse_unified_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out);
int octans_hidpp_parse_battery_status(const uint8_t *params, size_t len, octans_proto_reading_t *out);
int octans_hidpp_parse_battery_voltage(const uint8_t *params, size_t len, octans_proto_reading_t *out);
int octans_hidpp_parse_adc_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out);

/** Pure parsers for HID++ 1.0 battery registers 0x0D and 0x07 (params after header). */
int octans_hidpp_parse_v10_reg0d(const uint8_t *params, size_t len, octans_proto_reading_t *out);
int octans_hidpp_parse_v10_reg07(const uint8_t *params, size_t len, octans_proto_reading_t *out);

/** Pure parser for G535/G533 long headset charge register reply (10 slot 05 reg mv_hi mv_lo state). */
int octans_hidpp_parse_headset_reg(const uint8_t *frame, size_t len, octans_proto_reading_t *out);

/** Map HID++ 0x0005 fn=2 device type byte to octans_device_kind_t. */
octans_device_kind_t octans_hidpp_map_kind(uint8_t dev_type);

/**
 * Poll all Logitech HID++ devices in `ifaces[0..iface_count-1]`.
 * Appends discovered device readings to `out[0..max_out-1]`; returns count appended.
 */
size_t octans_hidpp_poll_all(const octans_hid_info_t *ifaces,
                             size_t iface_count,
                             octans_proto_reading_t *out,
                             size_t max_out);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_HIDPP_H */
