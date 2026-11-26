#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "family_request.h"
#include "proto_obf.h"
#include "../core/hid_dev.h"
#include "../core/hid_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---- Family 3: Razer 90-byte ---- */

void octans_proto_build_razer90(uint8_t tx_id, uint8_t cmd_id, uint8_t out91[91]) {
    memset(out91, 0, 91);
    out91[0] = 0x00u;
    uint8_t *f = out91 + 1;
    f[0] = 0x00u;
    f[1] = tx_id;
    f[2] = 0x00u;
    f[3] = 0x00u;
    f[4] = 0x00u;
    f[5] = 0x02u;
    f[6] = 0x07u;
    f[7] = cmd_id;
    f[88] = octans_crc_xor(f, 2u, 87u);
    f[89] = 0x00u;
}

int octans_proto_parse_razer90(const uint8_t *buf,
                               size_t len,
                               uint8_t expected_cmd,
                               uint8_t *out_status,
                               uint8_t *out_val) {
    if (!buf || len < 90u) return 0;
    const uint8_t *f = buf;
    if (len >= 91u && buf[7] == 0x07u && buf[8] == expected_cmd &&
        !(buf[6] == 0x07u && buf[7] == expected_cmd)) {
        f = buf + 1;
    }
    if (f[2] != 0x00u || f[3] != 0x00u || f[6] != 0x07u || f[7] != expected_cmd) {
        return 0;
    }
    if (out_status) *out_status = f[0];
    if (out_val) *out_val = f[9];
    return 1;
}

void octans_proto_build_razer_pa(uint8_t cmd_id, uint8_t cmd_type, uint8_t out64[64]) {
    memset(out64, 0, 64);
    out64[0] = 0x02u;
    out64[1] = 0x80u;
    out64[2] = (cmd_type == 0x02u) ? 0x07u : 0x08u;
    out64[5] = 0x50u; /* 'P' */
    out64[6] = 0x41u; /* 'A' */
    out64[7] = (cmd_type == 0x02u) ? 0x0Eu : 0x08u;
    out64[9] = cmd_type;
    out64[10] = cmd_id;
    if (cmd_type == 0x02u) out64[11] = 0x01u;
}

int octans_proto_parse_razer_pa(const uint8_t *buf, size_t len, uint8_t cmd_id, uint8_t *out_val) {
    if (!buf || !out_val) return 0;
    for (int off = 0; off >= -1; --off) {
        int c = 12 + off;
        if (off == 0 && buf[0] != 0x02u) continue;
        if (c >= 0 && (size_t)(c + 3) < len && buf[c] == cmd_id && buf[c + 1] == 0x01u) {
            *out_val = buf[c + 3];
            return 1;
        }
    }
    return 0;
}

int octans_proto_parse_razer_bsv2_2020(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    static const uint8_t enc_prefix[9] = {
        OCTANS_ENC8(0xFF, 20), OCTANS_ENC8(0x0F, 21), OCTANS_ENC8(0x05, 22),
        OCTANS_ENC8(0xFE, 23), OCTANS_ENC8(0x12, 24), OCTANS_ENC8(0x04, 25),
        OCTANS_ENC8(0x1F, 26), OCTANS_ENC8(0x08, 27), OCTANS_ENC8(0x05, 28)
    };
    uint8_t prefix[9];
    octans_dec_bytes(prefix, enc_prefix, sizeof(prefix), 20u);
    if (!buf || len < 15u || !out) return 0;
    if (memcmp(buf, prefix, sizeof(prefix)) != 0) return 0;
    uint8_t state = buf[11];
    uint16_t mv = (uint16_t)(((uint16_t)buf[12] << 8) | buf[13]);
    uint8_t lvl = buf[14];
    if (lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    if (mv >= 2500u && mv <= 5000u) out->voltage_mv = mv;
    out->is_estimated = 1;
    out->power_state = (state == 0x06u) ? OCTANS_PWR_FULL
                      : (state == 0x09u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

/* ---- Family 4: Sinowealth ---- */

void octans_proto_build_sinowealth(uint8_t page, uint8_t cmd, uint8_t out65[65]) {
    memset(out65, 0, 65);
    out65[0] = 0x00u;
    out65[1] = 0x00u;
    out65[2] = 0x00u;
    out65[3] = 0x02u;
    out65[4] = 0x02u;
    out65[5] = page;
    out65[6] = cmd;
}

int octans_proto_parse_sinowealth(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out) return 0;
    if (len >= 4u && buf[0] == 0x03u && buf[1] == 0x00u && buf[2] <= 100u) {
        out->battery_percent = (int8_t)buf[2];
        out->power_state = buf[3] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }
    if (len < 8u) return 0;
    for (size_t i = 5u; i + 2u < len; ++i) {
        if (buf[i] == 0x83u && buf[i - 1u] == 0x00u && buf[i - 2u] == 0x02u &&
            (buf[i - 5u] == 0xA1u || buf[i - 5u] == 0xA2u)) {
            int pct = octans_clamp_percent((int)buf[i + 2u]);
            out->battery_percent = (int8_t)pct;
            out->power_state = buf[i + 1u] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
    }
    return 0;
}

/* ---- Family 5: Compx 17-byte ---- */

void octans_proto_build_compx(uint8_t cmd, uint8_t out17[17]) {
    memset(out17, 0, 17);
    out17[0] = 0x08u;
    out17[1] = cmd;
    out17[16] = octans_crc_compx(out17);
}

int octans_proto_parse_compx(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 17u || !out) return 0;
    if ((buf[0] != 0x08u && buf[0] != 0x09u) || buf[1] != 0x04u) return 0;
    /* Note: Variant B replies may replace [0] 0x08 with 0x09; checksum is computed on the frame. */
    uint8_t expected = octans_crc_compx(buf);
    uint8_t alt_frame[16];
    memcpy(alt_frame, buf, 16);
    alt_frame[0] = 0x08u;
    uint8_t expected_alt = octans_crc_compx(alt_frame);
    if (buf[16] != expected && buf[16] != expected_alt) return 0;

    if (buf[6] > 100u) return 0;
    out->battery_percent = (int8_t)buf[6];
    out->power_state = buf[7] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->voltage_mv = (uint16_t)(((uint16_t)buf[8] << 8) | buf[9]);
    out->valid = 1;
    return 1;
}

int octans_proto_parse_compx_link(const uint8_t *buf, size_t len, int *out_online) {
    if (!buf || len < 17u || !out_online) return 0;
    if ((buf[0] != 0x08u && buf[0] != 0x09u) || buf[1] != 0x03u) return 0;
    if (buf[16] != octans_crc_compx(buf)) return 0;
    *out_online = (buf[2] == 0x00u && buf[6] == 0x01u) ? 1 : 0;
    return 1;
}

/* ---- Family 6: ATK / VXE Nordic ---- */

void octans_proto_build_atk_54l15(uint8_t out64[64]) {
    memset(out64, 0, 64);
    out64[0] = 0x08u;
    out64[1] = 0x7Du;
    out64[2] = 0x72u;
    out64[3] = 0x02u;
    out64[4] = 0x00u;
    out64[5] = 0x01u;
    out64[6] = 0x07u;
    out64[7] = 0x01u;
}

int octans_proto_parse_atk_54l15(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 8u || !out) return 0;
    const uint8_t *d = (buf[0] == 0x00u && len >= 9u && buf[2] == 0x72u) ? (buf + 1) : buf;
    size_t n = (d == buf) ? len : (len - 1u);
    if (n < 8u || d[1] != 0x72u || d[5] != 0x07u || d[7] > 100u) return 0;
    out->battery_percent = (int8_t)d[7];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

void octans_proto_build_atk_52840(uint8_t out17[17]) {
    memset(out17, 0, 17);
    out17[0] = 0x08u;
    out17[1] = 0x04u;
    out17[16] = 0x49u;
}

int octans_proto_parse_atk_52840(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 8u || !out) return 0;
    if (buf[0] != 0x08u || buf[1] != 0x04u || buf[6] > 100u) return 0;
    out->battery_percent = (int8_t)buf[6];
    out->power_state = buf[7] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    if (buf[7]) out->conn = OCTANS_CONN_WIRED;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_atk_mouseinfo(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 20u || !out) return 0;
    const uint8_t *r = buf;
    size_t n = len;
    if (buf[0] != 0x13u && len >= 21u && buf[1] == 0x13u) {
        r = buf + 1;
        n = len - 1u;
    }
    if (n < 20u || r[0] != 0x13u || r[2] != 0x00u || r[17] > 100u) return 0;
    out->battery_percent = (int8_t)r[17];
    out->power_state = (r[18] == 1u) ? OCTANS_PWR_FULL
                     : (r[16] == 1u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

/* ---- Family 7: ASUS ROG ---- */

void octans_proto_build_rog_1207(uint8_t report_id, uint8_t out_buf[65], size_t *out_len) {
    memset(out_buf, 0, 65);
    out_buf[0] = report_id;
    out_buf[1] = 0x12u;
    out_buf[2] = 0x07u;
    if (out_len) *out_len = (report_id == 0x00u) ? 65u : 64u;
}

int octans_proto_parse_rog(const uint8_t *buf, size_t len, octans_proto_variant_t var, octans_proto_reading_t *out) {
    if (!buf || len < 12u || !out) return 0;

    if ((var == VAR_ROG_OMNI_RX || var == VAR_ROG_OMNI_KB) &&
        buf[0] == 0x02u && buf[1] == 0x12u && buf[2] == 0x01u) {
        if (buf[6] > 100u) return 0;
        out->kind = OCTANS_DEV_KEYBOARD;
        out->battery_percent = (int8_t)buf[6];
        out->power_state = buf[9] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    const uint8_t *p = buf;
    if (buf[0] == 0x12u && buf[1] == 0x07u) {
        p = buf;
    } else if (len >= 13u && (buf[0] == 0x00u || buf[0] == 0x03u) && buf[1] == 0x12u && buf[2] == 0x07u) {
        p = buf + 1;
    } else {
        return 0;
    }

    if (p[4] == 0xFFu && p[5] == 0xAAu) return 0;
    uint8_t pct = p[4];
    /* Note: docs/02 line 238 specifies byte5 on the 03-prefixed Harpe/OMNI frame (= p[4] unprefixed). */
    if (p != buf) pct = buf[5];
    uint8_t chg = (p != buf) ? buf[10] : p[9];
    uint16_t mv = (p != buf)
        ? (uint16_t)(((uint16_t)buf[8] << 8) | buf[9])
        : (uint16_t)(((uint16_t)p[7] << 8) | p[8]);

    if (pct == 0u && chg == 0u) return 0; /* standby/idle, not 0% */
    if (pct <= 4u && chg == 0u && mv == 0u) {
        /* Legacy 0..4 step gauge (Keris/Chakram) */
        out->battery_percent = (int8_t)(pct * 25u);
        out->is_estimated = 1;
    } else if (pct <= 100u) {
        out->battery_percent = (int8_t)pct;
    } else {
        return 0;
    }
    if (mv >= 2500u && mv <= 5000u) out->voltage_mv = mv;
    out->power_state = chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

/* ---- Family 8: Corsair ---- */

void octans_proto_build_corsair_bragi(uint8_t target, uint8_t prop, uint8_t out64[64]) {
    memset(out64, 0, 64);
    out64[0] = 0x02u;
    out64[1] = target;
    out64[2] = 0x02u;
    out64[3] = prop;
}

int octans_proto_parse_corsair_bragi(const uint8_t *buf, size_t len, uint16_t *out_raw) {
    if (!buf || len < 6u || !out_raw) return 0;
    const uint8_t *d = (buf[0] == 0x00u && len >= 7u && buf[1] == 0x01u) ? (buf + 1) : buf;
    size_t n = (d == buf) ? len : (len - 1u);
    if (n < 6u || d[0] != 0x01u || d[2] != 0x02u || d[3] != 0x00u) return 0;
    *out_raw = (uint16_t)((uint16_t)d[4] | ((uint16_t)d[5] << 8));
    return 1;
}

int octans_proto_parse_corsair_void_elite(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 5u || !out) return 0;
    const uint8_t *d = (buf[0] == 0x00u && len >= 6u && buf[1] == 0x64u) ? (buf + 1) : buf;
    size_t n = (d == buf) ? len : (len - 1u);
    if (n < 5u || d[0] != 0x64u) return 0;
    uint8_t lvl = d[2] & 0x7Fu;
    if (lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (d[4] == 4u || d[4] == 5u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_corsair_legacy_nxp(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 7u || !out) return 0;
    const uint8_t *p = buf;
    size_t n = len;
    if (buf[0] == 0x0Eu && len >= 8u) {
        p = buf + 1;
        n = len - 1u;
    }
    if (n < 7u) return 0;
    uint8_t status = p[4];
    uint8_t charge = p[5];
    int pct = -1;
    switch (status) {
        case 2u: pct = 100; break; /* Charged */
        case 3u: pct = 90;  break; /* High */
        case 4u: pct = 60;  break; /* Medium */
        case 5u: pct = 30;  break; /* Low */
        case 6u: pct = 5;   break; /* VeryLow */
        case 7u: pct = 10;  break; /* CriticalLow */
        default: return 0;
    }
    out->battery_percent = (int8_t)pct;
    out->is_estimated = 1;
    out->power_state = (status == 2u) ? OCTANS_PWR_FULL
                     : ((charge == 2u || charge == 3u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

/* ---- Family 9: SteelSeries ---- */

int octans_proto_parse_steelseries(const uint8_t *buf,
                                   size_t len,
                                   octans_proto_variant_t var,
                                   octans_proto_reading_t *out) {
    if (!buf || len < 3u || !out) return 0;

    if (var == VAR_SS_RIVAL3) {
        const uint8_t *d = (buf[0] == 0x00u && len >= 5u && buf[1] == 0xAAu) ? (buf + 1) : buf;
        size_t n = (d == buf) ? len : (len - 1u);
        if (n >= 4u && d[0] == 0xAAu && d[1] <= 100u) {
            out->battery_percent = (int8_t)d[1];
            out->power_state = d[3] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (len == 3u && buf[0] <= 100u) {
            out->battery_percent = (int8_t)buf[0];
            out->power_state = buf[2] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        return 0;
    }

    if (var == VAR_SS_AEROX) {
        const uint8_t *d = (buf[0] == 0x00u && len >= 3u && buf[1] == 0xD2u) ? (buf + 1) : buf;
        size_t n = (d == buf) ? len : (len - 1u);
        if (n < 2u || d[0] != 0xD2u) return 0;
        uint8_t raw = d[1];
        int charging = (raw & 0x80u) != 0;
        uint8_t v = raw & 0x7Fu;
        int pct;
        if (v >= 1u && v <= 21u) {
            pct = (int)(v - 1u) * 5;
            out->is_estimated = 1;
        } else if (v <= 100u) {
            pct = (int)v;
        } else {
            return 0;
        }
        out->battery_percent = (int8_t)pct;
        out->power_state = charging ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_SS_NOVA7) {
        const uint8_t *d = (buf[0] == 0x00u && len >= 5u && buf[1] == 0xB0u) ? (buf + 1) : buf;
        size_t n = (d == buf) ? len : (len - 1u);
        if (n < 4u || d[0] != 0xB0u) return 0;
        uint8_t st = d[3];
        if (st == 0x00u) return 0; /* headset off */
        int pct = (d[2] <= 4u) ? (int)d[2] * 25 : (int)octans_clamp_percent((int)d[2]);
        out->battery_percent = (int8_t)pct;
        out->power_state = (st == 0x01u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_SS_NOVA_PRO && len >= 16u && buf[0] == 0x06u) {
        if (buf[15] == 0x01u) return 0; /* off */
        if (buf[6] > 8u) return 0;
        out->battery_percent = (int8_t)octans_round_div((uint32_t)buf[6] * 100u, 8u);
        out->is_estimated = 1;
        out->power_state = (buf[15] == 0x02u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_SS_NOVA_ELITE && len >= 16u && buf[0] == 0x01u && buf[1] == 0xB0u) {
        if (buf[14] == 0x01u || buf[6] > 100u) return 0;
        out->battery_percent = (int8_t)buf[6];
        out->power_state = (buf[15] == 0x02u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_SS_ARCTIS1 && len >= 4u) {
        if (buf[2] == 0x01u) {
            out->offline = 1;
            out->power_state = OCTANS_PWR_OFFLINE;
            return 1;
        }
        if (buf[3] > 100u) return 0;
        out->battery_percent = (int8_t)buf[3];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_SS_GAMEBUDS && len >= 7u) {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] == 0x00u && len >= 8u && buf[1] == 0xB0u) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 7u || d[0] != 0xB0u) return 0;
        int l_act = (d[3] == 0x03u && d[5] <= 100u);
        int r_act = (d[4] == 0x03u && d[6] <= 100u);
        if (!l_act && !r_act) {
            out->offline = 1;
            out->power_state = OCTANS_PWR_OFFLINE;
            return 1;
        }
        int pct = (l_act && r_act) ? (d[5] < d[6] ? d[5] : d[6]) : (l_act ? d[5] : d[6]);
        out->battery_percent = (int8_t)pct;
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    return 0;
}

/* ---- Family 10: HyperX ---- */

int octans_proto_parse_hyperx(const uint8_t *buf,
                              size_t len,
                              octans_proto_variant_t var,
                              octans_proto_reading_t *out) {
    if (!buf || !out) return 0;

    if (var == VAR_HX_GEN1 && len >= 8u && buf[0] == 0x06u && buf[1] == 0xFFu && buf[2] == 0xBBu) {
        if (buf[3] == 0x02u && buf[7] <= 100u) {
            out->battery_percent = (int8_t)buf[7];
            out->voltage_mv = (uint16_t)((uint16_t)buf[5] | ((uint16_t)buf[6] << 8));
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (buf[3] == 0x03u) {
            out->power_state = buf[4] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            return 1;
        }
        return 0;
    }

    if (var == VAR_HX_GEN2 && len >= 5u && buf[0] == 0x66u && buf[1] == 0x89u) {
        if ((buf[2] == 0u && buf[3] == 0u) || buf[4] > 100u) return 0;
        out->battery_percent = (int8_t)buf[4];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_HX_GEN3_HASTE2 && len >= 8u && buf[0] == 0x51u && buf[1] == 0x02u) {
        if (buf[2] > 100u) return 0;
        out->battery_percent = (int8_t)buf[2];
        out->power_state = (buf[3] == 2u) ? OCTANS_PWR_FULL
                          : (buf[3] == 1u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
        out->voltage_mv = (uint16_t)((uint16_t)buf[6] | ((uint16_t)buf[7] << 8));
        out->valid = 1;
        return 1;
    }

    if (var == VAR_HX_CLOUD3S && len >= 7u && buf[0] == 0x0Cu && buf[6] != 0xFFu) {
        if (buf[5] == 0x06u && buf[6] <= 100u) {
            out->battery_percent = (int8_t)buf[6];
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (buf[5] == 0x48u) {
            out->power_state = buf[6] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            return 1;
        }
        return 0;
    }

    if (var == VAR_HX_ALPHA2 && len >= 7u && buf[0] == 0x51u && buf[1] == 0x02u) {
        if (buf[2] > 100u) return 0;
        out->battery_percent = (int8_t)buf[2];
        out->power_state = (buf[6] & 0x80u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_HX_ALPHA_WIRELESS && len >= 4u && buf[0] == 0x21u && buf[1] == 0xBBu) {
        if (buf[2] == 0x0Bu && buf[3] <= 100u) {
            out->battery_percent = (int8_t)buf[3];
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (buf[2] == 0x0Cu && (buf[3] == 0u || buf[3] == 1u)) {
            out->power_state = buf[3] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            return 1;
        }
        return 0;
    }

    if (var == VAR_HX_AIROHA && len >= 9u) {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] != 0x05u && len >= 10u && buf[1] == 0x05u) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 9u || d[0] != 0x05u || d[6] != 0x00u) return 0;
        uint16_t race = (uint16_t)((uint16_t)d[4] | ((uint16_t)d[5] << 8));
        if (d[1] == 0x5Bu && race == 0x30CFu && d[8] <= 100u) {
            out->battery_percent = (int8_t)d[8];
            out->power_state = (d[7] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (d[1] == 0x5Du && race == 0x0CD6u && d[8] <= 100u) {
            out->battery_percent = (int8_t)d[8];
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        return 0;
    }

    if (var == VAR_HX_FLIGHT && (len == 15u || len == 20u || (len >= 15u && buf[0] == 0x21u))) {
        uint16_t mv = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);
        if (mv == 0u) return 0;
        out->voltage_mv = mv;
        out->is_estimated = 1;
        if (mv > 0x100Bu) {
            out->battery_percent = 100;
            out->power_state = OCTANS_PWR_CHARGING;
            out->valid = 1;
            return 1;
        }
        static const uint16_t k_mv[9] = {3300u, 3648u, 3700u, 3750u, 3800u, 3850u, 3900u, 3940u, 3975u};
        static const uint8_t  k_pc[9] = {0u,    5u,    15u,   30u,   50u,   70u,   85u,   95u,   100u};
        int pct = 0;
        if (mv <= k_mv[0]) {
            pct = 0;
        } else if (mv >= k_mv[8]) {
            pct = 100;
        } else {
            for (size_t i = 0; i < 8u; ++i) {
                if (mv <= k_mv[i + 1u]) {
                    uint32_t num = (uint32_t)(mv - k_mv[i]) * (uint32_t)(k_pc[i + 1u] - k_pc[i]);
                    uint32_t den = (uint32_t)(k_mv[i + 1u] - k_mv[i]);
                    pct = (int)k_pc[i] + (int)octans_round_div(num, den);
                    break;
                }
            }
        }
        out->battery_percent = (int8_t)octans_clamp_percent(pct);
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    return 0;
}

/* ---- Family 11: Rapoo (active Vision AP / CH315 + passive fallback) ---- */

void octans_proto_build_rapoo_vision(uint8_t out64[64]) {
    memset(out64, 0, 64u);
    out64[0] = 0x04u; /* Report ID 4 */
    uint8_t *f = out64 + 1;
    f[2] = 0xADu;     /* VIS_GET_BATTERY_LEVEL (173) */
    f[3] = 0x06u;
    uint16_t sum = 0;
    for (size_t i = 2u; i < 63u; ++i) sum = (uint16_t)(sum + f[i]);
    f[0] = (uint8_t)(sum & 0xFFu);
    f[1] = (uint8_t)((sum >> 8) & 0xFFu);
}

int octans_proto_parse_rapoo(const uint8_t *buf,
                             size_t len,
                             octans_proto_variant_t var,
                             octans_proto_reading_t *out) {
    if (!buf || !out) return 0;

    if (var == VAR_RAPOO_VISION_AP && len >= 9u) {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] == 0x04u && len >= 10u && buf[3] == 0xADu) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 9u || d[2] != 0xADu || d[3] > 56u || d[6] != 0u) return 0;
        if (n < (size_t)(7u + d[3])) return 0;
        if (d[7] > 100u || (d[8] != 0u && d[8] != 1u)) return 0;
        out->battery_percent = (int8_t)d[7];
        out->power_state = (d[8] == 1u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_RAPOO_CH315 && len >= 5u) {
        if ((buf[2] == 1u || buf[2] == 2u) && buf[3] == 1u && buf[4] <= 100u && buf[4] > 0u) {
            out->battery_percent = (int8_t)buf[4];
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        if (len >= 7u && (buf[4] == 1u || buf[4] == 2u) && buf[5] == 1u && buf[6] <= 100u && buf[6] > 0u) {
            out->battery_percent = (int8_t)buf[6];
            out->power_state = OCTANS_PWR_DISCHARGING;
            out->valid = 1;
            return 1;
        }
        return 0;
    }

    if (var == VAR_RAPOO_MOUSE_R7 && len >= 8u) {
        uint8_t state = buf[6];
        uint8_t pct = buf[7];
        if ((state != 1u && state != 2u) || pct > 100u) return 0;
        uint8_t link = buf[0] & 0x0Fu;
        out->conn = (link == 1u) ? OCTANS_CONN_BLUETOOTH
                  : (link == 2u ? OCTANS_CONN_WIRED : OCTANS_CONN_DONGLE_24G);
        out->battery_percent = (int8_t)pct;
        out->power_state = (state == 2u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_RAPOO_KB_BD && len >= 9u && buf[0] == 0xBDu && buf[1] == 0x01u) {
        uint8_t state = buf[7];
        uint8_t pct = buf[8];
        if ((state != 1u && state != 2u) || pct > 100u) return 0;
        out->battery_percent = (int8_t)pct;
        out->power_state = (state == 2u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    return 0;
}

/* ---- Family 12: AJAZZ & EWEADN ---- */

void octans_proto_build_eweadn(uint8_t cmd, uint8_t out33[33]) {
    memset(out33, 0, 33);
    out33[0] = 0x00u;
    out33[1] = cmd;
    out33[32] = octans_crc_sum_range(out33, 5u, 31u);
}

int octans_proto_parse_eweadn(const uint8_t *buf, size_t len, octans_proto_reading_t *out, char out_dev_id[8]) {
    if (!buf || len < 3u || !out) return 0;
    const uint8_t *d = buf;
    size_t n = len;
    if (buf[0] == 0x00u && len >= 4u &&
        (buf[1] == 0x30u || buf[1] == 0x10u || buf[1] == 0xD1u || buf[1] == 0xC1u)) {
        d = buf + 1;
        n = len - 1u;
    }
    if (d[0] == 0xD1u && d[1] == 0x01u && d[2] <= 100u) {
        out->battery_percent = (int8_t)d[2];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }
    if (d[0] == 0xC1u && n >= 4u && d[2] <= 100u && d[2] > 0u) {
        out->battery_percent = (int8_t)d[2];
        out->power_state = d[3] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }
    if (d[0] != 0x30u && d[0] != 0x10u) return 0;
    if (d[1] == 0x02u) {
        out->asleep = 1;
        out->power_state = OCTANS_PWR_SLEEPING;
        return 1;
    }
    if (n < 15u || d[1] != 0x00u || d[13] > 100u) return 0;
    if (d[0] == 0x10u && d[13] == 0u && d[14] == 0u) return 0;
    out->battery_percent = (int8_t)d[13];
    out->power_state = d[12] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    if (out_dev_id) {
        size_t k = 0;
        for (size_t i = 4u; i < 8u; ++i) {
            if (d[i] >= 32u && d[i] < 127u) out_dev_id[k++] = (char)d[i];
        }
        out_dev_id[k] = '\0';
    }
    return 1;
}

int octans_proto_parse_ajazz_0x10(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out) return 0;
    if (len >= 3u && buf[0] == 0xC0u && buf[1] == 0x01u && buf[2] <= 100u) {
        out->battery_percent = (int8_t)buf[2];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }
    if (len < 32u) return 0;
    if (buf[0] != 0x10u || buf[1] != 0x00u || buf[2] != 0x01u || buf[3] != 0x0Bu) return 0;
    if (octans_crc_sum_range(buf, 4u, 30u) != buf[31]) return 0;
    if (buf[13] > 100u) return 0;
    out->battery_percent = (int8_t)buf[13];
    out->power_state = (buf[14] == 0x00u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_ajazz_0x05(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 4u || !out) return 0;
    if (buf[0] != 0x05u || buf[3] > 100u) return 0;
    out->battery_percent = (int8_t)buf[3];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_ajazz_mkbms(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 40u || !out) return 0;
    const uint8_t *d = buf;
    size_t n = len;
    if (buf[0] == 0x00u && len >= 41u && buf[1] == 0x55u && buf[3] == 0x80u) {
        d = buf + 1;
        n = len - 1u;
    }
    if (n < 40u || d[0] != 0x55u || d[2] != 0x80u || d[3] != 0x20u) return 0;
    if (d[3] == 0xFFu && d[4] == 0xFFu && d[5] == 0xFFu) return 0;
    uint8_t flags = d[4];
    int plugged = (flags & 0x80u) != 0;
    int full = plugged && ((flags & 0x40u) != 0);
    int chg = plugged && ((flags & 0x40u) == 0);
    uint8_t pct = full ? 100u : d[5];
    if (pct > 100u) return 0;
    out->battery_percent = (int8_t)pct;
    out->power_state = full ? OCTANS_PWR_FULL : (chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    if (d[6] == 1u || d[6] == 3u) out->conn = OCTANS_CONN_WIRED;
    else if (d[6] == 2u) out->conn = OCTANS_CONN_DONGLE_24G;
    out->valid = 1;
    return 1;
}

/* ---- Family 13: Jieli JL_RCSP TLV ---- */

void octans_proto_build_jieli_query(uint8_t seq, uint8_t target, uint8_t tlv_type, uint8_t out65[65]) {
    memset(out65, 0, 65);
    out65[0] = 0x00u;
    out65[1] = 0xFEu;
    out65[2] = seq;
    out65[3] = target;
    out65[4] = 0x00u;
    out65[5] = 0x02u;
    out65[6] = 0x00u;
    out65[7] = 0x00u;
    out65[8] = 0x02u; /* len LE16 = 2 */
    out65[9] = 0x00u;
    out65[10] = tlv_type;
    out65[11] = 0x00u;
    uint16_t crc = octans_crc16_ccitt_false(out65 + 1, 11u);
    out65[12] = (uint8_t)(crc >> 8);
    out65[13] = (uint8_t)(crc & 0xFFu);
}

int octans_proto_parse_jieli_tlv(const uint8_t *buf, size_t len, uint8_t expected_seq, uint8_t *out_pct) {
    if (!buf || len < 14u || !out_pct) return 0;
    if (buf[1] != 0xFEu || buf[2] != expected_seq) return 0;
    uint16_t payload_len = (uint16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8));
    if ((size_t)(10u + payload_len + 2u) > len) return 0;

    uint16_t calc_crc = octans_crc16_ccitt_false(buf + 1, (size_t)(9u + payload_len));
    uint16_t got_crc = (uint16_t)(((uint16_t)buf[10u + payload_len] << 8) | buf[11u + payload_len]);
    if (calc_crc != got_crc) return 0;

    size_t pos = 10u;
    size_t end = 10u + (size_t)payload_len;
    while (pos + 2u <= end) {
        uint8_t t = buf[pos];
        uint8_t l = buf[pos + 1u];
        pos += 2u;
        if (pos + l > end) break;
        if (t == 0x01u && l >= 1u && buf[pos] <= 100u) {
            *out_pct = buf[pos];
            return 1;
        }
        pos += l;
    }
    return 0;
}

/* ---- Family 14: Keyboards ---- */

void octans_proto_build_via_battery(uint8_t out33[33]) {
    memset(out33, 0, 33);
    out33[0] = 0x00u;
    out33[1] = 0x08u; /* custom get */
    out33[2] = 0x00u; /* channel 0 */
    out33[3] = 0x07u; /* value 7 = battery */
}

void octans_proto_build_semico_aa55(uint8_t cmd, int with_report_id, uint8_t *out_buf, size_t len) {
    if (!out_buf || len < 6u) return;
    memset(out_buf, 0, len);
    size_t off = with_report_id ? 1u : 0u;
    if (off + 5u > len) return;
    if (with_report_id) out_buf[0] = 0x00u;
    out_buf[off + 0u] = 0xAAu;
    out_buf[off + 1u] = 0x55u;
    out_buf[off + 2u] = 0xCCu;
    out_buf[off + 3u] = 0x33u;
    out_buf[off + 4u] = cmd;
}

int octans_proto_parse_semico_aa55(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 7u || !out) return 0;
    const uint8_t *p = NULL;
    if (buf[0] == 0xAAu && buf[1] == 0x55u && buf[2] == 0xCCu && buf[3] == 0x33u) {
        p = buf;
    } else if (len >= 8u && buf[0] == 0x00u &&
               buf[1] == 0xAAu && buf[2] == 0x55u && buf[3] == 0xCCu && buf[4] == 0x33u) {
        p = buf + 1;
    } else {
        return 0;
    }

    uint8_t pct = p[5];
    uint8_t st = p[6];
    if (st == 0u) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    if (pct > 100u) return 0;
    out->battery_percent = (int8_t)pct;
    out->power_state = (st == 3u) ? OCTANS_PWR_FULL
                      : (st == 1u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    if (st == 3u) out->conn = OCTANS_CONN_WIRED;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_keyboard(const uint8_t *buf,
                                size_t len,
                                octans_proto_variant_t var,
                                uint32_t expected_req_id,
                                octans_proto_reading_t *out) {
    if (!buf || !out) return 0;

    if (var == VAR_KB_ATK_EVISION && len >= 9u) {
        const uint8_t *p = (buf[0] == 0x04u) ? (buf + 1) : buf;
        size_t avail = (buf[0] == 0x04u) ? (len - 1u) : len;
        if (avail < 8u) return 0;
        uint32_t rid = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        if (expected_req_id != 0u && rid != expected_req_id) return 0;
        if (p[4] != 0x1Au || p[6] > 100u) return 0;
        out->battery_percent = (int8_t)p[6];
        out->power_state = (p[7] == 1u || p[7] == 2u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_LOFREE_HYZEN && len >= 9u) {
        const uint8_t *p = (buf[0] == 0x04u) ? (buf + 1) : buf;
        size_t avail = (buf[0] == 0x04u) ? (len - 1u) : len;
        if (avail < 8u || p[4] != 0x1Au || p[6] > 100u) return 0;
        out->battery_percent = (int8_t)p[6];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_KEYCHRON && len >= 21u) {
        if (buf[0] != 0xB4u || buf[1] != 0x06u || buf[20] > 100u) return 0;
        out->battery_percent = (int8_t)buf[20];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_VIA_RAW && len >= 4u) {
        const uint8_t *p = buf;
        if (len >= 33u && buf[0] == 0x00u && buf[1] == 0x08u) p = buf + 1;
        if (p[0] != 0x08u || p[1] != 0x00u || p[2] != 0x07u || p[3] > 100u) return 0;
        out->battery_percent = (int8_t)p[3];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_ATK_HFD && len >= 5u) {
        const uint8_t *p = buf;
        size_t n = len;
        if (buf[0] == 0x00u && len >= 6u && buf[1] == 0x02u) {
            p = buf + 1;
            n = len - 1u;
        }
        if (n < 5u || p[0] != 0x02u || p[3] > 100u) return 0;
        out->battery_percent = (int8_t)p[3];
        out->power_state = (p[4] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_DURGOD_3123 && len >= 4u) {
        const uint8_t *p = buf;
        size_t n = len;
        if (buf[0] == 0x00u && len >= 5u && buf[1] == 0x82u) {
            p = buf + 1;
            n = len - 1u;
        }
        if (n < 4u || p[1] == 0u || p[1] > 100u) return 0;
        out->battery_percent = (int8_t)p[1];
        out->power_state = (p[2] == 2u) ? OCTANS_PWR_FULL
                         : (p[2] == 1u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
        out->valid = 1;
        return 1;
    }

    if (var == VAR_KB_WOB_RAINY75V2) {
        return octans_proto_parse_wob_rainy75v2(buf, len, out);
    }
    if (var == VAR_KB_WOB_ZEN65) {
        return octans_proto_parse_wob_zen65(buf, len, out);
    }
    if (var == VAR_KB_RONGYUAN_F7) {
        return octans_proto_parse_rongyuan_f7(buf, len, out);
    }

    return 0;
}

/* ---- Family 15: Independent ---- */

static int wstr_contains_nocase(const wchar_t *hay, const wchar_t *needle) {
    if (!hay || !needle || !needle[0]) return 0;
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = hay; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) return 1;
    }
    return 0;
}

static int is_mchose_cz_kb_name(const wchar_t *name) {
    static const wchar_t *const k[] = {
        L"ACE60", L"ACE68", L"ACE75", L"G75 V2", L"G75_V2", L"G87 V2", L"G87_V2",
        L"G87 V3", L"G98 V3", L"GOD60", L"GT75", L"JET75", L"K87 V3", L"K87S",
        L"K99 V3", L"MIX87", L"ZERO75X", L"Keyboard", NULL
    };
    for (size_t i = 0; k[i]; ++i) {
        if (wstr_contains_nocase(name, k[i])) return 1;
    }
    return 0;
}

static int is_mchose_by_exact_pid(uint16_t vid, uint16_t pid) {
    if (vid == 0x3837u && (pid == 0x201Au || pid == 0x201Bu)) return 1;
    if (vid == 0x41E4u && (pid == 0x2001u || pid == 0x2201u)) return 1;
    return 0;
}

static int is_mchose_by_kb_name(const wchar_t *name) {
    if (!name || !name[0]) return 0;
    if (wstr_contains_nocase(name, L"G98 V3")) return 0;
    static const wchar_t *const k[] = {
        L"G87", L"K99", L"G98", L"UT98", L"Z75", L"X75", L"GX87", L"G75", NULL
    };
    for (size_t i = 0; k[i]; ++i) {
        if (wstr_contains_nocase(name, k[i])) return 1;
    }
    if (wstr_contains_nocase(name, L"K87") && !wstr_contains_nocase(name, L"K87S")) return 1;
    return 0;
}

void octans_proto_build_mchose_hid4d(uint8_t report_id, uint16_t cmd, uint8_t *out_buf, size_t len) {
    if (!out_buf || len < 9u) return;
    memset(out_buf, 0, len);
    out_buf[0] = report_id;
    out_buf[1] = 0x01u; /* version */
    out_buf[2] = 0x01u; /* flags: with XOR checksum */
    out_buf[3] = 0x00u; /* data len = 0 */
    out_buf[4] = (uint8_t)(cmd & 0xFFu);
    out_buf[5] = (uint8_t)((cmd >> 8) & 0xFFu);
    out_buf[6] = 0x00u; /* bizCode */
    out_buf[7] = 0x00u; /* sequence */
    out_buf[8] = octans_crc_xor(out_buf, 2u, 7u);
}

int octans_proto_parse_mchose_hid4d(const uint8_t *buf, size_t len, uint16_t expected_vid, octans_proto_reading_t *out) {
    if (!buf || len < 14u || !out) return 0;
    if (buf[0] == 0xFFu || (buf[0] == 0x4Du && buf[1] == 0xFFu)) return 0;

    int p = -1;
    if (expected_vid != 0u) {
        uint8_t lo = (uint8_t)(expected_vid & 0xFFu);
        uint8_t hi = (uint8_t)((expected_vid >> 8) & 0xFFu);
        for (size_t i = 0; i + 13u <= len; ++i) {
            if (buf[i] == lo && buf[i + 1u] == hi) {
                p = (int)i;
                break;
            }
        }
    }
    if (p < 0) {
        for (size_t i = 0; i + 17u <= len; ++i) {
            if (buf[i] == 0x00u && buf[i + 1u] == 0x09u) {
                p = (int)(i + 4u);
                break;
            }
        }
    }
    if (p < 0 || (size_t)p + 13u > len) return 0;

    uint8_t lvl = buf[(size_t)p + 12u];
    uint8_t chg = buf[(size_t)p + 11u];
    if (lvl == 0u || lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (chg == 1u) ? OCTANS_PWR_CHARGING
                     : (chg == 2u ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

int octans_proto_parse_mchose_hid4d_batt(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 8u || !out) return 0;
    if (buf[0] == 0xFFu || (buf[0] == 0x4Du && buf[1] == 0xFFu)) return 0;

    int p = -1;
    for (size_t i = 0; i + 6u <= len; ++i) {
        if (buf[i] == 0x04u && buf[i + 1u] == 0x09u) {
            p = (int)(i + 4u);
            break;
        }
    }
    if (p < 0 || (size_t)p + 2u > len) return 0;

    uint8_t chg = buf[(size_t)p];
    uint8_t lvl = buf[(size_t)p + 1u];
    if (lvl == 0u || lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (chg == 1u) ? OCTANS_PWR_CHARGING
                     : (chg == 2u ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

void octans_proto_build_mchose_m7(uint8_t report_id, uint8_t cmd, uint8_t *out_buf, size_t len) {
    if (!out_buf || len < 2u) return;
    memset(out_buf, 0xFF, len);
    out_buf[0] = report_id;
    out_buf[1] = (uint8_t)(cmd ^ 0xFFu);
}

int octans_proto_parse_mchose_m7(const uint8_t *buf, size_t len, uint8_t expected_cmd, octans_proto_reading_t *out) {
    if (!buf || len < 13u || !out) return 0;
    uint8_t inv_cmd = (uint8_t)(expected_cmd ^ 0xFFu);
    int cmd_idx = -1;
    if ((buf[0] == 0x11u || buf[0] == 0x12u) && buf[1] == inv_cmd) {
        cmd_idx = 1;
    } else if (buf[0] == inv_cmd) {
        cmd_idx = 0;
    } else {
        return 0;
    }
    size_t base = (size_t)(cmd_idx + 1);
    if (base + 11u > len) return 0;

    uint8_t dec[62];
    size_t dlen = len - base;
    if (dlen > sizeof(dec)) dlen = sizeof(dec);
    int all_zero = 1;
    for (size_t i = 0; i < dlen; ++i) {
        dec[i] = (uint8_t)(buf[base + i] ^ 0xFFu);
        if (dec[i] != 0u) all_zero = 0;
    }
    if (all_zero) {
        out->asleep = 1;
        out->power_state = OCTANS_PWR_SLEEPING;
        return 1;
    }
    uint8_t lvl = dec[9];
    if (lvl == 0u || lvl > 100u) {
        if (dlen < 13u || dec[12] == 0u || dec[12] > 100u) return 0;
        lvl = dec[12];
    }
    out->battery_percent = (int8_t)lvl;
    out->power_state = (dec[10] == 1u) ? OCTANS_PWR_CHARGING
                     : (dec[10] == 2u ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
    out->conn = (dec[8] == 0x00u && dec[10]) ? OCTANS_CONN_WIRED : OCTANS_CONN_DONGLE_24G;
    out->valid = 1;
    return 1;
}

void octans_proto_build_mchose_g7max(uint8_t report_id, uint8_t out64[64]) {
    if (!out64) return;
    memset(out64, 0, 64u);
    out64[0] = report_id; /* 0x03 */
    out64[2] = 0x50u;
    out64[3] = 0x00u;
    out64[4] = 0x02u;
    out64[5] = 0x4Fu;
    out64[6] = 0x81u; /* BASICS */
    out64[1] = octans_crc_sum_range(out64, 2u, 63u);
}

int octans_proto_parse_mchose_g7max(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 8u || !out) return 0;
    size_t base = (buf[0] == 0x03u && len >= 9u) ? 1u : 0u;
    if (base + 7u > len) return 0;
    uint8_t v = buf[base + 6u];
    uint8_t lvl = (uint8_t)(v & 0x7Fu);
    if (lvl == 0u || lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (v & 0x80u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

void octans_proto_build_mchose_by_wireless(uint8_t out20[20]) {
    if (!out20) return;
    memset(out20, 0, 20u);
    out20[0] = 0x13u; /* Report ID 19 */
    out20[1] = 0x4Au; /* getBattery command */
    out20[2] = 0x01u;
    out20[19] = octans_crc_sum_range(out20, 0u, 18u); /* 0x5E checksum */
}

int octans_proto_parse_mchose_by(const uint8_t *buf, size_t len, int is_wired, octans_proto_reading_t *out) {
    if (!buf || !out) return 0;
    uint8_t lvl = 0;
    uint8_t flags = 0;
    if (is_wired) {
        size_t base = ((buf[0] == 0x06u || buf[0] == 0x09u) && len >= 10u) ? 1u : 0u;
        if (base + 9u > len) return 0;
        if (buf[base] != 0x87u) return 0;
        lvl = buf[base + 7u];
        flags = buf[base + 8u];
    } else {
        size_t base = (buf[0] == 0x13u && len >= 7u) ? 1u : 0u;
        if (base + 6u > len) return 0;
        if (buf[base] != 0x4Au || buf[base + 1u] < 1u || buf[base + 2u] != 0u) return 0;
        lvl = buf[base + 4u];
        flags = buf[base + 5u];
    }
    if (lvl == 0u || lvl > 100u) return 0;
    uint8_t full = (uint8_t)((flags >> 4) & 0x0Fu);
    uint8_t chg  = (uint8_t)(flags & 0x0Fu);
    out->battery_percent = (int8_t)lvl;
    out->is_estimated = 0;
    out->power_state = (chg == 1u) ? OCTANS_PWR_CHARGING
                     : ((full == 1u && lvl >= 100u) ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

void octans_proto_build_leobog_20b(uint8_t out20[20]) {
    if (!out20) return;
    memset(out20, 0, 20u);
    out20[0] = 0x13u;
    out20[1] = 0xCAu;
    out20[19] = (uint8_t)((0x13u + 0xCAu) & 0xFFu); /* 0xDD */
}

int octans_proto_parse_leobog_20b(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 20u) return 0;
    const uint8_t *d = (buf[0] == 0x00u && len >= 21u && buf[1] == 0x13u && buf[2] == 0x4Au) ? (buf + 1) : buf;
    size_t n = (d == buf) ? len : (len - 1u);
    if (n < 20u || d[0] != 0x13u || d[1] != 0x4Au || d[2] != 0x01u || d[3] != 0x00u || d[4] != 0x02u) {
        return 0;
    }
    uint32_t sum = 0;
    for (size_t i = 0; i < 19u; ++i) sum += d[i];
    if (d[19] != (uint8_t)(sum & 0xFFu)) return 0;
    if (d[5] > 100u) return 0;
    out->battery_percent = (int8_t)d[5];
    out->power_state = (d[6] == 0x10u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

void octans_proto_build_leobog_bytech(uint8_t report_id, uint8_t out64[64]) {
    if (!out64) return;
    memset(out64, 0, 64u);
    out64[0] = report_id;
    out64[1] = 0x87u; /* cmd 135 = getBatteryStatus */
    out64[2] = 0x00u; /* param */
    out64[3] = 0x00u;
    out64[4] = 0x01u; /* totalPackets = 1 */
    out64[5] = 0x00u; /* currentPacket = 0 */
    out64[6] = 0x00u; /* dataLen = 0 */
    uint32_t sum = report_id;
    for (size_t i = 1u; i < 63u; ++i) sum += out64[i];
    out64[63] = (uint8_t)(255u - (sum & 0xFFu));
}

int octans_proto_parse_leobog_bytech(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 8u) return 0;
    const uint8_t *d = buf;
    size_t n = len;
    if (buf[0] != 0x87u && len >= 9u && buf[1] == 0x87u) {
        d = buf + 1;
        n = len - 1u;
    }
    if (n < 8u || d[0] != 0x87u || d[1] != 0x00u || d[3] != 0x01u || d[4] != 0x00u) {
        return 0;
    }
    uint8_t lvl = d[6];
    uint8_t flags = d[7];
    if (lvl > 100u) return 0;
    int full = ((flags & 0x0Fu) != 0u);
    int chg = (((flags & 0xF0u) >> 4) != 0u);
    out->battery_percent = (int8_t)lvl;
    out->power_state = full ? OCTANS_PWR_FULL : (chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

void octans_proto_build_darmoshark_nordic(int wireless, uint8_t out64[64]) {
    if (!out64) return;
    memset(out64, 0, 64u);
    out64[0] = (uint8_t)(0x01u | (wireless ? 0x40u : 0x00u));
    out64[2] = 0x81u; /* DS_OP_BASE_INFO = 129 */
    out64[3] = 0x01u; /* DS_POWER_SUB = 1 */
    uint32_t sum = 0;
    for (size_t i = 0; i < 63u; ++i) sum += out64[i];
    out64[63] = (uint8_t)((0xA1u - (sum & 0xFFu)) & 0xFFu);
}

int octans_proto_parse_darmoshark_nordic(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 12u) return 0;
    const uint8_t *d = buf;
    size_t n = len;
    if (buf[0] == 0x00u && len >= 13u && ((buf[1] & 0x3Fu) == 0x01u) && buf[4] == 0x01u) {
        d = buf + 1;
        n = len - 1u;
    }
    if (n < 12u || (d[0] & 0x3Fu) != 0x01u || d[3] != 0x01u) return 0;

    int is_v2 = ((d[6] == 0x15u && d[7] == 0x19u) ||
                 (d[6] == 0x2Du && d[7] == 0x36u) ||
                 (d[6] == 0xDAu && d[7] == 0x0Bu) ||
                 (d[6] == 0x8Au && d[7] == 0x24u) ||
                 (d[6] > 100u && d[11] <= 100u));
    uint8_t pct = is_v2 ? d[11] : d[6];
    if (pct > 100u) return 0;
    if (pct == 0u && (!is_v2 || d[10] == 0u)) return 0;
    out->battery_percent = (int8_t)pct;
    if (is_v2) {
        uint8_t st = (d[10] == 3u) ? 0u : d[10];
        out->power_state = (st == 2u) ? OCTANS_PWR_FULL
                         : (st == 1u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

void octans_proto_build_darmoshark_telink(uint8_t opcode, uint8_t out21[21]) {
    if (!out21) return;
    memset(out21, 0, 21u);
    out21[0] = 0x51u;
    out21[1] = opcode;
}

int octans_proto_parse_darmoshark_telink(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 11u) return 0;
    const uint8_t *body = buf;
    size_t n = len;
    if (buf[0] == 0x51u && len >= 12u && buf[1] == 0x06u) {
        body = buf + 1;
        n = len - 1u;
    }
    if (n < 11u || body[0] != 0x06u || body[1] != 0x01u) return 0;
    uint8_t pct = body[10];
    if (pct == 0u || pct > 100u) return 0;
    out->battery_percent = (int8_t)pct;
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_mchose_audio_mc(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 9u || !out) return 0;
    size_t base = 0;
    if (buf[0] == 0x55u && len >= 10u && buf[1] == 0xAAu) base = 1u;
    else if (buf[0] == 0x00u && len >= 11u && buf[1] == 0x55u && buf[2] == 0xAAu) base = 2u;
    else if (buf[0] != 0xAAu) return 0;

    if (base + 9u > len) return 0;
    const uint8_t *d = buf + base;
    if (d[0] != 0xAAu || d[1] != 0x01u || d[5] != 0x01u) return 0;
    if (d[6] != 0x0Bu && d[6] != 0x22u) return 0;
    size_t body_len = d[3];
    if (body_len < 2u || base + 7u + body_len > len) return 0;

    uint8_t body0 = d[7];
    uint8_t body1 = d[8];
    if (body0 == 0u) {
        out->asleep = 1;
        out->power_state = OCTANS_PWR_SLEEPING;
        return 1;
    }
    uint8_t lvl = (uint8_t)(body1 & 0x7Fu);
    if (lvl == 0u || lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (body1 & 0x80u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_dareu_tg(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 10u || !out) return 0;
    const uint8_t *d = buf;
    size_t n = len;
    if (len == 65u || ((buf[0] & 0x0Fu) != 2u && len > 64u)) {
        d = buf + 1;
        n = len - 1u;
    }
    if (n < 10u || (d[0] & 0x0Fu) != 2u || d[7] > 100u) return 0;
    out->battery_percent = (int8_t)d[7];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_iqunix_texthex(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 4u || !out) return 0;
    char text[160];
    size_t copy = len < sizeof(text) - 1u ? len : sizeof(text) - 1u;
    for (size_t i = 0; i < copy; ++i) {
        char c = (char)buf[i];
        if (c == '\r' || c == '\n' || c == '\0') c = ' ';
        text[i] = c;
    }
    text[copy] = '\0';

    char *tok[16];
    int nt = 0;
    char *p = text;
    while (*p && nt < 16) {
        while (*p == ' ') ++p;
        if (!*p) break;
        tok[nt++] = p;
        while (*p && *p != ' ') ++p;
        if (*p) *p++ = '\0';
    }
    if (nt < 2) return 0;
    int i = 0;
    if (_stricmp(tok[0], "55") == 0) ++i;
    if (i >= nt || _stricmp(tok[i], "65") != 0) return 0;
    ++i;
    if (i >= nt) return 0;
    if (_stricmp(tok[i], "FE") == 0) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    if (_stricmp(tok[i], "FF") == 0) return 0;

    char *endp = NULL;
    long pct = strtol(tok[i], &endp, 16);
    if (endp == tok[i] || pct < 0 || pct > 100) return 0;
    out->battery_percent = (int8_t)pct;
    out->power_state = OCTANS_PWR_DISCHARGING;

    ++i;
    if (i < nt) {
        if (_stricmp(tok[i], "03") == 0) {
            out->power_state = OCTANS_PWR_CHARGING;
        } else if (_stricmp(tok[i], "04") == 0) {
            out->power_state = OCTANS_PWR_FULL;
            out->battery_percent = 100;
        }
    }
    out->valid = 1;
    return 1;
}

int octans_proto_parse_wob_rainy75v2(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 6u) return 0;
    const uint8_t *p = (buf[0] == 0x00u && len >= 7u && buf[1] == 0xBEu) ? (buf + 1) : buf;
    size_t n = (p == buf) ? len : (len - 1u);
    if (n < 6u || p[0] != 0xBEu || p[1] != 0xBBu) return 0;
    size_t flen = (p[2] == 0u || p[2] == 6u) ? 6u
                : (p[2] == 10u ? 10u : (p[2] == 14u ? 14u : 0u));
    if (flen == 0u || n < flen || p[flen - 1u] != 0xEDu || p[3] > 100u) return 0;
    out->battery_percent = (int8_t)p[3];
    out->power_state = (p[4] == 1u) ? OCTANS_PWR_FULL
                     : (p[4] == 2u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

int octans_proto_parse_wob_zen65(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 10u) return 0;
    const uint8_t *p = (buf[0] == 0x00u && len >= 11u && buf[1] == 0xAAu) ? (buf + 1) : buf;
    size_t n = (p == buf) ? len : (len - 1u);
    if (n < 10u || p[0] != 0xAAu || p[1] != 0x30u || p[2] != 0x00u ||
        p[3] != 0x00u || p[7] != 0x55u || p[8] > 100u) {
        return 0;
    }
    out->battery_percent = (int8_t)p[8];
    out->power_state = (p[9] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_rongyuan_f7(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 7u) return 0;
    const uint8_t *d = NULL;
    size_t n = 0;
    if (len >= 8u && buf[0] == 0x00u && buf[5] == 0x01u && buf[7] == 0x01u) {
        d = buf + 1;
        n = len - 1u;
    } else if (buf[4] == 0x01u && buf[6] == 0x01u) {
        d = buf;
        n = len;
    } else {
        return 0;
    }
    if (n < 7u || d[1] > 100u || d[3] > 1u) return 0;
    if (d[1] == 0u && d[3] == 0u) return 0; /* uninitialized dongle SRAM */
    out->battery_percent = (int8_t)d[1];
    out->power_state = (d[3] == 1u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_machenike_iton(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 3u) return 0;
    const uint8_t *p = (buf[0] == 0xA1u && len >= 4u) ? (buf + 1) : buf;
    size_t n = (p == buf) ? len : (len - 1u);
    if (n < 3u || p[0] != 0x02u || p[1] != 0x25u) return 0;
    uint8_t bat = p[2];
    if (bat == 0xBCu) {
        out->battery_percent = 100;
        out->is_estimated = 1;
        out->power_state = OCTANS_PWR_CHARGING;
        out->valid = 1;
        return 1;
    }
    if (bat == 0u || bat > 100u) return 0;
    out->battery_percent = (int8_t)bat;
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_incott(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 3u || buf[0] != 0x09u || buf[1] == 0u) return 0;
    if (buf[1] == 0x89u && buf[2] == 0x00u && (len < 4u || buf[3] == 0x00u)) return 0;
    uint8_t raw = buf[1];
    if (raw > 100u) {
        if (raw < 129u || raw > 228u) return 0;
        out->battery_percent = (int8_t)(raw - 128u);
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->battery_percent = (int8_t)raw;
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_proto_parse_beken_push(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 5u || buf[0] != 0x03u) return 0;
    if (buf[2] != 0x40u && buf[2] != 0x41u) return 0;
    uint8_t dev_id = buf[1];
    uint8_t sub = buf[3];
    uint8_t raw = buf[4];

    const wchar_t *known_name = NULL;
    switch (dev_id) {
        case 0x55u: known_name = L"Attack Shark X11"; break;
        case 0x10u: known_name = L"Attack Shark R1"; break;
        case 0x85u: known_name = L"Attack Shark X6"; break;
        case 0x4Du: known_name = L"Attack Shark X3"; break;
        case 0xBEu: known_name = L"Attack Shark X11 Pro"; break;
        case 0x07u: known_name = L"Attack Shark X11 SE"; break;
        default: break;
    }

    uint8_t pct = 0;
    if (dev_id == 0x85u && raw > 0u && raw <= 10u) {
        pct = (uint8_t)(raw * 10u);
        out->is_estimated = 1;
    } else if (raw > 0u && raw <= 100u) {
        pct = raw;
    } else {
        return 0;
    }

    int chg = (sub == 0x02u || sub == 0x03u || sub == 0x80u);
    if (known_name && len >= 8u && (buf[5] != 0u || buf[6] != 0u || buf[7] != 0u)) {
        chg = 1;
    }

    out->battery_percent = (int8_t)pct;
    out->power_state = (sub == 0x02u && pct >= 100u) ? OCTANS_PWR_FULL
                     : (chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    if (known_name && (!out->name[0] ||
        wstr_contains_nocase(out->name, L"Wireless Mouse") ||
        wstr_contains_nocase(out->name, L"2.4G"))) {
        wcsncpy(out->name, known_name, 63);
        out->name[63] = L'\0';
    }
    out->valid = 1;
    return 1;
}

int octans_proto_parse_bloody_8k(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 4u || buf[0] != 0x06u || buf[1] != 0x56u) return 0;
    if (buf[2] == 0xFFu) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    if (buf[2] > 100u) return 0;
    out->battery_percent = (int8_t)buf[2];
    out->power_state = (buf[3] == 1u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    if (buf[3] == 1u) out->conn = OCTANS_CONN_WIRED;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_lenovo_voip(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 8u || buf[0] != 0x27u || buf[1] != 0x01u) return 0;
    if (buf[2] != 0u) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    if (buf[7] > 100u) return 0;
    out->battery_percent = (int8_t)buf[7];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_sony_inzone_buds(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 20u || buf[0] != 0x02u || buf[1] != 0x12u || buf[2] != 0x04u) return 0;
    uint8_t right = buf[14];
    uint8_t left = buf[16];
    if (buf[19] != (uint8_t)(right + left + 117u)) return 0;
    if (right > 100u || left > 100u) return 0;
    out->battery_percent = (int8_t)(left < right ? left : right);
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_sony_pulse3d(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || !out || len < 5u || buf[0] != 0xB0u) return 0;
    if ((buf[4] & 0x08u) == 0u) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    if (buf[3] == 0x80u) {
        out->battery_percent = 100;
        out->is_estimated = 1;
        out->power_state = OCTANS_PWR_CHARGING;
        out->valid = 1;
        return 1;
    }
    if (buf[3] > 100u) return 0;
    out->battery_percent = (int8_t)buf[3];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

static uint8_t crc8_epos(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void octans_proto_build_epos_h3pro(int is_wired, uint8_t out107[107]) {
    if (!out107) return;
    memset(out107, 0, 107u);
    out107[0] = 0x01u;
    out107[1] = 0xEBu; out107[2] = 0xFEu;
    out107[3] = 0x01u; out107[4] = 0x00u;
    out107[5] = 0x04u; out107[6] = 0x00u;
    out107[7] = is_wired ? 0x01u : 0x05u; out107[8] = 0x00u;
    out107[9] = 0x04u; out107[10] = 0x00u;
    out107[13] = 0x12u; out107[14] = 0x00u;
    out107[15] = 0x03u; out107[16] = 0x00u;
    out107[11] = crc8_epos(out107 + 13, 4u);
    out107[12] = 0x00u;
}

int octans_proto_parse_epos_h3pro(const uint8_t *buf, size_t len, int is_wired, octans_proto_reading_t *out) {
    if (!buf || !out || len < 19u) return 0;
    if (buf[0] != 0x01u || buf[1] != 0xEBu || buf[2] != 0xFEu ||
        buf[5] != 0x04u || buf[6] != 0x00u) {
        return 0;
    }
    uint16_t target = (uint16_t)(buf[7] | ((uint16_t)buf[8] << 8));
    if (!is_wired && target == 0x0001u) {
        out->offline = 1;
        out->power_state = OCTANS_PWR_OFFLINE;
        return 1;
    }
    uint16_t expected_target = is_wired ? 0x0000u : 0x0004u;
    if (target != expected_target) return 0;
    uint16_t plen = (uint16_t)(buf[9] | ((uint16_t)buf[10] << 8));
    if (plen < 6u || buf[13] != 0x12u || buf[14] != 0x00u ||
        buf[15] != 0x03u || buf[16] != 0x00u || buf[17] > 100u) {
        return 0;
    }
    out->battery_percent = (int8_t)buf[17];
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_proto_parse_indep(const uint8_t *buf,
                             size_t len,
                             octans_proto_variant_t var,
                             octans_proto_reading_t *out) {
    if (!buf || !out) return 0;

    switch (var) {
    case VAR_INDEP_VAXEE:
        if (len < 6u || buf[5] > 20u) return 0;
        out->battery_percent = (int8_t)(buf[5] * 5u);
        out->is_estimated = 1;
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;

    case VAR_INDEP_NZXT: {
        if (len < 23u || buf[0] != 0x4Eu || buf[1] != 0x02u || buf[2] != 0x97u) return 0;
        uint16_t pct = (uint16_t)((uint16_t)buf[17] | ((uint16_t)buf[18] << 8));
        if (pct > 100u) return 0;
        out->battery_percent = (int8_t)pct;
        out->voltage_mv = (uint16_t)((uint16_t)buf[8] | ((uint16_t)buf[9] << 8));
        out->power_state = (buf[22] & 0x01u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_ALIENWARE:
        if (len < 4u || buf[0] != 0x0Eu || buf[1] != 0xA1u || buf[2] > 100u) return 0;
        out->battery_percent = (int8_t)buf[2];
        out->power_state = (buf[3] == 0x04u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;

    case VAR_INDEP_MCHOSE_G7: {
        size_t base = (buf[0] == 0x00u && len >= 11u && buf[1] == 0xAAu) ? 1u : 0u;
        if (len < base + 10u || buf[base] != 0xAAu || buf[base + 1u] != 0x30u) return 0;
        uint8_t lvl = buf[base + 8u];
        if (lvl == 0u || lvl > 100u) return 0;
        out->battery_percent = (int8_t)lvl;
        out->power_state = buf[base + 9u] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        if (buf[base + 9u]) out->conn = OCTANS_CONN_WIRED;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_AUDEZE:
        if (len < 7u || buf[0] != 0x05u || buf[1] != 0x5Du || buf[4] != 0xD6u || buf[5] != 0x0Cu || buf[6] > 100u) {
            return 0;
        }
        out->battery_percent = (int8_t)buf[6];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;

    case VAR_INDEP_JBL_Q910:
        if (len < 2u || buf[0] != 0x08u || buf[1] > 100u) return 0;
        out->battery_percent = (int8_t)buf[1];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;

    case VAR_INDEP_DUALSENSE: {
        if (len < 54u || buf[0] != 0x01u) return 0;
        uint8_t b = buf[53];
        uint8_t lvl = b & 0x0Fu;
        uint8_t st = (b >> 4) & 0x0Fu;
        int pct = (int)(lvl * 10u + 5u);
        if (pct > 100) pct = 100;
        out->battery_percent = (int8_t)pct;
        out->is_estimated = 1;
        out->power_state = (st == 0x02u) ? OCTANS_PWR_FULL
                          : (st == 0x01u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_8BITDO: {
        if (len < 15u || (buf[0] != 0x01u && buf[0] != 0x04u)) return 0;
        uint8_t raw = buf[14];
        int chg = (raw & 0x80u) != 0;
        uint8_t pct = raw & 0x7Fu;
        if (pct > 100u) return 0;
        out->battery_percent = (int8_t)pct;
        out->power_state = chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_SWITCH_PRO: {
        if (len < 3u || buf[0] != 0x30u) return 0;
        uint8_t hi = (buf[2] >> 4) & 0x0Fu;
        int chg = (hi & 0x01u) != 0;
        uint8_t step = (hi >> 1) & 0x07u;
        out->battery_percent = (int8_t)(step * 25u > 100u ? 100u : step * 25u);
        out->is_estimated = 1;
        out->power_state = chg ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_STEAM_2025:
        if (len < 3u || buf[0] != 0x43u || buf[2] > 100u) return 0;
        out->battery_percent = (int8_t)buf[2];
        out->power_state = (buf[1] == 4u) ? OCTANS_PWR_FULL
                          : (buf[1] == 2u ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
        out->valid = 1;
        return 1;

    case VAR_INDEP_NORDIC_GEN:
        if (len < 10u || buf[9] > 100u) return 0;
        if (len >= 13u && (buf[12] == 0u || buf[9] == 0u)) {
            out->asleep = 1;
            out->power_state = OCTANS_PWR_SLEEPING;
            return 1;
        }
        out->battery_percent = (int8_t)buf[9];
        if (len >= 12u && buf[11] != 0u) {
            out->power_state = OCTANS_PWR_FULL;
        } else if (len >= 11u && buf[10] != 0u) {
            out->power_state = OCTANS_PWR_CHARGING;
        } else {
            out->power_state = OCTANS_PWR_DISCHARGING;
        }
        out->valid = 1;
        return 1;

    case VAR_INDEP_MCHOSE_V9: {
        size_t base = 0;
        if (buf[0] == 0x55u && len >= 4u && buf[1] == 0x65u) base = 1u;
        else if (buf[0] == 0x00u && len >= 5u && buf[1] == 0x55u && buf[2] == 0x65u) base = 2u;
        else if (buf[0] == 0x65u && len >= 3u) base = 0u;
        else return 0;

        uint8_t lvl = buf[base + 1u];
        uint8_t st = buf[base + 2u];
        if (st == 26u) {
            out->asleep = 1;
            out->power_state = OCTANS_PWR_SLEEPING;
            return 1;
        }
        if (lvl == 0u || lvl > 100u) return 0;
        out->battery_percent = (int8_t)lvl;
        out->power_state = (st == 3u) ? OCTANS_PWR_CHARGING
                         : (st == 4u ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_MCHOSE_AUDIO_MC:
        return octans_proto_parse_mchose_audio_mc(buf, len, out);

    case VAR_INDEP_MCHOSE_G7MAX:
        return octans_proto_parse_mchose_g7max(buf, len, out);

    case VAR_INDEP_LOGI_ASTRO:
        if (len < 9u || buf[0] != 0x02u || buf[1] != 0x0Cu || buf[4] != 0x06u || buf[6] > 100u) return 0;
        out->battery_percent = (int8_t)buf[6];
        out->power_state = (buf[8] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;

    case VAR_INDEP_AULA_GEN64: {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] == 0x00u && len >= 6u && (buf[1] == 0x0Du || buf[5] <= 100u)) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 5u || d[4] == 0u || d[4] > 100u) return 0;
        out->battery_percent = (int8_t)d[4];
        out->power_state = (d[3] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_AULA_KBD_AA: {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] == 0x00u && len >= 28u && buf[1] == 0x55u) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 27u || d[0] != 0x55u || d[1] != 0x10u) return 0;
        const uint8_t *p = d + 8;
        if (p[17] > 100u) return 0;
        out->battery_percent = (int8_t)p[17];
        out->power_state = (p[18] != 0u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_AULA_24G_RX: {
        const uint8_t *d = buf;
        size_t n = len;
        if (buf[0] != 0x81u && len >= 19u && buf[1] == 0x81u) {
            d = buf + 1;
            n = len - 1u;
        }
        if (n < 18u || d[0] != 0x81u) return 0;
        const uint8_t *p = d + 3;
        if (p[14] > 100u) return 0;
        out->battery_percent = (int8_t)p[14];
        out->power_state = (p[13] == 1u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }

    case VAR_INDEP_DAREU_TG:
        return octans_proto_parse_dareu_tg(buf, len, out);

    case VAR_INDEP_IQUNIX_TEXTHEX:
        return octans_proto_parse_iqunix_texthex(buf, len, out);

    case VAR_INDEP_MACHENIKE_ITON:
        return octans_proto_parse_machenike_iton(buf, len, out);

    case VAR_INDEP_INCOTT_09:
        return octans_proto_parse_incott(buf, len, out);

    case VAR_INDEP_BEKEN_PUSH:
        return octans_proto_parse_beken_push(buf, len, out);

    case VAR_INDEP_BLOODY_8K:
        return octans_proto_parse_bloody_8k(buf, len, out);

    case VAR_INDEP_LENOVO_VOIP:
        return octans_proto_parse_lenovo_voip(buf, len, out);

    case VAR_INDEP_SONY_INZONE_BUDS:
        return octans_proto_parse_sony_inzone_buds(buf, len, out);

    case VAR_INDEP_SONY_PULSE3D:
        return octans_proto_parse_sony_pulse3d(buf, len, out);

    case VAR_INDEP_DARMOSHARK_NORDIC:
        return octans_proto_parse_darmoshark_nordic(buf, len, out);

    case VAR_INDEP_DARMOSHARK_TELINK:
        return octans_proto_parse_darmoshark_telink(buf, len, out);

    default:
        return 0;
    }
}

/* ---- Live interface matcher & runner ---- */

int octans_proto_matches_iface(const proto_entry_t *e, const octans_hid_info_t *info) {
    if (!e || !info) return 0;
    uint16_t vid = octans_dec16(e->vid, 1u);
    uint16_t pid = octans_dec16(e->pid, 2u);
    uint16_t up  = octans_dec16(e->usage_page, 3u);
    uint16_t u   = octans_dec16(e->usage, 4u);
    if (vid != 0xFFFFu && vid != info->vid) return 0;
    if (pid != 0xFFFFu && pid != info->pid) return 0;
    if (up != 0u && up != info->usage_page) return 0;
    if (u != 0u && u != info->usage) return 0;
    if (e->interface_num >= 0 && e->interface_num != info->interface_num) return 0;
    return 1;
}

static void init_reading_from_entry(const proto_entry_t *e,
                                    const octans_hid_info_t *info,
                                    octans_proto_reading_t *r) {
    octans_reading_init(r);
    r->vid = info->vid;
    r->pid = info->pid;
    r->kind = e->kind;
    r->conn = (e->flags & PROTO_F_WIRED) ? OCTANS_CONN_WIRED
            : (info->is_bluetooth ? OCTANS_CONN_BLUETOOTH : e->conn);
    wcsncpy(r->brand, e->brand, 31);
    r->brand[31] = L'\0';
    if (info->product[0]) {
        wcsncpy(r->name, info->product, 63);
        r->name[63] = L'\0';
    } else {
        wcsncpy(r->name, e->name, 63);
        r->name[63] = L'\0';
    }
    snprintf(r->stable_id, sizeof(r->stable_id), "%04x:%s", info->vid, info->group_key);
    snprintf(r->source_tag, sizeof(r->source_tag), "fam_%d_%d", (int)e->family, (int)e->variant);
}

static int run_razer90_live(const proto_entry_t *e, const octans_hid_info_t *info, octans_proto_reading_t *out) {
    octans_hid_dev_t dev;
    /* Razer feature report works on dwDesiredAccess=0 handles (needed for system-exclusive 0001:0002). */
    if (octans_hid_open(info->path, info->is_system_exclusive ? 0 : 1, 0, &dev) < 0) return 0;

    int ok = 0;
    uint8_t tx_list[3];
    int tx_cnt = 1;
    if (e->variant == VAR_RAZER90_KB_WIRELESS) {
        tx_list[0] = 0x9Fu;
    } else if (e->variant == VAR_RAZER90_KB_WIRED) {
        tx_list[0] = 0x3Fu;
    } else {
        /* Try modern 0x1F first, then 0x3F (Mamba/Lancehead/DA V2 Pro) and 0xFF (Viper Ult/Basilisk X) */
        tx_list[0] = 0x1Fu;
        tx_list[1] = 0x3Fu;
        tx_list[2] = 0xFFu;
        tx_cnt = 3;
    }

    uint8_t req[91];
    uint8_t resp[128];
    size_t flen = (dev.feature_len == 90u) ? 90u : (dev.feature_len >= 91u ? dev.feature_len : 91u);
    if (flen > sizeof(resp)) flen = sizeof(resp);
    int send_90 = (flen == 90u) ? 1 : 0;
    uint8_t used_tx = tx_list[0];

    static const DWORD wait_ms[4] = {20u, 45u, 85u, 140u};
    for (int ti = 0; ti < tx_cnt && !ok; ++ti) {
        uint8_t tx_id = tx_list[ti];
        octans_proto_build_razer90(tx_id, 0x80u, req);
        const uint8_t *send_ptr = send_90 ? (req + 1) : req;
        size_t send_len = send_90 ? 90u : 91u;
        if (octans_hid_set_feature(&dev, send_ptr, send_len) != 0) break;

        for (int attempt = 0; attempt < 4; ++attempt) {
            Sleep(wait_ms[attempt]);
            if (octans_hid_get_feature(&dev, 0x00u, resp, flen) < 0) continue;
            uint8_t st = 0, val = 0;
            if (!octans_proto_parse_razer90(resp, flen, 0x80u, &st, &val)) continue;
            if (st == 0x05u) break; /* Not supported on this tx_id */
            if (st == 0x01u) {
                if (attempt == 3 && val > 0u) {
                    /* OpenRazer / Synapse treats 0x01 busy with non-zero level as valid */
                    out->battery_percent = (int8_t)octans_clamp_percent(octans_round_div((uint32_t)val * 100u, 255u));
                    out->power_state = OCTANS_PWR_DISCHARGING;
                    out->valid = 1;
                    used_tx = tx_id;
                    ok = 1;
                    break;
                }
                continue;
            }
            if (st == 0x04u) {
                out->asleep = 1;
                out->power_state = OCTANS_PWR_SLEEPING;
                used_tx = tx_id;
                ok = 1;
                break;
            }
            if (st == 0x02u) {
                out->battery_percent = (int8_t)octans_clamp_percent(octans_round_div((uint32_t)val * 100u, 255u));
                out->power_state = OCTANS_PWR_DISCHARGING;
                out->valid = 1;
                used_tx = tx_id;
                ok = 1;
                break;
            }
        }
    }

    if (ok && out->valid) {
        octans_proto_build_razer90(used_tx, 0x84u, req);
        const uint8_t *send_ptr = send_90 ? (req + 1) : req;
        size_t send_len = send_90 ? 90u : 91u;
        if (octans_hid_set_feature(&dev, send_ptr, send_len) == 0) {
            Sleep(25);
            if (octans_hid_get_feature(&dev, 0x00u, resp, flen) >= 0) {
                uint8_t st = 0, val = 0;
                if (octans_proto_parse_razer90(resp, flen, 0x84u, &st, &val) &&
                    (st == 0x02u || st == 0x01u) && val != 0u) {
                    out->power_state = OCTANS_PWR_CHARGING;
                }
            }
        }
    }

    octans_hid_close(&dev);
    return ok;
}

static int run_passive_listen_live(const proto_entry_t *e,
                                   const octans_hid_info_t *info,
                                   uint32_t listen_ms,
                                   octans_proto_reading_t *out) {
    if (info->is_system_exclusive || info->input_len == 0) return 0;
    octans_hid_dev_t dev;
    if (octans_hid_open(info->path, 0, 1, &dev) < 0 || !dev.can_read) {
        octans_hid_close(&dev);
        return 0;
    }

    int ok = 0;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(listen_ms > 0 ? listen_ms : 250u);
    while (GetTickCount64() < deadline) {
        uint8_t buf[128];
        int n = octans_hid_read_input(&dev, buf, sizeof(buf), 80);
        if (n <= 0) continue;
        if (e->family == FAM_RAPOO_PUSH && octans_proto_parse_rapoo(buf, (size_t)n, e->variant, out)) {
            ok = 1;
            break;
        }
        if (e->family == FAM_INDEP && octans_proto_parse_indep(buf, (size_t)n, e->variant, out)) {
            ok = 1;
            break;
        }
    }

    octans_hid_close(&dev);
    return ok;
}

typedef int (*octans_parse_fn_t)(const uint8_t *buf, size_t len, void *ctx, octans_proto_reading_t *out);

static int send_and_read_loop(octans_hid_dev_t *dev,
                              int use_feature_write,
                              const uint8_t *req,
                              size_t req_len,
                              int attempts,
                              uint32_t read_timeout_ms,
                              octans_parse_fn_t parser,
                              void *ctx,
                              octans_proto_reading_t *out) {
    int rc = use_feature_write
           ? octans_hid_set_feature(dev, req, req_len)
           : octans_hid_write_output(dev, req, req_len);
    if (rc != 0) return 0;

    uint8_t resp[128];
    for (int i = 0; i < attempts; ++i) {
        int n = octans_hid_read_input(dev, resp, sizeof(resp), read_timeout_ms);
        if (n > 0 && parser(resp, (size_t)n, ctx, out)) return 1;
    }
    return 0;
}

static int wrap_parse_compx(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    return octans_proto_parse_compx(b, l, o);
}
static int wrap_parse_atk54(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    return octans_proto_parse_atk_54l15(b, l, o);
}
static int wrap_parse_atk52(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    return octans_proto_parse_atk_52840(b, l, o);
}
static int wrap_parse_eweadn(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    char dev_id[8] = {0};
    return octans_proto_parse_eweadn(b, l, o, dev_id);
}
static int wrap_parse_rog(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    return octans_proto_parse_rog(b, l, *(const octans_proto_variant_t *)c, o);
}
static int wrap_parse_ss(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    return octans_proto_parse_steelseries(b, l, *(const octans_proto_variant_t *)c, o);
}
static int wrap_parse_hx(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    return octans_proto_parse_hyperx(b, l, *(const octans_proto_variant_t *)c, o);
}
static int wrap_parse_aj10(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    return octans_proto_parse_ajazz_0x10(b, l, o);
}
static int wrap_parse_aj05(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    (void)c;
    return octans_proto_parse_ajazz_0x05(b, l, o);
}
static int wrap_parse_kb(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    return octans_proto_parse_keyboard(b, l, *(const octans_proto_variant_t *)c, 0u, o);
}
static int wrap_parse_indep(const uint8_t *b, size_t l, void *c, octans_proto_reading_t *o) {
    return octans_proto_parse_indep(b, l, *(const octans_proto_variant_t *)c, o);
}

static int run_razer_bsv2_live(const octans_hid_info_t *info, octans_proto_reading_t *out) {
    static const uint8_t enc_req_hdr[9] = {
        OCTANS_ENC8(0xFF, 30), OCTANS_ENC8(0x0A, 31), OCTANS_ENC8(0x00, 32),
        OCTANS_ENC8(0xFD, 33), OCTANS_ENC8(0x04, 34), OCTANS_ENC8(0x12, 35),
        OCTANS_ENC8(0xF1, 36), OCTANS_ENC8(0x02, 37), OCTANS_ENC8(0x05, 38)
    };
    octans_hid_dev_t dev;
    if (octans_hid_open(info->path, 1, 0, &dev) < 0 || dev.feature_len < 64) {
        octans_hid_close(&dev);
        return 0;
    }
    uint8_t req[64], resp[64];
    memset(req, 0, sizeof(req));
    octans_dec_bytes(req, enc_req_hdr, sizeof(enc_req_hdr), 30u);
    int ok = 0;
    if (octans_hid_set_feature(&dev, req, 64u) == 0) {
        Sleep(20);
        if (octans_hid_get_feature(&dev, 0xFFu, resp, 64u) == 64 &&
            octans_proto_parse_razer_bsv2_2020(resp, 64u, out)) {
            ok = 1;
        }
    }
    octans_hid_close(&dev);
    return ok;
}

static int run_razer_pa_live(const octans_hid_info_t *info, octans_proto_reading_t *out) {
    octans_hid_dev_t dev;
    if (octans_hid_open(info->path, 1, 1, &dev) < 0 || !dev.can_write) {
        octans_hid_close(&dev);
        return 0;
    }
    octans_hid_drain(&dev, 32);
    uint8_t req[64], resp[64];
    octans_proto_build_razer_pa(0xE1u, 0x02u, req); /* wake remote mode */
    octans_hid_write_output(&dev, req, 64u);
    Sleep(15);
    octans_proto_build_razer_pa(0x21u, 0x03u, req);
    int ok = 0;
    if (octans_hid_write_output(&dev, req, 64u) == 0) {
        for (int t = 0; t < 4; ++t) {
            int n = octans_hid_read_input(&dev, resp, sizeof(resp), 150);
            uint8_t val = 0;
            if (n > 0 && octans_proto_parse_razer_pa(resp, (size_t)n, 0x21u, &val) && val <= 100u) {
                out->battery_percent = (int8_t)val;
                out->power_state = OCTANS_PWR_DISCHARGING;
                out->valid = 1;
                ok = 1;
                break;
            }
        }
    }
    if (ok) {
        octans_proto_build_razer_pa(0x2Au, 0x03u, req);
        if (octans_hid_write_output(&dev, req, 64u) == 0) {
            for (int t = 0; t < 3; ++t) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 120);
                uint8_t val = 0;
                if (n > 0 && octans_proto_parse_razer_pa(resp, (size_t)n, 0x2Au, &val)) {
                    if (val) out->power_state = OCTANS_PWR_CHARGING;
                    break;
                }
            }
        }
    }
    octans_hid_close(&dev);
    return ok;
}

static int run_feature_or_output_live(const proto_entry_t *e,
                                      const octans_hid_info_t *info,
                                      int debug_log,
                                      octans_proto_reading_t *out) {
    if (info->is_system_exclusive) return 0;

    octans_hid_dev_t dev;
    int need_read = (e->tx != TX_FEATURE) || (e->variant == VAR_COMPX_B_FEATURE) ||
                    (e->variant == VAR_KB_KEYCHRON) || (e->variant == VAR_INDEP_ALIENWARE) ||
                    (e->variant == VAR_INDEP_LENOVO_VOIP);
    int need_write = (e->tx != TX_FEATURE);
    if (octans_hid_open(info->path, 1, need_read, &dev) < 0 || (need_write && !dev.can_write)) {
        octans_hid_close(&dev);
        return 0;
    }

    if (e->flags & PROTO_F_DRAIN_FIRST) {
        octans_hid_drain(&dev, 32);
    }

    int ok = 0;
    uint8_t req[128];
    uint8_t resp[128];
    memset(req, 0, sizeof(req));
    octans_proto_variant_t var = e->variant;

    if (e->family == FAM_SINOWEALTH && dev.feature_len >= 64) {
        size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 65u;
        octans_proto_build_sinowealth(0x00u, 0x83u, req);
        const uint8_t *sptr = (flen == 64u) ? (req + 1) : req;
        if (octans_hid_set_feature(&dev, sptr, flen) == 0) {
            Sleep(90);
            int n = octans_hid_get_feature(&dev, 0x00u, resp, flen);
            if (n >= 64 && octans_proto_parse_sinowealth(resp, (size_t)n, out)) {
                ok = 1;
            }
        }
    } else if (e->family == FAM_COMPX &&
               ((e->tx == TX_FEATURE && dev.feature_len >= 17u) ||
                (e->tx != TX_FEATURE && dev.output_len >= 17u && dev.output_len != 20u))) {
        octans_proto_build_compx(0x04u, req);
        if (e->tx == TX_FEATURE) {
            size_t flen = dev.feature_len <= sizeof(req) ? dev.feature_len : 17u;
            ok = send_and_read_loop(&dev, 1, req, flen, 4, 120u, wrap_parse_compx, NULL, out);
        } else if (e->tx == TX_OUTPUT_INTR) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 17u;
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 150u);
                    if (n >= 17 && octans_proto_parse_compx(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 17u;
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_compx, NULL, out);
        }
    } else if (e->family == FAM_ATK_NORDIC) {
        if (e->variant == VAR_ATK_54L15_64B && dev.output_len >= 64) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            octans_proto_build_atk_54l15(req);
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_atk54, NULL, out);
        } else if (e->variant == VAR_ATK_52840_17B && dev.output_len >= 17) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 17u;
            octans_proto_build_atk_52840(req);
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_atk52, NULL, out);
        } else if (e->variant == VAR_ATK_MOUSEINFO && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t rids[3] = {11u, 8u, 6u};
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            for (int ri = 0; ri < 3 && !ok; ++ri) {
                memset(req, 0, olen);
                req[0] = rids[ri];
                req[1] = 0x13u; /* cmd 19 = MouseInfo */
                if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                    octans_hid_write_output(&dev, req, olen) == 0) {
                    for (int a = 0; a < 4; ++a) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 160u);
                        if (n >= 20 && octans_proto_parse_atk_mouseinfo(resp, (size_t)n, out)) { ok = 1; break; }
                    }
                }
            }
        }
    } else if (e->family == FAM_ASUS && dev.output_len >= 64) {
        size_t rlen = 64u;
        octans_proto_build_rog_1207(octans_dec8(e->report_id, 5u), req, &rlen);
        if (var == VAR_ROG_OMNI_KB) req[2] = 0x01u;
        if (rlen > dev.output_len) rlen = dev.output_len;
        ok = send_and_read_loop(&dev, 0, req, rlen, 4, 120u, wrap_parse_rog, &var, out);
    } else if (e->family == FAM_CORSAIR && e->variant == VAR_CORSAIR_BRAGI && dev.output_len >= 64) {
        size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
        size_t off = (olen >= 65u) ? 1u : 0u;
        memset(req, 0, olen);
        octans_proto_build_corsair_bragi(0x09u, 0x0Fu, req + off);
        if (octans_hid_write_output(&dev, req, olen) == 0 ||
            octans_hid_write_output_intr(&dev, req, olen) == 0) {
            for (int t = 0; t < 4; ++t) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 120);
                uint16_t raw = 0;
                if (n > 0 && octans_proto_parse_corsair_bragi(resp, (size_t)n, &raw)) {
                    out->battery_percent = (int8_t)octans_clamp_percent(octans_round_div(raw, 10u));
                    out->power_state = OCTANS_PWR_DISCHARGING;
                    out->valid = 1;
                    ok = 1;
                    break;
                }
            }
        }
    } else if (e->family == FAM_CORSAIR && e->variant == VAR_CORSAIR_VOID_ELITE && dev.output_len >= 2u && dev.can_read) {
        size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
        memset(req, 0, olen);
        req[0] = 0xC9u;
        req[1] = 0x64u;
        int wrc = octans_hid_write_output(&dev, req, olen);
        if (wrc != 0) wrc = octans_hid_write_output_intr(&dev, req, olen);
        if (wrc != 0 && olen >= 3u) {
            memset(req, 0, olen);
            req[0] = 0x00u;
            req[1] = 0xC9u;
            req[2] = 0x64u;
            wrc = octans_hid_write_output(&dev, req, olen);
            if (wrc != 0) wrc = octans_hid_write_output_intr(&dev, req, olen);
        }
        if (wrc == 0) {
            for (int t = 0; t < 4; ++t) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                if (n >= 5 && octans_proto_parse_corsair_void_elite(resp, (size_t)n, out)) { ok = 1; break; }
            }
        }
    } else if (e->family == FAM_CORSAIR && e->variant == VAR_CORSAIR_LEGACY_NXP && dev.output_len >= 64u && dev.can_read) {
        size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
        memset(req, 0, olen);
        req[0] = 0x0Eu;
        req[1] = 0x50u;
        if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
            octans_hid_write_output(&dev, req, olen) == 0) {
            for (int t = 0; t < 3; ++t) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                if (n >= 7 && octans_proto_parse_corsair_legacy_nxp(resp, (size_t)n, out)) { ok = 1; break; }
            }
        }
    } else if (e->family == FAM_STEELSERIES && e->variant == VAR_SS_ARCTIS1 && dev.output_len >= 8u) {
        size_t rlen = dev.output_len <= sizeof(req) ? dev.output_len : 31u;
        req[0] = 0x06u; req[1] = 0x12u;
        ok = send_and_read_loop(&dev, 0, req, rlen, 4, 150u, wrap_parse_ss, &var, out);
    } else if (e->family == FAM_STEELSERIES && dev.output_len >= 64) {
        size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
        if (e->variant == VAR_SS_NOVA_ELITE) {
            req[0] = 0x01u;
            req[1] = 0xB0u;
        } else {
            req[0] = (e->variant == VAR_SS_NOVA_PRO) ? 0x06u : 0x00u;
            req[1] = (e->variant == VAR_SS_RIVAL3) ? 0xAAu : (e->variant == VAR_SS_AEROX ? 0xD2u : 0xB0u);
            if (e->variant == VAR_SS_RIVAL3) req[2] = 0x01u;
        }
        ok = send_and_read_loop(&dev, 0, req, olen, 4, 150u, wrap_parse_ss, &var, out);
    } else if (e->family == FAM_HYPERX) {
        if (e->variant == VAR_HX_FLIGHT && dev.output_len >= 20u) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 20u;
            req[0] = 0x21u; req[1] = 0xFFu; req[2] = 0x05u;
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 200u, wrap_parse_hx, &var, out);
        } else if (e->variant == VAR_HX_ALPHA_WIRELESS && dev.output_len >= 31) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 31u;
            req[0] = 0x21u; req[1] = 0xBBu; req[2] = 0x0Bu;
            if (send_and_read_loop(&dev, 0, req, olen, 5, 200u, wrap_parse_hx, &var, out)) {
                ok = 1;
                req[2] = 0x0Cu;
                if (octans_hid_write_output(&dev, req, olen) == 0) {
                    int cn = octans_hid_read_input(&dev, resp, sizeof(resp), 200);
                    if (cn > 0) octans_proto_parse_hyperx(resp, (size_t)cn, var, out);
                }
            }
        } else if (e->variant == VAR_HX_CLOUD3S && dev.output_len >= 64u) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            req[0] = 0x0Cu; req[1] = 0x02u; req[2] = 0x03u; req[3] = 0x01u; req[4] = 0x00u; req[5] = 0x06u;
            if (send_and_read_loop(&dev, 0, req, olen, 4, 150u, wrap_parse_hx, &var, out)) {
                ok = 1;
                req[5] = 0x48u;
                if (octans_hid_write_output(&dev, req, olen) == 0) {
                    int cn = octans_hid_read_input(&dev, resp, sizeof(resp), 150u);
                    if (cn > 0) octans_proto_parse_hyperx(resp, (size_t)cn, var, out);
                }
            }
        } else if (e->variant == VAR_HX_AIROHA && dev.output_len >= 8u && dev.can_read) {
            static const uint8_t enc_tws[7] = {
                OCTANS_ENC8(0x05, 80), OCTANS_ENC8(0x5A, 81), OCTANS_ENC8(0x03, 82),
                OCTANS_ENC8(0x00, 83), OCTANS_ENC8(0xD6, 84), OCTANS_ENC8(0x0C, 85),
                OCTANS_ENC8(0x00, 86)
            };
            static const uint8_t enc_ms[6] = {
                OCTANS_ENC8(0x05, 88), OCTANS_ENC8(0x5A, 89), OCTANS_ENC8(0x02, 90),
                OCTANS_ENC8(0x00, 91), OCTANS_ENC8(0xCF, 92), OCTANS_ENC8(0x30, 93)
            };
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            size_t off = (olen >= 65u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_tws, sizeof(enc_tws), 80u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int t = 0; t < 4; ++t) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                    if (n >= 9 && octans_proto_parse_hyperx(resp, (size_t)n, var, out)) { ok = 1; break; }
                }
            }
            if (!ok) {
                memset(req, 0, olen);
                octans_dec_bytes(req + off, enc_ms, sizeof(enc_ms), 88u);
                if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                    octans_hid_write_output(&dev, req, olen) == 0) {
                    for (int t = 0; t < 4; ++t) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                        if (n >= 9 && octans_proto_parse_hyperx(resp, (size_t)n, var, out)) { ok = 1; break; }
                    }
                }
            }
        } else if (dev.output_len >= 52) {
            size_t rlen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            if (e->variant == VAR_HX_GEN1) { req[0]=0x06; req[1]=0xFF; req[2]=0xBB; req[3]=0x02; }
            else if (e->variant == VAR_HX_GEN2) { req[0]=0x66; req[1]=0x89; }
            else { req[0]=0x50; req[1]=0x02; }
            ok = send_and_read_loop(&dev, 0, req, rlen, 4, 120u, wrap_parse_hx, &var, out);
        }
    } else if (e->family == FAM_RAPOO_PUSH && dev.can_read) {
        if (e->variant == VAR_RAPOO_VISION_AP && dev.output_len >= 64u) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            octans_proto_build_rapoo_vision(req);
            static const uint32_t tms[2] = { 300u, 120u };
            for (int a = 0; a < 2 && !ok; ++a) {
                if (octans_hid_write_output_intr(&dev, req, olen) != 0 &&
                    octans_hid_write_output(&dev, req, olen) != 0) break;
                for (int r = 0; r < 3; ++r) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), tms[a]);
                    if (n >= 9 && octans_proto_parse_rapoo(resp, (size_t)n, var, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_RAPOO_CH315 && dev.output_len >= 3u) {
            static const uint8_t rids[4] = { 0u, 1u, 2u, 4u };
            static const uint8_t sels[2] = { 1u, 2u };
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            for (int si = 0; si < 2 && !ok; ++si) {
                for (int ri = 0; ri < 4 && !ok; ++ri) {
                    memset(req, 0, olen);
                    req[0] = rids[ri];
                    req[1] = 0xAAu;
                    req[2] = sels[si];
                    if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                        octans_hid_write_output(&dev, req, olen) == 0) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 160u);
                        if (n >= 5 && octans_proto_parse_rapoo(resp, (size_t)n, var, out)) { ok = 1; break; }
                    }
                }
            }
        }
    } else if (e->family == FAM_AJAZZ) {
        if (e->variant == VAR_AJAZZ_MKBMS && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t enc_mkbms[3] = { OCTANS_ENC8(0x55, 96), OCTANS_ENC8(0xAA, 97), OCTANS_ENC8(0x80, 98) };
            size_t olen = dev.output_len >= 65u ? 65u : 64u;
            size_t off = (olen >= 65u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_mkbms, sizeof(enc_mkbms), 96u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                    if (n >= 40 && octans_proto_parse_ajazz_mkbms(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_EWEADN_0X30 && dev.output_len >= 32u) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 33u;
            uint8_t cmds[2];
            if (info->vid == 0x249Au || info->vid == 0x248Au) {
                cmds[0] = 0x10u; cmds[1] = 0x30u;
            } else {
                cmds[0] = 0x30u; cmds[1] = 0x10u;
            }
            for (int ci = 0; ci < 2 && !ok; ++ci) {
                octans_proto_build_eweadn(cmds[ci], req);
                const uint8_t *sptr = (olen == 32u) ? (req + 1) : req;
                ok = send_and_read_loop(&dev, 0, sptr, olen, 4, 140u, wrap_parse_eweadn, NULL, out);
            }
        } else if (e->variant == VAR_AJAZZ_0X10 && dev.output_len >= 33) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 33u;
            req[0] = 0x10u; req[2] = 0x01u; req[3] = 0x0Bu;
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_aj10, NULL, out);
        } else if (e->variant == VAR_AJAZZ_0X05 && dev.output_len >= 64) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            req[0] = 0xF7u;
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_aj05, NULL, out);
        }
    } else if (e->family == FAM_JIELI_TLV && dev.output_len >= 64) {
        size_t rlen = dev.output_len >= 65 ? 65u : 64u;
        octans_proto_build_jieli_query(1u, 0x11u, 0x01u, req);
        if (octans_hid_write_output(&dev, req, rlen) == 0) {
            for (int t = 0; t < 4; ++t) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 120);
                uint8_t pct = 0;
                if (n >= 14 && octans_proto_parse_jieli_tlv(resp, (size_t)n, 1u, &pct)) {
                    out->battery_percent = (int8_t)pct;
                    out->power_state = OCTANS_PWR_DISCHARGING;
                    out->valid = 1;
                    ok = 1;
                    break;
                }
            }
        }
    } else if (e->family == FAM_KEYBOARD) {
        if (e->variant == VAR_KB_SEMICO_AA55 && dev.output_len >= 64) {
            int with_rid = (dev.output_len >= 65) ? 1 : 0;
            size_t rlen = with_rid ? 65u : 64u;
            octans_proto_build_semico_aa55(0x0Eu, with_rid, req, rlen);
            if (octans_hid_write_output(&dev, req, rlen) == 0) {
                for (int t = 0; t < 4; ++t) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 150);
                    if (n > 0) {
                        if (octans_proto_parse_semico_aa55(resp, (size_t)n, out)) {
                            ok = 1;
                            break;
                        }
                        if (debug_log) {
                            fprintf(stderr, "[octans] semico raw (%dB):", n);
                            for (int b = 0; b < n && b < 24; ++b) {
                                fprintf(stderr, " %02X", resp[b]);
                            }
                            fprintf(stderr, "\n");
                        }
                    }
                }
            }
        } else if ((e->variant == VAR_KB_ATK_EVISION || e->variant == VAR_KB_LOFREE_HYZEN) && dev.output_len >= 32) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 32u;
            req[0] = 0x04u; req[1] = 0x01u; req[5] = 0x1Au;
            ok = send_and_read_loop(&dev, 0, req, olen, 4, 120u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_KEYCHRON && dev.feature_len >= 64) {
            size_t flen = dev.feature_len <= sizeof(req) ? dev.feature_len : 64u;
            req[0] = 0xB3u; req[1] = 0x06u;
            ok = send_and_read_loop(&dev, 1, req, flen, 3, 120u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_VIA_RAW && dev.output_len >= 32) {
            size_t len = dev.output_len >= 33 ? 33u : 32u;
            octans_proto_build_via_battery(req);
            ok = send_and_read_loop(&dev, 0, req, len, 2, 150u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_ATK_HFD && dev.output_len >= 32u && dev.can_read) {
            static const uint8_t enc_hfd[3] = { OCTANS_ENC8(0x02, 100), OCTANS_ENC8(0x96, 101), OCTANS_ENC8(0x24, 102) };
            size_t olen = dev.output_len >= 33u ? 33u : 32u;
            size_t off = (olen >= 33u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_hfd, sizeof(enc_hfd), 100u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 160u);
                    if (n >= 5 && octans_proto_parse_keyboard(resp, (size_t)n, var, 0u, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_KB_DURGOD_3123 && dev.feature_len >= 8u) {
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 9u;
            memset(req, 0, flen);
            size_t off = (flen >= 9u) ? 1u : 0u;
            req[off] = octans_dec8(OCTANS_ENC8(0x82u, 105), 105u);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(40);
                int n = octans_hid_get_feature(&dev, 0x00u, resp, flen);
                if (n >= 4 && octans_proto_parse_keyboard(resp, (size_t)n, var, 0u, out)) { ok = 1; }
            }
        } else if (e->variant == VAR_KB_MCHOSE_BY &&
                   (is_mchose_by_exact_pid(info->vid, info->pid) ||
                    is_mchose_by_kb_name(info->product) ||
                    ((info->vid == 0x3837u || info->vid == 0x41E4u) &&
                     (wstr_contains_nocase(info->product, L"MCHOSE 2.4G Wireless") ||
                      wstr_contains_nocase(info->product, L"2.4G Wireless Receiver"))) ||
                    (info->vid == 0x3837u && dev.output_len == 20u && dev.input_len == 20u))) {
            if (dev.feature_len >= 519u) {
                uint8_t by_req[520];
                uint8_t by_resp[600];
                static const uint8_t rids[2] = {0x09u, 0x06u};
                for (int ri = 0; ri < 2 && !ok; ++ri) {
                    size_t flen = (dev.feature_len <= sizeof(by_req)) ? dev.feature_len : 520u;
                    memset(by_req, 0, flen);
                    by_req[0] = rids[ri];
                    by_req[1] = 0x87u; by_req[4] = 0x01u; by_req[6] = 0x02u;
                    if (octans_hid_set_feature(&dev, by_req, flen) == 0) {
                        Sleep(60);
                        int n = octans_hid_get_feature(&dev, rids[ri], by_resp, flen);
                        if (n >= 9 && octans_proto_parse_mchose_by(by_resp, (size_t)n, 1, out)) {
                            out->conn = OCTANS_CONN_WIRED;
                            ok = 1;
                        }
                    }
                }
            } else if (dev.output_len >= 20u && dev.output_len <= 32u && dev.can_read) {
                octans_proto_build_mchose_by_wireless(req);
                if (octans_hid_write_output_intr(&dev, req, 20u) == 0 ||
                    octans_hid_write_output(&dev, req, 20u) == 0) {
                    for (int a = 0; a < 6; ++a) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 250u);
                        if (n >= 6 && octans_proto_parse_mchose_by(resp, (size_t)n, 0, out)) {
                            ok = 1;
                            break;
                        }
                    }
                }
            }
        } else if (e->variant == VAR_KB_WOB_RAINY75V2 && dev.output_len >= 6u && dev.can_read) {
            static const uint8_t enc_r75[4] = {
                OCTANS_ENC8(0xBE, 140), OCTANS_ENC8(0xBB, 141),
                OCTANS_ENC8(0x04, 142), OCTANS_ENC8(0xED, 143)
            };
            uint8_t wreq[260], wresp[260];
            size_t olen = (dev.output_len <= sizeof(wreq)) ? dev.output_len : 257u;
            memset(wreq, 0, olen);
            wreq[0] = 0x00u;
            octans_dec_bytes(wreq + 1, enc_r75, sizeof(enc_r75), 140u);
            if (octans_hid_write_output_intr(&dev, wreq, olen) == 0 ||
                octans_hid_write_output(&dev, wreq, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, wresp, sizeof(wresp), 200u);
                    if (n >= 6 && octans_proto_parse_wob_rainy75v2(wresp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_KB_WOB_ZEN65 && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t enc_z65[5] = {
                OCTANS_ENC8(0xAA, 145), OCTANS_ENC8(0x30, 146),
                OCTANS_ENC8(0x00, 147), OCTANS_ENC8(0x00, 148),
                OCTANS_ENC8(0x18, 149)
            };
            size_t olen = dev.output_len >= 65u ? 65u : 64u;
            size_t off = (olen >= 65u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_z65, sizeof(enc_z65), 145u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                    if (n >= 10 && octans_proto_parse_wob_zen65(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_KB_RONGYUAN_F7 && dev.feature_len >= 64u) {
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 65u;
            size_t off = (flen >= 65u) ? 1u : 0u;
            memset(req, 0, flen);
            req[off] = octans_dec8(OCTANS_ENC8(0xF7u, 130), 130u);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(35);
                int n = octans_hid_get_feature(&dev, 0x00u, resp, flen);
                if (n >= 7 && octans_proto_parse_rongyuan_f7(resp, (size_t)n, out)) { ok = 1; }
            }
        } else if (e->variant == VAR_KB_LEOBOG_20B && dev.output_len == 20u && dev.input_len == 20u && dev.can_read) {
            octans_proto_build_leobog_20b(req);
            if (octans_hid_write_output_intr(&dev, req, 20u) == 0 ||
                octans_hid_write_output(&dev, req, 20u) == 0) {
                for (int a = 0; a < 6; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                    if (n >= 20 && octans_proto_parse_leobog_20b(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_KB_LEOBOG_BYTECH && dev.output_len >= 63u && dev.can_read) {
            uint8_t rids[8];
            size_t rcnt = octans_hid_collect_output_rids(&dev, rids, 8u);
            size_t olen = (dev.output_len <= sizeof(req)) ? dev.output_len : 64u;
            for (size_t ri = 0; ri < rcnt && !ok; ++ri) {
                octans_proto_build_leobog_bytech(rids[ri], req);
                const uint8_t *sptr = (olen == 63u) ? (req + 1) : req;
                if (octans_hid_write_output_intr(&dev, sptr, olen) == 0 ||
                    octans_hid_write_output(&dev, sptr, olen) == 0) {
                    for (int a = 0; a < 4; ++a) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                        if (n >= 8 && octans_proto_parse_leobog_bytech(resp, (size_t)n, out)) { ok = 1; break; }
                    }
                }
            }
        }
    } else if (e->family == FAM_INDEP) {
        /* Skip MCHOSE wired-only OY headsets (G20 / G20 PRO / K20 / K20 PRO) */
        if ((info->vid == 0x0E6Au && info->pid == 0x0111u) ||
            (info->vid == 0x3837u && (info->pid == 0x60C4u || info->pid == 0x6003u))) {
            octans_hid_close(&dev);
            return 0;
        }
        if (e->variant == VAR_INDEP_VAXEE && dev.feature_len >= 64) {
            req[0] = 0x0Eu; req[1] = 0xA5u; req[2] = 0x0Bu; req[3] = 0x01u; req[4] = 0x01u;
            if (octans_hid_set_feature(&dev, req, dev.feature_len) == 0) {
                Sleep(300);
                if (octans_hid_get_feature(&dev, 0x0Eu, resp, dev.feature_len) >= 6 &&
                    octans_proto_parse_indep(resp, dev.feature_len, var, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_MCHOSE_HID4D && dev.output_len >= 61u && dev.can_read) {
            if (is_mchose_cz_kb_name(info->product)) {
                out->kind = OCTANS_DEV_KEYBOARD;
            }
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            /* Primary: cmd 0x0900 (deviceInfo), up to 5 attempts with 30ms busy retry */
            for (int a = 0; a < 5 && !ok; ++a) {
                octans_proto_build_mchose_hid4d(0x4Du, 0x0900u, req, olen);
                if (octans_hid_write_output_intr(&dev, req, olen) != 0 &&
                    octans_hid_write_output(&dev, req, olen) != 0) break;
                int busy = 0;
                for (int r = 0; r < 3; ++r) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                    if (n <= 0) break;
                    if (resp[0] == 0xFFu || (n >= 2 && resp[0] == 0x4Du && resp[1] == 0xFFu)) {
                        busy = 1;
                        break;
                    }
                    if (octans_proto_parse_mchose_hid4d(resp, (size_t)n, info->vid, out)) {
                        ok = 1;
                        break;
                    }
                }
                if (!ok && busy) Sleep(30);
            }
            /* Fallback: cmd 0x0904 (CZ keyboard dedicated battery command) */
            for (int a = 0; a < 3 && !ok; ++a) {
                octans_proto_build_mchose_hid4d(0x4Du, 0x0904u, req, olen);
                if (octans_hid_write_output_intr(&dev, req, olen) != 0 &&
                    octans_hid_write_output(&dev, req, olen) != 0) break;
                for (int r = 0; r < 2; ++r) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                    if (n <= 0) break;
                    if (resp[0] == 0xFFu || (n >= 2 && resp[0] == 0x4Du && resp[1] == 0xFFu)) {
                        Sleep(30);
                        break;
                    }
                    if (octans_proto_parse_mchose_hid4d_batt(resp, (size_t)n, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_MCHOSE_M7 && dev.feature_len >= 20u) {
            /* F2 MC Feature: always Report ID 0x11 (inverted frame), retry <= 4 */
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 21u;
            if (flen < 21u) flen = dev.feature_len;
            octans_proto_build_mchose_m7(0x11u, 0x06u, req, flen);
            for (int a = 0; a < 4 && !ok; ++a) {
                if (octans_hid_set_feature(&dev, req, flen) != 0) break;
                Sleep(35);
                int n = octans_hid_get_feature(&dev, 0x11u, resp, flen);
                if (n >= 13 && octans_proto_parse_mchose_m7(resp, (size_t)n, 0x06u, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_MCHOSE_G7MAX && dev.feature_len >= 63u &&
                   (wstr_contains_nocase(info->product, L"G7 MAX") ||
                    wstr_contains_nocase(info->product, L"G7_MAX") ||
                    wstr_contains_nocase(info->product, L"G7MAX"))) {
            size_t flen = dev.feature_len <= sizeof(req) ? dev.feature_len : 64u;
            octans_proto_build_mchose_g7max(0x03u, req);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(50);
                int n = octans_hid_get_feature(&dev, 0x03u, resp, flen);
                if (n >= 8 && octans_proto_parse_mchose_g7max(resp, (size_t)n, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_NORDIC_GEN && dev.feature_len >= 32) {
            size_t flen = dev.feature_len <= sizeof(req) ? dev.feature_len : 32u;
            req[0] = 0x05u; req[1] = 0x15u; req[4] = 0x01u;
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(80);
                int n = octans_hid_get_feature(&dev, 0x05u, resp, flen);
                if (n >= 10 && octans_proto_parse_indep(resp, (size_t)n, var, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_NZXT && dev.output_len >= 64) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            req[0] = 0x4Eu; req[1] = 0x02u; req[2] = 0x81u; req[4] = 0xB0u;
            ok = send_and_read_loop(&dev, 0, req, olen, 3, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_ALIENWARE && dev.feature_len >= 65) {
            size_t flen = dev.feature_len <= sizeof(req) ? dev.feature_len : 65u;
            req[0] = 0x00u; req[1] = 0x0Eu; req[2] = 0xA1u;
            ok = send_and_read_loop(&dev, 1, req, flen, 4, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_MCHOSE_G7 && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t enc_g7[9] = {
                OCTANS_ENC8(0x00, 40), OCTANS_ENC8(0x55, 41), OCTANS_ENC8(0x30, 42),
                OCTANS_ENC8(0xA5, 43), OCTANS_ENC8(0x0B, 44), OCTANS_ENC8(0x2E, 45),
                OCTANS_ENC8(0x01, 46), OCTANS_ENC8(0x01, 47), OCTANS_ENC8(0x01, 48)
            };
            size_t olen = dev.output_len >= 65u ? 65u : 64u;
            octans_dec_bytes(req, enc_g7, sizeof(enc_g7), 40u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 5; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                    if (n >= 10 && octans_proto_parse_indep(resp, (size_t)n, var, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_AUDEZE && dev.output_len >= 64) {
            static const uint8_t enc_aud[6] = {
                OCTANS_ENC8(0x05, 50), OCTANS_ENC8(0x5D, 51), OCTANS_ENC8(0x04, 52),
                OCTANS_ENC8(0x00, 53), OCTANS_ENC8(0xD6, 54), OCTANS_ENC8(0x0C, 55)
            };
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            octans_dec_bytes(req, enc_aud, sizeof(enc_aud), 50u);
            ok = send_and_read_loop(&dev, 0, req, olen, 3, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_MCHOSE_V9 && dev.output_len >= 64u && dev.can_read) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            req[0] = octans_dec8(OCTANS_ENC8(0x55u, 58), 58u);
            req[1] = octans_dec8(OCTANS_ENC8(0x65u, 59), 59u);
            req[2] = 0x01u;
            int wrc = octans_hid_write_output_intr(&dev, req, olen);
            if (wrc != 0) wrc = octans_hid_write_output(&dev, req, olen);
            if (wrc != 0 && olen >= 65u) {
                memset(req, 0, olen);
                req[0] = 0x00u;
                req[1] = octans_dec8(OCTANS_ENC8(0x55u, 58), 58u);
                req[2] = octans_dec8(OCTANS_ENC8(0x65u, 59), 59u);
                req[3] = 0x01u;
                wrc = octans_hid_write_output_intr(&dev, req, olen);
                if (wrc != 0) wrc = octans_hid_write_output(&dev, req, olen);
            }
            if (wrc == 0) {
                for (int a = 0; a < 5; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 260u);
                    if (n >= 3 && octans_proto_parse_indep(resp, (size_t)n, var, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_MCHOSE_AUDIO_MC && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t enc_mc[9] = {
                OCTANS_ENC8(0x55, 60), OCTANS_ENC8(0xAA, 61), OCTANS_ENC8(0x01, 62),
                OCTANS_ENC8(0x00, 63), OCTANS_ENC8(0x01, 64), OCTANS_ENC8(0x00, 65),
                OCTANS_ENC8(0x01, 66), OCTANS_ENC8(0x0B, 67), OCTANS_ENC8(0x00, 68)
            };
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            octans_dec_bytes(req, enc_mc, sizeof(enc_mc), 60u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 5; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 260u);
                    if (n >= 9 && octans_proto_parse_mchose_audio_mc(resp, (size_t)n, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_LOGI_ASTRO && dev.output_len >= 64) {
            static const uint8_t enc_astro[6] = {
                OCTANS_ENC8(0x02, 70), OCTANS_ENC8(0x0C, 71), OCTANS_ENC8(0x03, 72),
                OCTANS_ENC8(0x00, 73), OCTANS_ENC8(0x06, 74), OCTANS_ENC8(0x0C, 75)
            };
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            octans_dec_bytes(req, enc_astro, sizeof(enc_astro), 70u);
            ok = send_and_read_loop(&dev, 0, req, olen, 6, 200u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_AULA_GEN64 && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t enc_ag[2] = { OCTANS_ENC8(0x0D, 110), OCTANS_ENC8(0x01, 111) };
            size_t olen = dev.output_len >= 65u ? 65u : 64u;
            size_t off = (olen >= 65u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_ag, sizeof(enc_ag), 110u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 3; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                    if (n >= 5 && octans_proto_parse_indep(resp, (size_t)n, var, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_AULA_KBD_AA && dev.output_len >= 32u && dev.can_read) {
            static const uint8_t enc_aa[2] = { OCTANS_ENC8(0xAA, 114), OCTANS_ENC8(0x10, 115) };
            size_t olen = dev.output_len >= 33u ? 33u : 32u;
            size_t off = (olen >= 33u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_aa, sizeof(enc_aa), 114u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                    if (n >= 27 && octans_proto_parse_indep(resp, (size_t)n, var, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_AULA_24G_RX && dev.output_len >= 3u && dev.can_read) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            req[0] = 11u;
            req[1] = octans_dec8(OCTANS_ENC8(0x81u, 118), 118u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                    if (n >= 18 && octans_proto_parse_indep(resp, (size_t)n, var, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_DAREU_TG && dev.feature_len >= 64u) {
            static const uint8_t rids[3] = { 0u, 7u, 10u };
            static const uint8_t enc_tg[4] = {
                OCTANS_ENC8(0x00, 120), OCTANS_ENC8(0x03, 121),
                OCTANS_ENC8(0x07, 122), OCTANS_ENC8(0x80, 123)
            };
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 65u;
            size_t off = (flen >= 65u) ? 1u : 0u;
            for (int ri = 0; ri < 3 && !ok; ++ri) {
                memset(req, 0, flen);
                if (off) req[0] = rids[ri];
                octans_dec_bytes(req + off, enc_tg, sizeof(enc_tg), 120u);
                if (octans_hid_set_feature(&dev, req, flen) == 0) {
                    Sleep(35);
                    int n = octans_hid_get_feature(&dev, rids[ri], resp, flen);
                    if (n >= 10 && octans_proto_parse_dareu_tg(resp, (size_t)n, out)) { ok = 1; }
                }
            }
        } else if (e->variant == VAR_INDEP_IQUNIX_TEXTHEX && dev.output_len >= 32u && dev.can_read &&
                   wstr_contains_nocase(info->product, L"IQUNIX")) {
            static const uint8_t enc_iq[2] = { OCTANS_ENC8(0x65, 126), OCTANS_ENC8(0x02, 127) };
            size_t olen = dev.output_len >= 33u ? 33u : 32u;
            size_t off = (olen >= 33u) ? 1u : 0u;
            memset(req, 0, olen);
            octans_dec_bytes(req + off, enc_iq, sizeof(enc_iq), 126u);
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                int n = octans_hid_read_input(&dev, resp, sizeof(resp), 260u);
                if (n >= 4 && octans_proto_parse_iqunix_texthex(resp, (size_t)n, out)) { ok = 1; }
            }
        } else if (e->variant == VAR_INDEP_MACHENIKE_ITON && dev.output_len >= 3u && dev.can_read) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            req[0] = octans_dec8(OCTANS_ENC8(0xA0u, 134), 134u);
            req[1] = 0x02u;
            if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                octans_hid_write_output(&dev, req, olen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 180u);
                    if (n >= 3 && octans_proto_parse_machenike_iton(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_INCOTT_09 && dev.feature_len >= 9u) {
            int match_prod = (!info->product[0]) ||
                wstr_contains_nocase(info->product, L"mouse") ||
                wstr_contains_nocase(info->product, L"g23") ||
                wstr_contains_nocase(info->product, L"g24") ||
                wstr_contains_nocase(info->product, L"ghero") ||
                wstr_contains_nocase(info->product, L"zero") ||
                wstr_contains_nocase(info->product, L"incott") ||
                wstr_contains_nocase(info->product, L"wraith");
            if (match_prod) {
                size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 64u;
                memset(req, 0, flen);
                req[0] = 0x09u;
                req[1] = octans_dec8(OCTANS_ENC8(0x89u, 136), 136u);
                if (octans_hid_set_feature(&dev, req, flen) == 0) {
                    for (int a = 0; a < 5 && !ok; ++a) {
                        Sleep(40);
                        int n = octans_hid_get_feature(&dev, 0x09u, resp, flen);
                        if (n >= 3 && octans_proto_parse_incott(resp, (size_t)n, out)) { ok = 1; }
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_LENOVO_VOIP && dev.feature_len >= 61u && dev.can_read) {
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 61u;
            memset(req, 0, flen);
            req[0] = 0x24u;
            req[1] = 0x01u;
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                for (int a = 0; a < 4; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                    if (n >= 8 && octans_proto_parse_lenovo_voip(resp, (size_t)n, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_EPOS_H3PRO && dev.output_len >= 107u && dev.can_read) {
            int is_wired = (e->flags & PROTO_F_WIRED) != 0;
            octans_proto_build_epos_h3pro(is_wired, req);
            if (octans_hid_write_output(&dev, req, 107u) == 0 ||
                octans_hid_write_output_intr(&dev, req, 107u) == 0) {
                for (int a = 0; a < 6; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 200u);
                    if (n >= 19 && octans_proto_parse_epos_h3pro(resp, (size_t)n, is_wired, out)) { ok = 1; break; }
                }
            }
        } else if (e->variant == VAR_INDEP_DARMOSHARK_NORDIC && dev.output_len >= 64u && dev.can_read) {
            size_t olen = (dev.output_len <= sizeof(req)) ? dev.output_len : 64u;
            size_t off = (olen >= 65u) ? 1u : 0u;
            static const int modes[2] = { 1, 0 }; /* Try 2.4G wireless (0x41) first, then wired (0x01) */
            for (int mi = 0; mi < 2 && !ok; ++mi) {
                memset(req, 0, olen);
                octans_proto_build_darmoshark_nordic(modes[mi], req + off);
                if (octans_hid_write_output_intr(&dev, req, olen) == 0 ||
                    octans_hid_write_output(&dev, req, olen) == 0) {
                    for (int a = 0; a < 5; ++a) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                        if (n >= 12 && octans_proto_parse_darmoshark_nordic(resp, (size_t)n, out)) {
                            if (modes[mi] == 0) out->conn = OCTANS_CONN_WIRED;
                            ok = 1;
                            break;
                        }
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_DARMOSHARK_TELINK && dev.feature_len >= 21u) {
            size_t flen = (dev.feature_len <= sizeof(req)) ? dev.feature_len : 21u;
            /* Prime buffer with opcode 0x03 (bond info), then query opcode 0x06 (identity + battery) */
            memset(req, 0, flen);
            octans_proto_build_darmoshark_telink(0x03u, req);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(15);
                octans_hid_get_feature(&dev, 0x51u, resp, flen);
            }
            memset(req, 0, flen);
            octans_proto_build_darmoshark_telink(0x06u, req);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                for (int a = 0; a < 5 && !ok; ++a) {
                    Sleep(20);
                    int n = octans_hid_get_feature(&dev, 0x51u, resp, flen);
                    if (n >= 12 && octans_proto_parse_darmoshark_telink(resp, (size_t)n, out)) {
                        ok = 1;
                    }
                }
            }
        }
    }

    octans_hid_close(&dev);
    return ok;
}

int octans_proto_query_iface(const proto_entry_t *e,
                             const octans_hid_info_t *info,
                             uint32_t listen_ms,
                             int debug_log,
                             octans_proto_reading_t *out) {
    if (!e || !info || !out) return 0;
    init_reading_from_entry(e, info, out);

    if (e->family == FAM_RAZER90) {
        if (e->variant == VAR_RAZER_BSV2_2020) return run_razer_bsv2_live(info, out);
        if (e->variant == VAR_RAZER_PA_HEADSET) return run_razer_pa_live(info, out);
        return run_razer90_live(e, info, out);
    }
    if (e->tx == TX_LISTEN || (e->flags & PROTO_F_PASSIVE_ONLY)) {
        return run_passive_listen_live(e, info, listen_ms, out);
    }
    return run_feature_or_output_live(e, info, debug_log, out);
}
