#ifndef OCTANS_FRAME_H
#define OCTANS_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "octans_ui_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum octans_curve_id {
    CURVE_NONE = 0,
    CURVE_SOLAAR_13PT,
    CURVE_LOGI_G535,
    CURVE_AIROHA_BARRACUDA
} octans_curve_id_t;

typedef struct octans_proto_reading {
    int valid;
    int asleep;
    int offline;
    char stable_id[64];
    wchar_t name[64];
    wchar_t brand[32];
    uint16_t vid;
    uint16_t pid;
    octans_device_kind_t kind;
    octans_conn_kind_t conn;
    octans_power_state_t power_state;
    int8_t battery_percent;  /**< 0..100, or -1 if unknown */
    uint16_t voltage_mv;     /**< Millivolts, or 0 if unavailable */
    int8_t signal_percent;   /**< 0..100, or -1 if unavailable */
    uint8_t is_estimated;    /**< 1 if curve/step estimated */
    char source_tag[32];     /**< Diagnostic protocol tag */
} octans_proto_reading_t;

/** XOR bytes[start..end] inclusive (e.g. Razer90 bytes[2..87]). */
uint8_t octans_crc_xor(const uint8_t *data, size_t start, size_t end);

/** Compx checksum: (0x55 - sum(frame[0..15])) & 0xFF. */
uint8_t octans_crc_compx(const uint8_t *frame16);

/** Sum bytes[start..end] inclusive modulo 256 (EWEADN / AJAZZ). */
uint8_t octans_crc_sum_range(const uint8_t *data, size_t start, size_t end);

/** CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF), used by Jieli JL_RCSP TLV. */
uint16_t octans_crc16_ccitt_false(const uint8_t *data, size_t len);

/** Linear piecewise Li-ion mV -> 0..100% interpolation for given curve id. */
int octans_mv_to_percent(uint16_t mv, octans_curve_id_t curve);

/** Clamp integer percentage into [0, 100]. */
int octans_clamp_percent(int val);

/** Rounded unsigned division (num + den/2) / den. */
int octans_round_div(uint32_t num, uint32_t den);

/** Initialize an octans_proto_reading_t to clean unknown defaults. */
void octans_reading_init(octans_proto_reading_t *r);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_FRAME_H */
