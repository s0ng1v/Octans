#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "octans_core.h"
#include "octans_ui_api.h"
#include "../core/xinput.h"
#include "../engine/family_request.h"
#include "../engine/frame.h"
#include "../engine/hidpp.h"
#include "../engine/merge.h"
#include "../engine/user_rules.h"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(expr) do { \
    ++g_tests_run; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++g_tests_failed; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    ++g_tests_run; \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        fprintf(stderr, "FAIL %s:%d: %s expected %lld got %lld\n", \
                __FILE__, __LINE__, #actual, _e, _a); \
        ++g_tests_failed; \
    } \
} while (0)

static void test_checksums_and_curves(void) {
    /* 1. Razer90 XOR bytes[2..87] */
    uint8_t razer[91];
    octans_proto_build_razer90(0x1Fu, 0x80u, razer);
    ASSERT_EQ_INT(0x85, razer[89]); /* frame[88] after 1B report id */
    octans_proto_build_razer90(0x1Fu, 0x84u, razer);
    ASSERT_EQ_INT(0x81, razer[89]);

    /* 2. Compx checksum: sum(0..15) + csum == 0x55 mod 256 */
    uint8_t compx[17];
    octans_proto_build_compx(0x04u, compx);
    ASSERT_EQ_INT(0x49, compx[16]); /* 0x55 - (0x08 + 0x04) = 0x49 */

    /* 3. EWEADN sum[5..31] */
    uint8_t eweadn[33];
    octans_proto_build_eweadn(0x30u, eweadn);
    ASSERT_EQ_INT(0x00, eweadn[0]);
    ASSERT_EQ_INT(0x30, eweadn[1]);
    ASSERT_EQ_INT(0x00, eweadn[32]);

    /* 4. CRC16-CCITT-FALSE ("123456789" -> 0x29B1) */
    const uint8_t vec[9] = {'1','2','3','4','5','6','7','8','9'};
    ASSERT_EQ_INT(0x29B1, octans_crc16_ccitt_false(vec, 9));

    /* 5. Voltage curves */
    ASSERT_EQ_INT(100, octans_mv_to_percent(4200, CURVE_SOLAAR_13PT));
    ASSERT_EQ_INT(100, octans_mv_to_percent(4186, CURVE_SOLAAR_13PT));
    ASSERT_EQ_INT(50, octans_mv_to_percent(3811, CURVE_SOLAAR_13PT));
    ASSERT_EQ_INT(0, octans_mv_to_percent(3400, CURVE_SOLAAR_13PT));
    ASSERT_EQ_INT(35, octans_mv_to_percent(3765, CURVE_SOLAAR_13PT)); /* midpoint between 3751(30) and 3778(40) */
    ASSERT_EQ_INT(45, octans_mv_to_percent(3795, CURVE_SOLAAR_13PT)); /* midpoint between 3778(40) and 3811(50) */
    ASSERT_EQ_INT(50, octans_mv_to_percent(3817, CURVE_LOGI_G535));
    ASSERT_EQ_INT(35, octans_mv_to_percent(3780, CURVE_AIROHA_BARRACUDA));
}

static void test_hidpp(void) {
    uint8_t req20[20];
    octans_hidpp_build_long(0x01u, 0x05u, 0x01u, 0x0Au, NULL, 0, req20);
    ASSERT_EQ_INT(0x11, req20[0]);
    ASSERT_EQ_INT(0x01, req20[1]);
    ASSERT_EQ_INT(0x05, req20[2]);
    ASSERT_EQ_INT(0x1A, req20[3]);

    uint8_t err_frame[20] = {0x11, 0x01, 0xFF, 0x00, 0x1A, 0x09};
    uint8_t err_code = 0;
    ASSERT_EQ_INT(1, octans_hidpp_parse_error(err_frame, 20, &err_code));
    ASSERT_EQ_INT(0x09, err_code);

    uint8_t err10[7] = {0x10, 0x02, 0x8F, 0x00, 0x1A, 0x01};
    ASSERT_EQ_INT(1, octans_hidpp_parse_error(err10, 7, &err_code));
    ASSERT_EQ_INT(0x01, err_code);

    octans_proto_reading_t r;
    octans_reading_init(&r);
    const uint8_t p1004[4] = {78, 0x04, 0x00, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_unified_battery(p1004, 4, &r));
    ASSERT_EQ_INT(78, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);
    ASSERT_EQ_INT(0, r.is_estimated);

    /* Approx flag when pct == 0 or soc_supported == 0 (level 4->60%, 8->100%) */
    octans_reading_init(&r);
    const uint8_t p1004_approx[4] = {0, 0x02, 0x01, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_unified_battery(p1004_approx, 4, &r));
    ASSERT_EQ_INT(20, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);
    ASSERT_EQ_INT(1, r.is_estimated);

    octans_reading_init(&r);
    const uint8_t p1004_nosoc[4] = {42, 0x04, 0x00, 0x00}; /* soc_supported=0 -> must ignore byte0 and use level 4=60% */
    ASSERT_EQ_INT(1, octans_hidpp_parse_unified_battery_ex(p1004_nosoc, 4, 0, &r));
    ASSERT_EQ_INT(60, r.battery_percent);
    ASSERT_EQ_INT(1, r.is_estimated);

    /* 0x1000: normal + charging-0 rejected + full-0 -> 100% */
    octans_reading_init(&r);
    const uint8_t p1000[4] = {64, 50, 0x03, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_battery_status(p1000, 4, &r));
    ASSERT_EQ_INT(64, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    octans_reading_init(&r);
    const uint8_t p1000_chg0[4] = {0, 0, 0x01, 0x00};
    ASSERT_EQ_INT(0, octans_hidpp_parse_battery_status(p1000_chg0, 4, &r));

    octans_reading_init(&r);
    const uint8_t p1000_full0[4] = {0, 0, 0x03, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_battery_status(p1000_full0, 4, &r));
    ASSERT_EQ_INT(100, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    /* 0x1001 voltage (3989 mV = 80%, full 0x81, charging 0x80, undefined 0x83 guard) */
    octans_reading_init(&r);
    const uint8_t p1001[4] = {0x0F, 0x95, 0x81, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_battery_voltage(p1001, 4, &r));
    ASSERT_EQ_INT(3989, r.voltage_mv);
    ASSERT_EQ_INT(80, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    octans_reading_init(&r);
    const uint8_t p1001_chg[4] = {0x0F, 0x95, 0x80, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_battery_voltage(p1001_chg, 4, &r));
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    octans_reading_init(&r);
    const uint8_t p1001_83[4] = {0x0F, 0x95, 0x83, 0x00}; /* bit0..2=3 undefined -> NOT full/charging */
    ASSERT_EQ_INT(1, octans_hidpp_parse_battery_voltage(p1001_83, 4, &r));
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);

    /* 0x1F20 ADC: connected+charging (0x03), full (0x05), unlinked (0x00) */
    octans_reading_init(&r);
    const uint8_t p1f20[4] = {0x0E, 0xE3, 0x03, 0x00}; /* 3811 mV = 50%, connected+charging */
    ASSERT_EQ_INT(1, octans_hidpp_parse_adc_battery(p1f20, 4, &r));
    ASSERT_EQ_INT(50, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    octans_reading_init(&r);
    const uint8_t p1f20_full[4] = {0x10, 0x5E, 0x05, 0x00}; /* 4190 mV = 100%, connected+full */
    ASSERT_EQ_INT(1, octans_hidpp_parse_adc_battery(p1f20_full, 4, &r));
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    octans_reading_init(&r);
    const uint8_t p1f20_off[4] = {0x0E, 0xE3, 0x00, 0x00};
    ASSERT_EQ_INT(0, octans_hidpp_parse_adc_battery(p1f20_off, 4, &r));

    /* 0x0104 CentPP Audio SoC */
    octans_reading_init(&r);
    const uint8_t p0104[4] = {74, 0x80, 0x00, 0x00};
    ASSERT_EQ_INT(1, octans_hidpp_parse_centpp_soc(p0104, 4, &r));
    ASSERT_EQ_INT(74, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* modelId prefix classification */
    octans_device_kind_t mk = OCTANS_DEV_UNKNOWN;
    ASSERT_EQ_INT(1, octans_hidpp_classify_model_id("MOUSE.g502_hero", &mk));
    ASSERT_EQ_INT(OCTANS_DEV_MOUSE, mk);
    ASSERT_EQ_INT(1, octans_hidpp_classify_model_id("HEADSET.g733", &mk));
    ASSERT_EQ_INT(OCTANS_DEV_HEADSET, mk);
    ASSERT_EQ_INT(-1, octans_hidpp_classify_model_id("WHEEL.g923", &mk));
    ASSERT_EQ_INT(-1, octans_hidpp_classify_model_id("GAMEPAD.f710", &mk));

    /* HID++ 1.0 reg 0x0D, 0x07 (4-level & direct pct), and 0x0F (voltage) */
    octans_reading_init(&r);
    const uint8_t reg0d[3] = {55, 0x00, 0x50};
    ASSERT_EQ_INT(1, octans_hidpp_parse_v10_reg0d(reg0d, 3, &r));
    ASSERT_EQ_INT(55, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    octans_reading_init(&r);
    const uint8_t reg07[2] = {5, 0x22};
    ASSERT_EQ_INT(1, octans_hidpp_parse_v10_reg07(reg07, 2, &r));
    ASSERT_EQ_INT(50, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    octans_reading_init(&r);
    const uint8_t reg07_pct[4] = {68, 0x00, 0x00, 0x01};
    ASSERT_EQ_INT(1, octans_hidpp_parse_v10_reg07(reg07_pct, 4, &r));
    ASSERT_EQ_INT(68, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    octans_reading_init(&r);
    const uint8_t reg0f[2] = {0x0E, 0xE3}; /* 3811 mV = 50% */
    ASSERT_EQ_INT(1, octans_hidpp_parse_v10_reg0f(reg0f, 2, &r));
    ASSERT_EQ_INT(50, r.battery_percent);
}

static void test_razer_and_sinowealth(void) {
    /* DeathAdder V4 Pro example from docs/02: 02 1f 00 00 00 02 07 80 00 ab */
    uint8_t rz_resp[90] = {0x02, 0x1F, 0x00, 0x00, 0x00, 0x02, 0x07, 0x80, 0x00, 0xAB};
    uint8_t st = 0, val = 0;
    ASSERT_EQ_INT(1, octans_proto_parse_razer90(rz_resp, 90, 0x80u, &st, &val));
    ASSERT_EQ_INT(0x02, st);
    ASSERT_EQ_INT(67, octans_round_div((uint32_t)val * 100u, 255u));

    /* With leading Report ID 0x00 (91B) */
    uint8_t rz91[91] = {0x00, 0x04, 0x1F, 0x00, 0x00, 0x00, 0x02, 0x07, 0x80, 0x00, 0x00};
    ASSERT_EQ_INT(1, octans_proto_parse_razer90(rz91, 91, 0x80u, &st, &val));
    ASSERT_EQ_INT(0x04, st); /* idle/sleep */

    /* Synapse crosstalk mismatch (wrong cmd_id) */
    rz_resp[7] = 0x84u;
    ASSERT_EQ_INT(0, octans_proto_parse_razer90(rz_resp, 90, 0x80u, &st, &val));

    /* Sinowealth sliding match: a1 00 02 02 00 83 01 4b */
    octans_proto_reading_t r;
    octans_reading_init(&r);
    const uint8_t sino[12] = {0x00, 0xA1, 0x00, 0x02, 0x02, 0x00, 0x83, 0x01, 0x4B, 0x00};
    ASSERT_EQ_INT(1, octans_proto_parse_sinowealth(sino, sizeof(sino), &r));
    ASSERT_EQ_INT(75, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Sinowealth heartbeat */
    octans_reading_init(&r);
    const uint8_t hb[4] = {0x03, 0x00, 80, 0x00};
    ASSERT_EQ_INT(1, octans_proto_parse_sinowealth(hb, 4, &r));
    ASSERT_EQ_INT(80, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);
}

static void test_compx_and_atk(void) {
    /* Compx 17B battery reply */
    uint8_t cx[17] = {0x08, 0x04, 0,0,0,0, 88, 0x01, 0x0F, 0xD2, 0,0,0,0,0,0, 0};
    cx[16] = octans_crc_compx(cx);
    octans_proto_reading_t r;
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_compx(cx, 17, &r));
    ASSERT_EQ_INT(88, r.battery_percent);
    ASSERT_EQ_INT(4050, r.voltage_mv);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Corrupt checksum rejected */
    cx[16] ^= 0x01u;
    ASSERT_EQ_INT(0, octans_proto_parse_compx(cx, 17, &r));

    /* Compx link check */
    uint8_t link[17] = {0x08, 0x03, 0x00, 0,0,0, 0x01, 0};
    link[16] = octans_crc_compx(link);
    int online = 0;
    ASSERT_EQ_INT(1, octans_proto_parse_compx_link(link, 17, &online));
    ASSERT_EQ_INT(1, online);

    /* ATK Nordic 54L15 64B & nRF52840 17B */
    uint8_t atk64[64] = {0x08, 0x72, 0x02, 0x00, 0x01, 0x07, 0x01, 91};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_atk_54l15(atk64, 64, &r));
    ASSERT_EQ_INT(91, r.battery_percent);

    uint8_t atk17[17] = {0x08, 0x04, 0,0,0,0, 62, 1};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_atk_52840(atk17, 17, &r));
    ASSERT_EQ_INT(62, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);
}

static void test_rog_corsair_steelseries_hyperx(void) {
    octans_proto_reading_t r;

    /* ROG Harpe II Ace 03 12 07 ... byte5=53 (0x35), byte8..9=0x0E 0xFD (3837), byte10=0 */
    uint8_t rog[16] = {0x03, 0x12, 0x07, 0, 0, 53, 0, 0, 0x0E, 0xFD, 0x00};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_rog(rog, 16, VAR_ROG_HARPE2, &r));
    ASSERT_EQ_INT(53, r.battery_percent);
    ASSERT_EQ_INT(3837, r.voltage_mv);

    /* ROG OMNI keyboard 02 12 01 ... byte6=85, byte9=1 */
    uint8_t rog_kb[16] = {0x02, 0x12, 0x01, 0, 0, 0, 85, 0, 0, 1};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_rog(rog_kb, 16, VAR_ROG_OMNI_RX, &r));
    ASSERT_EQ_INT(85, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Corsair bragi: 01 01 02 00 e8 03 -> 1000 (100.0%) */
    const uint8_t bragi_ok[8] = {0x01, 0x01, 0x02, 0x00, 0xE8, 0x03};
    uint16_t raw = 0;
    ASSERT_EQ_INT(1, octans_proto_parse_corsair_bragi(bragi_ok, 8, &raw));
    ASSERT_EQ_INT(100, octans_round_div(raw, 10u));

    const uint8_t bragi_err[8] = {0x01, 0x01, 0x02, 0x05, 0x00, 0x00};
    ASSERT_EQ_INT(0, octans_proto_parse_corsair_bragi(bragi_err, 8, &raw));

    /* SteelSeries Rival 3 & Aerox */
    const uint8_t rival[4] = {0xAA, 72, 0x00, 0x01};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_steelseries(rival, 4, VAR_SS_RIVAL3, &r));
    ASSERT_EQ_INT(72, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    const uint8_t aerox[4] = {0xD2, (uint8_t)(0x80u | 15u), 0, 0}; /* charging + (15-1)*5 = 70% */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_steelseries(aerox, 4, VAR_SS_AEROX, &r));
    ASSERT_EQ_INT(70, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* HyperX Gen1, Gen2, Gen3 Haste 2, Cloud III S, Alpha 2 */
    const uint8_t hx1[8] = {0x06, 0xFF, 0xBB, 0x02, 0x00, 0x00, 0x0F, 65};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hx1, 8, VAR_HX_GEN1, &r));
    ASSERT_EQ_INT(65, r.battery_percent);

    const uint8_t hx2[5] = {0x66, 0x89, 0x0E, 0xD7, 0x30};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hx2, 5, VAR_HX_GEN2, &r));
    ASSERT_EQ_INT(0x30, r.battery_percent);

    const uint8_t hx3[8] = {0x51, 0x02, 82, 0x01, 0, 0, 0x58, 0x0F};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hx3, 8, VAR_HX_GEN3_HASTE2, &r));
    ASSERT_EQ_INT(82, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    const uint8_t hx_c3s[7] = {0x0C, 0, 0, 0, 0, 0x06, 77};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hx_c3s, 7, VAR_HX_CLOUD3S, &r));
    ASSERT_EQ_INT(77, r.battery_percent);

    const uint8_t hx_a2[7] = {0x51, 0x02, 44, 0, 0, 0, 0x80};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hx_a2, 7, VAR_HX_ALPHA2, &r));
    ASSERT_EQ_INT(44, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);
}

static void test_rapoo_ajazz_eweadn_jieli(void) {
    octans_proto_reading_t r;

    /* Rapoo report 7 & V700DIY 0xBD */
    const uint8_t rp7[8] = {0x00, 0, 0, 0, 0, 0, 1, 86};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_rapoo(rp7, 8, VAR_RAPOO_MOUSE_R7, &r));
    ASSERT_EQ_INT(86, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);

    const uint8_t rpbd[9] = {0xBD, 0x01, 0x00, 0x07, 0x00, 0x46, 0x00, 0x02, 0x54};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_rapoo(rpbd, 9, VAR_RAPOO_KB_BD, &r));
    ASSERT_EQ_INT(0x54, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* AJAZZ 0x10 with valid checksum[4..30] */
    uint8_t aj[32] = {0x10, 0x00, 0x01, 0x0B};
    aj[13] = 0x4B; /* 75% */
    aj[14] = 0x00; /* charging */
    aj[31] = octans_crc_sum_range(aj, 4u, 30u);
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_ajazz_0x10(aj, 32, &r));
    ASSERT_EQ_INT(75, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* EWEADN 0x30 ("M156", 0x46 = 70%) and sleeping (30 02) */
    const uint8_t ew_ok[15] = {0x30, 0x00, 0x01, 0x0B, 'M','1','5','6', 0,0,0,0, 0x00, 0x46, 0x01};
    char dev_id[8] = {0};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_eweadn(ew_ok, 15, &r, dev_id));
    ASSERT_EQ_INT(70, r.battery_percent);
    ASSERT_EQ_INT(0, strcmp(dev_id, "M156"));

    const uint8_t ew_sleep[4] = {0x30, 0x02, 0x00, 0x00};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_eweadn(ew_sleep, 4, &r, NULL));
    ASSERT_EQ_INT(1, r.asleep);

    /* Jieli JL_RCSP TLV 0x01 with valid CRC16-CCITT-FALSE */
    uint8_t jl[16] = {0x00, 0xFE, 0x07, 0x11, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x01, 0x01, 83};
    uint16_t jcrc = octans_crc16_ccitt_false(jl + 1, 12u);
    jl[13] = (uint8_t)(jcrc >> 8);
    jl[14] = (uint8_t)(jcrc & 0xFFu);
    uint8_t jpct = 0;
    ASSERT_EQ_INT(1, octans_proto_parse_jieli_tlv(jl, 15, 0x07u, &jpct));
    ASSERT_EQ_INT(83, jpct);
    ASSERT_EQ_INT(0, octans_proto_parse_jieli_tlv(jl, 15, 0x08u, &jpct));
}

static void test_keyboards_and_independent(void) {
    octans_proto_reading_t r;

    /* ATK Evision 0x1A */
    const uint8_t atk_ev[9] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x1A, 0x00, 69, 0x01};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_keyboard(atk_ev, 9, VAR_KB_ATK_EVISION, 0x44332211u, &r));
    ASSERT_EQ_INT(69, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Keychron B4 06 */
    uint8_t kc[24] = {0xB4, 0x06};
    kc[20] = 94;
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_keyboard(kc, 24, VAR_KB_KEYCHRON, 0u, &r));
    ASSERT_EQ_INT(94, r.battery_percent);

    /* VIA raw HID: 08 00 07 4C -> 76% */
    const uint8_t via[32] = {0x08, 0x00, 0x07, 0x4C};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_keyboard(via, 32, VAR_KB_VIA_RAW, 0u, &r));
    ASSERT_EQ_INT(76, r.battery_percent);

    /* VAXEE byte5 * 5 */
    const uint8_t vaxee[8] = {0x0E, 0xA5, 0x0B, 0x01, 0x01, 15};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(vaxee, 8, VAR_INDEP_VAXEE, &r));
    ASSERT_EQ_INT(75, r.battery_percent);

    /* NZXT Lift Elite */
    uint8_t nzxt[24] = {0x4E, 0x02, 0x97};
    nzxt[8] = 0x14; nzxt[9] = 0x0F; /* 3860 mV */
    nzxt[17] = 61; nzxt[18] = 0;
    nzxt[22] = 0x01;
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(nzxt, 24, VAR_INDEP_NZXT, &r));
    ASSERT_EQ_INT(61, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Alienware Pro */
    const uint8_t aw[4] = {0x0E, 0xA1, 48, 0x04};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(aw, 4, VAR_INDEP_ALIENWARE, &r));
    ASSERT_EQ_INT(48, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* MCHOSE M7 Ultra inverted payload: 53 52 31 00 02 05 07 00 09 64 00 64 */
    const uint8_t m7_plain[12] = {0x53, 0x52, 0x31, 0x00, 0x02, 0x05, 0x07, 0x00, 0x09, 0x64, 0x00, 0x64};
    uint8_t m7_wire[20];
    m7_wire[0] = 0x11u;
    m7_wire[1] = (uint8_t)(0x06u ^ 0xFFu);
    for (int i = 0; i < 12; ++i) m7_wire[2 + i] = (uint8_t)(m7_plain[i] ^ 0xFFu);
    for (int i = 14; i < 20; ++i) m7_wire[i] = 0xFFu;
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_m7(m7_wire, 20, 0x06u, &r));
    ASSERT_EQ_INT(100, r.battery_percent);

    /* MCHOSE G7: AA 30 a5 0b 0a 01 01 01 2e 00 */
    const uint8_t g7[12] = {0xAA, 0x30, 0xA5, 0x0B, 0x0A, 0x01, 0x01, 0x01, 0x2E, 0x00};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(g7, 12, VAR_INDEP_MCHOSE_G7, &r));
    ASSERT_EQ_INT(46, r.battery_percent);

    /* 8BitDo & Steam Controller 2025 */
    uint8_t bitdo[16] = {0x01};
    bitdo[14] = (uint8_t)(0x80u | 55u);
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(bitdo, 16, VAR_INDEP_8BITDO, &r));
    ASSERT_EQ_INT(55, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    const uint8_t steam[4] = {0x43, 0x02, 79, 0};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(steam, 4, VAR_INDEP_STEAM_2025, &r));
    ASSERT_EQ_INT(79, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Semico AA55 (1A2C:7FFF / 8FFF) */
    uint8_t sem_req[65];
    octans_proto_build_semico_aa55(0x0Eu, 1, sem_req, sizeof(sem_req));
    ASSERT_EQ_INT(0x00, sem_req[0]);
    ASSERT_EQ_INT(0xAA, sem_req[1]);
    ASSERT_EQ_INT(0x55, sem_req[2]);
    ASSERT_EQ_INT(0xCC, sem_req[3]);
    ASSERT_EQ_INT(0x33, sem_req[4]);
    ASSERT_EQ_INT(0x0E, sem_req[5]);

    const uint8_t sem_chg[8] = {0xAA, 0x55, 0xCC, 0x33, 0x0E, 0x61, 0x01, 0x00}; /* 97% charging */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_semico_aa55(sem_chg, 8, &r));
    ASSERT_EQ_INT(0x61, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    const uint8_t sem_dis[9] = {0x00, 0xAA, 0x55, 0xCC, 0x33, 0x0E, 0x50, 0x02, 0x00}; /* 80% discharging w/ rid */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_semico_aa55(sem_dis, 9, &r));
    ASSERT_EQ_INT(0x50, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);

    const uint8_t sem_full[8] = {0xAA, 0x55, 0xCC, 0x33, 0x0E, 0x64, 0x03, 0x00}; /* 100% full */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_semico_aa55(sem_full, 8, &r));
    ASSERT_EQ_INT(100, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    const uint8_t sem_off[8] = {0xAA, 0x55, 0xCC, 0x33, 0x0E, 0x00, 0x00, 0x00}; /* offline */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_semico_aa55(sem_off, 8, &r));
    ASSERT_EQ_INT(1, r.offline);

    /* Razer PA Headset (BlackShark V2 Pro 2023 / Barracuda Pro) & BSV2 2020 */
    uint8_t rz_pa_req[64];
    octans_proto_build_razer_pa(0x21u, 0x03u, rz_pa_req);
    ASSERT_EQ_INT('P', rz_pa_req[5]);
    ASSERT_EQ_INT('A', rz_pa_req[6]);
    uint8_t rz_pa_resp[20] = {0x02};
    rz_pa_resp[12] = 0x21u; rz_pa_resp[13] = 0x01u; rz_pa_resp[14] = 0x01u; rz_pa_resp[15] = 84u;
    uint8_t pa_val = 0;
    ASSERT_EQ_INT(1, octans_proto_parse_razer_pa(rz_pa_resp, 20, 0x21u, &pa_val));
    ASSERT_EQ_INT(84, pa_val);

    uint8_t bsv2[64] = {0xFF, 0x0F, 0x05, 0xFE, 0x12, 0x04, 0x1F, 0x08, 0x05, 0, 0, 0x09, 0x10, 0x80, 0x50};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_razer_bsv2_2020(bsv2, 64, &r));
    ASSERT_EQ_INT(80, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* HyperX Cloud Alpha Wireless */
    const uint8_t hxw_lvl[4] = {0x21, 0xBB, 0x0B, 82};
    const uint8_t hxw_chg[4] = {0x21, 0xBB, 0x0C, 1};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hxw_lvl, 4, VAR_HX_ALPHA_WIRELESS, &r));
    ASSERT_EQ_INT(82, r.battery_percent);
    ASSERT_EQ_INT(1, octans_proto_parse_hyperx(hxw_chg, 4, VAR_HX_ALPHA_WIRELESS, &r));
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* Astro A50 Gen5 & MCHOSE V9 Pro */
    const uint8_t astro[9] = {0x02, 0x0C, 0x06, 0x00, 0x06, 0x00, 73, 73, 1};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(astro, 9, VAR_INDEP_LOGI_ASTRO, &r));
    ASSERT_EQ_INT(73, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    const uint8_t mv9[4] = {0x55, 0x65, 60, 0x03};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(mv9, 4, VAR_INDEP_MCHOSE_V9, &r));
    ASSERT_EQ_INT(60, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* XInput level mapping */
    int xpct = -1, xwired = 0;
    ASSERT_EQ_INT(0, octans_xinput_map_battery(0x00, 0x03, &xpct, &xwired));
    ASSERT_EQ_INT(1, octans_xinput_map_battery(0x03, 0x02, &xpct, &xwired));
    ASSERT_EQ_INT(60, xpct);
    ASSERT_EQ_INT(1, octans_xinput_map_battery(0x02, 0x01, &xpct, &xwired));
    ASSERT_EQ_INT(25, xpct);
}

static void test_merge_json_and_abi(void) {
    octans_merge_state_t m;
    octans_merge_init(&m);

    /* Wired + 2.4G same unitid should merge with wired/charging taking precedence */
    octans_proto_reading_t batch[2];
    octans_reading_init(&batch[0]);
    batch[0].valid = 1;
    strcpy(batch[0].stable_id, "hidpp:11223344");
    wcscpy(batch[0].brand, L"Logitech");
    wcscpy(batch[0].name, L"GPW4");
    batch[0].vid = 0x046D;
    batch[0].pid = 0xC54D;
    batch[0].kind = OCTANS_DEV_MOUSE;
    batch[0].conn = OCTANS_CONN_DONGLE_24G;
    batch[0].power_state = OCTANS_PWR_DISCHARGING;
    batch[0].battery_percent = 18;

    octans_reading_init(&batch[1]);
    batch[1].valid = 1;
    strcpy(batch[1].stable_id, "hidpp:11223344");
    wcscpy(batch[1].brand, L"Logitech");
    wcscpy(batch[1].name, L"GPW4");
    batch[1].vid = 0x046D;
    batch[1].pid = 0x4099;
    batch[1].kind = OCTANS_DEV_MOUSE;
    batch[1].conn = OCTANS_CONN_WIRED;
    batch[1].power_state = OCTANS_PWR_CHARGING;
    batch[1].battery_percent = 19;

    octans_merge_apply(&m, batch, 2, 1000ULL);
    octans_device_view_t views[4];
    size_t n = octans_merge_export(&m, views, 4);
    ASSERT_EQ_INT(1, n);
    ASSERT_EQ_INT(19, views[0].battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, views[0].power_state);
    ASSERT_EQ_INT(1, views[0].is_low_battery);

    /* Sleep retention preserves last battery % within 300s */
    octans_proto_reading_t sleep_r;
    octans_reading_init(&sleep_r);
    sleep_r.asleep = 1;
    strcpy(sleep_r.stable_id, "hidpp:11223344");
    sleep_r.vid = 0x046D;
    sleep_r.pid = 0xC54D;
    octans_merge_apply(&m, &sleep_r, 1, 60000ULL);
    n = octans_merge_export(&m, views, 4);
    ASSERT_EQ_INT(1, n);
    ASSERT_EQ_INT(OCTANS_PWR_SLEEPING, views[0].power_state);
    ASSERT_EQ_INT(19, views[0].battery_percent);

    /* Pre-registered / persisted alias applies automatically when slot appears */
    octans_merge_state_t m_alias;
    octans_merge_init(&m_alias);
    ASSERT_EQ_INT(0, octans_merge_set_alias(&m_alias, "hidpp:aabbccdd", L"My GPW4"));
    octans_proto_reading_t r_al;
    octans_reading_init(&r_al);
    r_al.valid = 1;
    strcpy(r_al.stable_id, "hidpp:aabbccdd");
    wcscpy(r_al.name, L"PRO X 2 DEX");
    r_al.kind = OCTANS_DEV_MOUSE;
    r_al.conn = OCTANS_CONN_DONGLE_24G;
    r_al.power_state = OCTANS_PWR_DISCHARGING;
    r_al.battery_percent = 77;
    octans_merge_apply(&m_alias, &r_al, 1, 2000ULL);
    n = octans_merge_export(&m_alias, views, 4);
    ASSERT_EQ_INT(1, n);
    ASSERT_EQ_INT(0, wcscmp(views[0].name, L"My GPW4"));

    /* JSON formatting & ABI sanity */
    octans_engine_t *eng = octans_engine_create(NULL);
    ASSERT_TRUE(eng != NULL);
    char jbuf[1024];
    int jlen = octans_engine_format_json(eng, jbuf, sizeof(jbuf));
    ASSERT_TRUE(jlen > 0);
    ASSERT_TRUE(strstr(jbuf, "\"api_version\":1") != NULL);
    octans_backend_ops_t ops;
    memset(&ops, 0, sizeof(ops));
    octans_engine_bind_ui(eng, &ops);
    ASSERT_TRUE(ops.request_refresh != NULL);
    ASSERT_TRUE(ops.request_quit != NULL);
    octans_engine_destroy(eng);

    ASSERT_EQ_INT(1, OCTANS_UI_API_VERSION);
    ASSERT_TRUE(sizeof(octans_device_view_t) > 100);
}

static void test_user_rules_and_broad(void) {
    const wchar_t *ini =
        L"; Sample rules\r\n"
        L"[MCHOSE_Custom]\r\n"
        L"name = My MCHOSE Headset\r\n"
        L"kind = headset\r\n"
        L"vid = 291D\r\n"
        L"pid = *\r\n"
        L"usage_page = FF00\r\n"
        L"proto = mchose_v9\r\n\r\n"
        L"[TataCifang_KB]\r\n"
        L"name = Tata KB\r\n"
        L"kind = keyboard\r\n"
        L"vid = 1234\r\n"
        L"pid = 5678\r\n"
        L"usage_page = FF01\r\n"
        L"usage = 0002\r\n"
        L"tx = output\r\n"
        L"req_hex = 08 04\r\n"
        L"req_len = 17\r\n"
        L"checksum = compx_55\r\n"
        L"resp_head = 08 04\r\n"
        L"batt_offset = 6\r\n"
        L"batt_mode = percent\r\n"
        L"chg_offset = 7\r\n"
        L"chg_mask = 01\r\n\r\n"
        L"[MvStepRule]\r\n"
        L"vid = ABCD\r\n"
        L"pid = 0001\r\n"
        L"tx = feature\r\n"
        L"req_hex = AA 55\r\n"
        L"req_len = 8\r\n"
        L"checksum = xor_all\r\n"
        L"batt_offset = 2\r\n"
        L"batt_mode = mv_be\r\n"
        L"chg_offset = 4\r\n"
        L"chg_eq = 02\r\n";

    octans_user_rule_t rules[8];
    size_t rc = octans_user_rules_parse_text(ini, rules, 8);
    ASSERT_EQ_INT(3, rc);

    /* Template rule + wildcard PID match */
    ASSERT_EQ_INT(VAR_INDEP_MCHOSE_V9, rules[0].template_variant);
    ASSERT_EQ_INT(0x291D, rules[0].vid);
    ASSERT_EQ_INT(0xFFFF, rules[0].pid);
    octans_hid_info_t hinf;
    memset(&hinf, 0, sizeof(hinf));
    hinf.vid = 0x291Du;
    hinf.pid = 0x9999u;
    hinf.usage_page = 0xFF00u;
    ASSERT_EQ_INT(1, octans_user_rule_matches_iface(&rules[0], &hinf));

    /* Custom byte rule + compx_55 checksum builder + reply parser */
    uint8_t req[32];
    size_t rlen = 0;
    ASSERT_EQ_INT(1, octans_user_rule_build_req(&rules[1], 17u, req, sizeof(req), &rlen));
    ASSERT_EQ_INT(17, rlen);
    ASSERT_EQ_INT(0x08, req[0]);
    ASSERT_EQ_INT(0x04, req[1]);
    ASSERT_EQ_INT(0x49, req[16]);

    uint8_t rep1[17] = {0x08, 0x04, 0, 0, 0, 0, 82, 0x01};
    octans_proto_reading_t out;
    octans_reading_init(&out);
    ASSERT_EQ_INT(1, octans_user_rule_parse_reply(&rules[1], rep1, sizeof(rep1), &out));
    ASSERT_EQ_INT(82, out.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, out.power_state);

    /* Wrong resp_head rejected */
    uint8_t bad_rep[17] = {0x09, 0x04, 0, 0, 0, 0, 82, 0x01};
    octans_reading_init(&out);
    ASSERT_EQ_INT(0, octans_user_rule_parse_reply(&rules[1], bad_rep, sizeof(bad_rep), &out));

    /* xor_all checksum + mv_be + chg_eq */
    ASSERT_EQ_INT(1, octans_user_rule_build_req(&rules[2], 8u, req, sizeof(req), &rlen));
    ASSERT_EQ_INT(8, rlen);
    ASSERT_EQ_INT(0xAA ^ 0x55, req[7]);

    uint8_t mv_rep[8] = {0x00, 0x00, 0x0E, 0xE3, 0x02}; /* 3811 mV = 50%, chg_eq=2 */
    octans_reading_init(&out);
    ASSERT_EQ_INT(1, octans_user_rule_parse_reply(&rules[2], mv_rep, sizeof(mv_rep), &out));
    ASSERT_EQ_INT(50, out.battery_percent);
    ASSERT_EQ_INT(3811, out.voltage_mv);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, out.power_state);
    ASSERT_EQ_INT(1, out.is_estimated);

    /* step10 & step5 checks */
    const wchar_t *ini2 =
        L"[S10]\r\nvid=1111\r\npid=2222\r\nbatt_offset=1\r\nbatt_mode=step10\r\n"
        L"[S5]\r\nvid=1111\r\npid=3333\r\nbatt_offset=1\r\nbatt_mode=step5\r\n";
    octans_user_rule_t r2[2];
    ASSERT_EQ_INT(2, octans_user_rules_parse_text(ini2, r2, 2));
    uint8_t sr[4] = {0x01, 7, 0, 0};
    octans_reading_init(&out);
    ASSERT_EQ_INT(1, octans_user_rule_parse_reply(&r2[0], sr, 4, &out));
    ASSERT_EQ_INT(70, out.battery_percent);
    octans_reading_init(&out);
    ASSERT_EQ_INT(1, octans_user_rule_parse_reply(&r2[1], sr, 4, &out));
    ASSERT_EQ_INT(35, out.battery_percent);
}

static void test_mchose_v2_suite(void) {
    octans_proto_reading_t r;

    /* F1 / F4 HID4D: build 0x0900 (XOR=0x08) & 0x0904 (XOR=0x0C) */
    uint8_t h4d[64];
    octans_proto_build_mchose_hid4d(0x4Du, 0x0900u, h4d, sizeof(h4d));
    ASSERT_EQ_INT(0x4D, h4d[0]);
    ASSERT_EQ_INT(0x01, h4d[1]);
    ASSERT_EQ_INT(0x01, h4d[2]);
    ASSERT_EQ_INT(0x00, h4d[4]);
    ASSERT_EQ_INT(0x09, h4d[5]);
    ASSERT_EQ_INT(0x08, h4d[8]);

    octans_proto_build_mchose_hid4d(0x4Du, 0x0904u, h4d, sizeof(h4d));
    ASSERT_EQ_INT(0x04, h4d[4]);
    ASSERT_EQ_INT(0x0C, h4d[8]);

    /* F1 0x0900 response auto-aligned by VID LE (0x37 0x38) with RID 0x4D prefix */
    uint8_t f1_rep[64] = {0};
    f1_rep[0] = 0x4Du; f1_rep[1] = 0x01u; f1_rep[4] = 0x00u; f1_rep[5] = 0x09u;
    f1_rep[8] = 0x37u; f1_rep[9] = 0x38u; /* VID 0x3837 LE at p=8 */
    f1_rep[8 + 11] = 1u;  /* chargeStatus = charging */
    f1_rep[8 + 12] = 88u; /* batteryLevel = 88% */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_hid4d(f1_rep, sizeof(f1_rep), 0x3837u, &r));
    ASSERT_EQ_INT(88, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* F4 0x0904 response (CZ keyboard dedicated battery cmd) */
    uint8_t f4_rep[64] = {0};
    f4_rep[0] = 0x4Du; f4_rep[4] = 0x04u; f4_rep[5] = 0x09u;
    f4_rep[8] = 2u;  /* full */
    f4_rep[9] = 100u;
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_hid4d_batt(f4_rep, sizeof(f4_rep), &r));
    ASSERT_EQ_INT(100, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_FULL, r.power_state);

    /* F2 MC Feature (0x11 inverted): both with & without RID 0x11 */
    uint8_t f2_rep[21];
    memset(f2_rep, 0xFF, sizeof(f2_rep));
    f2_rep[0] = 0x11u;
    f2_rep[1] = (uint8_t)(0x06u ^ 0xFFu);
    f2_rep[2 + 9] = (uint8_t)(76u ^ 0xFFu);
    f2_rep[2 + 10] = (uint8_t)(0u ^ 0xFFu);
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_m7(f2_rep, sizeof(f2_rep), 0x06u, &r));
    ASSERT_EQ_INT(76, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);

    /* F3 BY Keyboard: wired & wireless */
    uint8_t by_w[12] = {0x06, 0x87, 0, 0, 0, 0, 0, 0, 0x17}; /* chg=1, lvl=7 -> 70% */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_by(by_w, sizeof(by_w), 1, &r));
    ASSERT_EQ_INT(70, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    uint8_t by_wl[8] = {0x13, 0x4A, 0x09}; /* chg=0, lvl=9 -> 90% */
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_by(by_wl, sizeof(by_wl), 0, &r));
    ASSERT_EQ_INT(90, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_DISCHARGING, r.power_state);

    /* F5 G-Series: with leading 0x00 Windows Report ID */
    uint8_t g5[16] = {0x00, 0xAA, 0x30, 0, 0, 0, 0, 0, 0, 64, 1};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(g5, sizeof(g5), VAR_INDEP_MCHOSE_G7, &r));
    ASSERT_EQ_INT(64, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* F6 G7 MAX: build CRC + parse */
    uint8_t g7m_req[64];
    octans_proto_build_mchose_g7max(0x03u, g7m_req);
    ASSERT_EQ_INT(0x03, g7m_req[0]);
    ASSERT_EQ_INT((0x50 + 0x00 + 0x02 + 0x4F + 0x81) & 0xFF, g7m_req[1]);
    uint8_t g7m_rep[16] = {0x03, 0, 0, 0, 0, 0, 0, (uint8_t)(0x80u | 55u)};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_g7max(g7m_rep, sizeof(g7m_rep), &r));
    ASSERT_EQ_INT(55, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    /* F7 / F9 Audio BLK & V9 TURBO: discharge(2), charging(3), full(4), sleep(26) */
    uint8_t v9_chg[8] = {0x55, 0x65, 81, 3};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(v9_chg, sizeof(v9_chg), VAR_INDEP_MCHOSE_V9, &r));
    ASSERT_EQ_INT(81, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    uint8_t v9_slp[8] = {0x55, 0x65, 0, 26};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_indep(v9_slp, sizeof(v9_slp), VAR_INDEP_MCHOSE_V9, &r));
    ASSERT_EQ_INT(1, r.asleep);
    ASSERT_EQ_INT(OCTANS_PWR_SLEEPING, r.power_state);

    /* F8 Audio MC (V9 PRO 2 / R9 / K20 GT): active + asleep */
    uint8_t mc_ok[16] = {0x55, 0xAA, 0x01, 0x00, 0x03, 0x00, 0x01, 0x0B, 0x01, (uint8_t)(0x80u | 67u)};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_audio_mc(mc_ok, sizeof(mc_ok), &r));
    ASSERT_EQ_INT(67, r.battery_percent);
    ASSERT_EQ_INT(OCTANS_PWR_CHARGING, r.power_state);

    uint8_t mc_slp[16] = {0x55, 0xAA, 0x01, 0x00, 0x03, 0x00, 0x01, 0x22, 0x00, 0x00};
    octans_reading_init(&r);
    ASSERT_EQ_INT(1, octans_proto_parse_mchose_audio_mc(mc_slp, sizeof(mc_slp), &r));
    ASSERT_EQ_INT(1, r.asleep);
    ASSERT_EQ_INT(OCTANS_PWR_SLEEPING, r.power_state);
}

static void test_proto_obf_and_table_match(void) {
    /* Round-trip 16-bit, 8-bit, and byte-array compile-time obfuscation */
    ASSERT_EQ_INT(0x1532, octans_dec16(OCTANS_ENC16(0x1532u, 1u), 1u));
    ASSERT_EQ_INT(0xFFFF, octans_dec16(OCTANS_ENC16(0xFFFFu, 2u), 2u));
    ASSERT_EQ_INT(0x0000, octans_dec16(OCTANS_ENC16(0x0000u, 3u), 3u));
    ASSERT_EQ_INT(0x4D, octans_dec8(OCTANS_ENC8(0x4Du, 5u), 5u));

    static const uint8_t enc_seq[5] = {
        OCTANS_ENC8(0x55, 60), OCTANS_ENC8(0xAA, 61), OCTANS_ENC8(0x01, 62),
        OCTANS_ENC8(0x00, 63), OCTANS_ENC8(0x0B, 64)
    };
    uint8_t dec_seq[5] = {0};
    octans_dec_bytes(dec_seq, enc_seq, sizeof(dec_seq), 60u);
    ASSERT_EQ_INT(0x55, dec_seq[0]);
    ASSERT_EQ_INT(0xAA, dec_seq[1]);
    ASSERT_EQ_INT(0x01, dec_seq[2]);
    ASSERT_EQ_INT(0x00, dec_seq[3]);
    ASSERT_EQ_INT(0x0B, dec_seq[4]);

    /* Verify obfuscated g_protocols[] still matches real HID interfaces accurately */
    octans_hid_info_t info;
    memset(&info, 0, sizeof(info));
    info.vid = 0x3837u;
    info.pid = 0x1234u;
    info.usage_page = 0xFF01u;
    info.usage = 0x0000u;
    info.interface_num = 1;
    int matched_mchose = 0;
    for (size_t i = 0; i < g_protocols_count; ++i) {
        if (octans_proto_matches_iface(&g_protocols[i], &info) &&
            g_protocols[i].variant == VAR_INDEP_MCHOSE_HID4D) {
            matched_mchose = 1;
            break;
        }
    }
    ASSERT_EQ_INT(1, matched_mchose);

    memset(&info, 0, sizeof(info));
    info.vid = 0x3554u;
    info.pid = 0xF5D5u;
    info.usage_page = 0xFF02u;
    info.usage = 0x0002u;
    info.interface_num = 2;
    int matched_compx = 0;
    for (size_t i = 0; i < g_protocols_count; ++i) {
        if (octans_proto_matches_iface(&g_protocols[i], &info) &&
            g_protocols[i].family == FAM_COMPX) {
            matched_compx = 1;
            break;
        }
    }
    ASSERT_EQ_INT(1, matched_compx);

    /* Ensure non-matching VID is rejected */
    info.vid = 0x1234u;
    int matched_bogus = 0;
    for (size_t i = 0; i < g_protocols_count; ++i) {
        if (octans_proto_matches_iface(&g_protocols[i], &info)) {
            matched_bogus = 1;
            break;
        }
    }
    ASSERT_EQ_INT(0, matched_bogus);
}

int main(void) {
    test_checksums_and_curves();
    test_hidpp();
    test_razer_and_sinowealth();
    test_compx_and_atk();
    test_rog_corsair_steelseries_hyperx();
    test_rapoo_ajazz_eweadn_jieli();
    test_keyboards_and_independent();
    test_merge_json_and_abi();
    test_user_rules_and_broad();
    test_mchose_v2_suite();
    test_proto_obf_and_table_match();

    printf("test_engine: %d/%d assertions passed\n",
           g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
