#ifndef OCTANS_HID_PARSE_H
#define OCTANS_HID_PARSE_H

#include <stdint.h>
#include <stdio.h>

#include "hid_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Query standard HID battery / RF signal usages via HidP_GetValueCaps + HidD_GetFeature.
 * Sets *out_percent (0..100 or -1), *out_signal (0..100 or -1).
 * Returns 0 if a valid non-stub battery reading was obtained, -1 otherwise.
 */
int octans_hid_read_std_battery(octans_hid_dev_t *dev,
                                int *out_percent,
                                int *out_signal,
                                uint16_t *out_usage_page,
                                uint16_t *out_usage);

/** Dump Input/Output/Feature HidP_VALUE_CAPS to stream for `octans probe --dump`. */
void octans_hid_dump_caps(octans_hid_dev_t *dev, FILE *out);

/** Collect unique Feature Report IDs (always starting with 0x00) for safe read-only probing. */
size_t octans_hid_collect_feature_rids(octans_hid_dev_t *dev, uint8_t *out_rids, size_t max_rids);

/** Collect unique Output Report IDs declared in HidP_Output ValueCaps (falls back to 0x00 if none). */
size_t octans_hid_collect_output_rids(octans_hid_dev_t *dev, uint8_t *out_rids, size_t max_rids);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_HID_PARSE_H */
