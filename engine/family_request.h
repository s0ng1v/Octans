#ifndef OCTANS_FAMILY_REQUEST_H
#define OCTANS_FAMILY_REQUEST_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "prototable.h"
#include "../core/hid_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Pure frame builders & parsers (unit-testable without hardware) ---- */

void octans_proto_build_razer90(uint8_t tx_id, uint8_t cmd_id, uint8_t out91[91]);
int octans_proto_parse_razer90(const uint8_t *buf,
                               size_t len,
                               uint8_t expected_cmd,
                               uint8_t *out_status,
                               uint8_t *out_val);
void octans_proto_build_razer_pa(uint8_t cmd_id, uint8_t cmd_type, uint8_t out64[64]);
int octans_proto_parse_razer_pa(const uint8_t *buf, size_t len, uint8_t cmd_id, uint8_t *out_val);
int octans_proto_parse_razer_bsv2_2020(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

void octans_proto_build_sinowealth(uint8_t page, uint8_t cmd, uint8_t out65[65]);
int octans_proto_parse_sinowealth(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

void octans_proto_build_compx(uint8_t cmd, uint8_t out17[17]);
int octans_proto_parse_compx(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_compx_link(const uint8_t *buf, size_t len, int *out_online);

void octans_proto_build_atk_54l15(uint8_t out64[64]);
int octans_proto_parse_atk_54l15(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_atk_52840(uint8_t out17[17]);
int octans_proto_parse_atk_52840(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_atk_mouseinfo(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

void octans_proto_build_rog_1207(uint8_t report_id, uint8_t out_buf[65], size_t *out_len);
int octans_proto_parse_rog(const uint8_t *buf, size_t len, octans_proto_variant_t var, octans_proto_reading_t *out);

void octans_proto_build_corsair_bragi(uint8_t target, uint8_t prop, uint8_t out64[64]);
int octans_proto_parse_corsair_bragi(const uint8_t *buf, size_t len, uint16_t *out_raw);
int octans_proto_parse_corsair_void_elite(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_corsair_legacy_nxp(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

int octans_proto_parse_steelseries(const uint8_t *buf,
                                   size_t len,
                                   octans_proto_variant_t var,
                                   octans_proto_reading_t *out);

int octans_proto_parse_hyperx(const uint8_t *buf,
                              size_t len,
                              octans_proto_variant_t var,
                              octans_proto_reading_t *out);

void octans_proto_build_rapoo_vision(uint8_t out64[64]);
int octans_proto_parse_rapoo(const uint8_t *buf,
                             size_t len,
                             octans_proto_variant_t var,
                             octans_proto_reading_t *out);

void octans_proto_build_eweadn(uint8_t cmd, uint8_t out33[33]);
int octans_proto_parse_eweadn(const uint8_t *buf, size_t len, octans_proto_reading_t *out, char out_dev_id[8]);
int octans_proto_parse_ajazz_0x10(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_ajazz_0x05(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_ajazz_mkbms(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

void octans_proto_build_jieli_query(uint8_t seq, uint8_t target, uint8_t tlv_type, uint8_t out65[65]);
int octans_proto_parse_jieli_tlv(const uint8_t *buf, size_t len, uint8_t expected_seq, uint8_t *out_pct);

void octans_proto_build_via_battery(uint8_t out33[33]);
void octans_proto_build_semico_aa55(uint8_t cmd, int with_report_id, uint8_t *out_buf, size_t len);
int octans_proto_parse_semico_aa55(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_keyboard(const uint8_t *buf,
                                size_t len,
                                octans_proto_variant_t var,
                                uint32_t expected_req_id,
                                octans_proto_reading_t *out);

void octans_proto_build_mchose_hid4d(uint8_t report_id, uint16_t cmd, uint8_t *out_buf, size_t len);
int octans_proto_parse_mchose_hid4d(const uint8_t *buf, size_t len, uint16_t expected_vid, octans_proto_reading_t *out);
int octans_proto_parse_mchose_hid4d_batt(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_mchose_m7(uint8_t report_id, uint8_t cmd, uint8_t *out_buf, size_t len);
int octans_proto_parse_mchose_m7(const uint8_t *buf, size_t len, uint8_t expected_cmd, octans_proto_reading_t *out);
void octans_proto_build_mchose_g7max(uint8_t report_id, uint8_t out64[64]);
int octans_proto_parse_mchose_g7max(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_mchose_by_wireless(uint8_t out20[20]);
int octans_proto_parse_mchose_by(const uint8_t *buf, size_t len, int is_wired, octans_proto_reading_t *out);
void octans_proto_build_leobog_20b(uint8_t out20[20]);
int octans_proto_parse_leobog_20b(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_leobog_bytech(uint8_t report_id, uint8_t out64[64]);
int octans_proto_parse_leobog_bytech(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_darmoshark_nordic(int wireless, uint8_t out64[64]);
int octans_proto_parse_darmoshark_nordic(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_darmoshark_telink(uint8_t opcode, uint8_t out21[21]);
int octans_proto_parse_darmoshark_telink(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_mchose_audio_mc(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_dareu_tg(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_iqunix_texthex(const uint8_t *buf, size_t len, octans_proto_reading_t *out);

int octans_proto_parse_wob_rainy75v2(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_wob_zen65(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_rongyuan_f7(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_machenike_iton(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_incott(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_beken_push(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_bloody_8k(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_lenovo_voip(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_sony_inzone_buds(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
int octans_proto_parse_sony_pulse3d(const uint8_t *buf, size_t len, octans_proto_reading_t *out);
void octans_proto_build_epos_h3pro(int is_wired, uint8_t out107[107]);
int octans_proto_parse_epos_h3pro(const uint8_t *buf, size_t len, int is_wired, octans_proto_reading_t *out);

int octans_proto_parse_indep(const uint8_t *buf,
                             size_t len,
                             octans_proto_variant_t var,
                             octans_proto_reading_t *out);

/* ---- Live HID transport runner ---- */

/** Check whether a proto_entry_t matches an enumerated HID interface. */
int octans_proto_matches_iface(const proto_entry_t *e, const octans_hid_info_t *info);

/**
 * Execute one protocol table entry against an enumerated HID interface.
 * Returns 1 if a reading (valid, asleep, or offline) was produced, 0 otherwise.
 */
int octans_proto_query_iface(const proto_entry_t *e,
                             const octans_hid_info_t *info,
                             uint32_t listen_ms,
                             int debug_log,
                             octans_proto_reading_t *out);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_FAMILY_REQUEST_H */
