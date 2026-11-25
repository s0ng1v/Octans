#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "family_request.h"
#include "../core/hid_dev.h"

#include <stdio.h>
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
    static const uint8_t prefix[9] = {0xFF, 0x0F, 0x05, 0xFE, 0x12, 0x04, 0x1F, 0x08, 0x05};
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
    if (buf[1] != 0x72u || buf[5] != 0x07u || buf[7] > 100u) return 0;
    out->battery_percent = (int8_t)buf[7];
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
    if (buf[0] != 0x01u || buf[2] != 0x02u || buf[3] != 0x00u) return 0;
    *out_raw = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
    return 1;
}

int octans_proto_parse_corsair_void_elite(const uint8_t *buf, size_t len, octans_proto_reading_t *out) {
    if (!buf || len < 5u || !out) return 0;
    if (buf[0] != 0x64u) return 0;
    uint8_t lvl = buf[2] & 0x7Fu;
    if (lvl > 100u) return 0;
    out->battery_percent = (int8_t)lvl;
    out->power_state = (buf[4] == 4u || buf[4] == 5u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
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
        if (len >= 4u && buf[0] == 0xAAu && buf[1] <= 100u) {
            out->battery_percent = (int8_t)buf[1];
            out->power_state = buf[3] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
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
        if (buf[0] != 0xD2u) return 0;
        uint8_t raw = buf[1];
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
        if (buf[0] != 0xB0u || len < 4u) return 0;
        uint8_t st = buf[3];
        if (st == 0x00u) return 0; /* headset off */
        int pct = (buf[2] <= 4u) ? (int)buf[2] * 25 : (int)octans_clamp_percent((int)buf[2]);
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

    return 0;
}

/* ---- Family 11: Rapoo passive ---- */

int octans_proto_parse_rapoo(const uint8_t *buf,
                             size_t len,
                             octans_proto_variant_t var,
                             octans_proto_reading_t *out) {
    if (!buf || !out) return 0;

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
    if (buf[0] == 0xD1u && buf[1] == 0x01u && buf[2] <= 100u) {
        out->battery_percent = (int8_t)buf[2];
        out->power_state = OCTANS_PWR_DISCHARGING;
        out->valid = 1;
        return 1;
    }
    if (buf[0] != 0x30u) return 0;
    if (buf[1] == 0x02u) {
        out->asleep = 1;
        out->power_state = OCTANS_PWR_SLEEPING;
        return 1;
    }
    if (len < 15u || buf[1] != 0x00u || buf[13] > 100u) return 0;
    out->battery_percent = (int8_t)buf[13];
    out->power_state = buf[12] ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    if (out_dev_id) {
        size_t k = 0;
        for (size_t i = 4u; i < 8u; ++i) {
            if (buf[i] >= 32u && buf[i] < 127u) out_dev_id[k++] = (char)buf[i];
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
        L"ACE60", L"ACE68", L"ACE75", L"G75 V2", L"G87 V2", L"G87 V3", L"G98 V3",
        L"GOD60", L"GT75", L"JET75", L"K87 V3", L"K87S", L"K99 V3", L"MIX87",
        L"ZERO75X", L"Keyboard", NULL
    };
    for (size_t i = 0; k[i]; ++i) {
        if (wstr_contains_nocase(name, k[i])) return 1;
    }
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

int octans_proto_parse_mchose_by(const uint8_t *buf, size_t len, int is_wired, octans_proto_reading_t *out) {
    if (!buf || !out) return 0;
    uint8_t v = 0;
    if (is_wired) {
        size_t base = ((buf[0] == 0x06u || buf[0] == 0x09u) && len >= 9u) ? 1u : 0u;
        if (base + 8u > len) return 0;
        if (buf[base] != 0x87u) return 0;
        v = buf[base + 7u];
    } else {
        size_t base = (buf[0] == 0x13u && len >= 3u) ? 1u : 0u;
        if (base + 2u > len) return 0;
        if (buf[base] != 0x4Au) return 0;
        v = buf[base + 1u];
    }
    uint8_t lo = (uint8_t)(v & 0x0Fu);
    uint8_t hi = (uint8_t)((v >> 4) & 0x0Fu);
    if (lo == 0u || lo > 10u) return 0;
    out->battery_percent = (int8_t)(lo * 10u);
    out->is_estimated = 1;
    out->power_state = (hi == 1u) ? OCTANS_PWR_CHARGING
                     : (hi == 2u ? OCTANS_PWR_FULL : OCTANS_PWR_DISCHARGING);
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
        out->battery_percent = (int8_t)buf[9];
        out->power_state = OCTANS_PWR_DISCHARGING;
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

    default:
        return 0;
    }
}

/* ---- Live interface matcher & runner ---- */

int octans_proto_matches_iface(const proto_entry_t *e, const octans_hid_info_t *info) {
    if (!e || !info) return 0;
    if (e->vid != 0xFFFFu && e->vid != info->vid) return 0;
    if (e->pid != 0xFFFFu && e->pid != info->pid) return 0;
    if (e->usage_page != 0u && e->usage_page != info->usage_page) return 0;
    if (e->usage != 0u && e->usage != info->usage) return 0;
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
    uint8_t tx_id = (e->variant == VAR_RAZER90_KB_WIRELESS) ? 0x9Fu
                  : (e->variant == VAR_RAZER90_KB_WIRED ? 0x3Fu : 0x1Fu);
    uint8_t req[91];
    uint8_t resp[128];
    size_t flen = dev.feature_len >= 91 ? dev.feature_len : 91;
    if (flen > sizeof(resp)) flen = sizeof(resp);

    octans_proto_build_razer90(tx_id, 0x80u, req);
    if (octans_hid_set_feature(&dev, req, 91u) == 0) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            Sleep(12);
            if (octans_hid_get_feature(&dev, 0x00u, resp, flen) < 0) continue;
            uint8_t st = 0, val = 0;
            if (!octans_proto_parse_razer90(resp, flen, 0x80u, &st, &val)) continue;
            if (st == 0x01u) continue;
            if (st == 0x04u) {
                out->asleep = 1;
                out->power_state = OCTANS_PWR_SLEEPING;
                ok = 1;
                break;
            }
            if (st == 0x02u) {
                out->battery_percent = (int8_t)octans_clamp_percent(octans_round_div((uint32_t)val * 100u, 255u));
                out->power_state = OCTANS_PWR_DISCHARGING;
                out->valid = 1;
                ok = 1;
                break;
            }
        }
    }

    if (ok && out->valid) {
        octans_proto_build_razer90(tx_id, 0x84u, req);
        if (octans_hid_set_feature(&dev, req, 91u) == 0) {
            Sleep(12);
            if (octans_hid_get_feature(&dev, 0x00u, resp, flen) >= 0) {
                uint8_t st = 0, val = 0;
                if (octans_proto_parse_razer90(resp, flen, 0x84u, &st, &val) && st == 0x02u && val != 0u) {
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
    static const uint8_t req_hdr[9] = {0xFF, 0x0A, 0x00, 0xFD, 0x04, 0x12, 0xF1, 0x02, 0x05};
    octans_hid_dev_t dev;
    if (octans_hid_open(info->path, 1, 0, &dev) < 0 || dev.feature_len < 64) {
        octans_hid_close(&dev);
        return 0;
    }
    uint8_t req[64], resp[64];
    memset(req, 0, sizeof(req));
    memcpy(req, req_hdr, sizeof(req_hdr));
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
                    (e->variant == VAR_KB_KEYCHRON) || (e->variant == VAR_INDEP_ALIENWARE);
    if (octans_hid_open(info->path, 1, need_read, &dev) < 0 || !dev.can_write) {
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

    if (e->family == FAM_SINOWEALTH && dev.feature_len >= 65) {
        octans_proto_build_sinowealth(0x00u, 0x83u, req);
        if (octans_hid_set_feature(&dev, req, 65u) == 0) {
            Sleep(90);
            if (octans_hid_get_feature(&dev, 0x00u, resp, 65u) >= 65 &&
                octans_proto_parse_sinowealth(resp, 65u, out)) {
                ok = 1;
            }
        }
    } else if (e->family == FAM_COMPX && dev.output_len >= 17) {
        octans_proto_build_compx(0x04u, req);
        ok = send_and_read_loop(&dev, e->tx == TX_FEATURE, req, 17u, 4, 120u,
                                wrap_parse_compx, NULL, out);
    } else if (e->family == FAM_ATK_NORDIC) {
        if (e->variant == VAR_ATK_54L15_64B && dev.output_len >= 64) {
            octans_proto_build_atk_54l15(req);
            ok = send_and_read_loop(&dev, 0, req, 64u, 4, 120u, wrap_parse_atk54, NULL, out);
        } else if (e->variant == VAR_ATK_52840_17B && dev.output_len >= 17) {
            octans_proto_build_atk_52840(req);
            ok = send_and_read_loop(&dev, 0, req, 17u, 4, 120u, wrap_parse_atk52, NULL, out);
        }
    } else if (e->family == FAM_ASUS && dev.output_len >= 64) {
        size_t rlen = 64u;
        octans_proto_build_rog_1207(e->report_id, req, &rlen);
        if (var == VAR_ROG_OMNI_KB) req[2] = 0x01u;
        if (rlen > dev.output_len) rlen = dev.output_len;
        ok = send_and_read_loop(&dev, 0, req, rlen, 4, 120u, wrap_parse_rog, &var, out);
    } else if (e->family == FAM_CORSAIR && e->variant == VAR_CORSAIR_BRAGI && dev.output_len >= 64) {
        octans_proto_build_corsair_bragi(0x09u, 0x0Fu, req);
        if (octans_hid_write_output(&dev, req, 64u) == 0) {
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
    } else if (e->family == FAM_STEELSERIES && dev.output_len >= 64) {
        if (e->variant == VAR_SS_NOVA_ELITE) {
            req[0] = 0x01u;
            req[1] = 0xB0u;
        } else {
            req[0] = (e->variant == VAR_SS_NOVA_PRO) ? 0x06u : 0x00u;
            req[1] = (e->variant == VAR_SS_RIVAL3) ? 0xAAu : (e->variant == VAR_SS_AEROX ? 0xD2u : 0xB0u);
            if (e->variant == VAR_SS_RIVAL3) req[2] = 0x01u;
        }
        ok = send_and_read_loop(&dev, 0, req, 64u, 4, 150u, wrap_parse_ss, &var, out);
    } else if (e->family == FAM_HYPERX) {
        if (e->variant == VAR_HX_ALPHA_WIRELESS && dev.output_len >= 31) {
            req[0] = 0x21u; req[1] = 0xBBu; req[2] = 0x0Bu;
            if (send_and_read_loop(&dev, 0, req, 31u, 5, 200u, wrap_parse_hx, &var, out)) {
                ok = 1;
                req[2] = 0x0Cu;
                if (octans_hid_write_output(&dev, req, 31u) == 0) {
                    int cn = octans_hid_read_input(&dev, resp, sizeof(resp), 200);
                    if (cn > 0) octans_proto_parse_hyperx(resp, (size_t)cn, var, out);
                }
            }
        } else if (dev.output_len >= 52) {
            size_t rlen = dev.output_len > 64 ? 64u : dev.output_len;
            if (e->variant == VAR_HX_GEN1) { req[0]=0x06; req[1]=0xFF; req[2]=0xBB; req[3]=0x02; }
            else if (e->variant == VAR_HX_GEN2) { req[0]=0x66; req[1]=0x89; }
            else { req[0]=0x50; req[1]=0x02; }
            ok = send_and_read_loop(&dev, 0, req, rlen, 4, 120u, wrap_parse_hx, &var, out);
        }
    } else if (e->family == FAM_AJAZZ) {
        if (e->variant == VAR_EWEADN_0X30 && dev.output_len >= 33) {
            octans_proto_build_eweadn(0x30u, req);
            ok = send_and_read_loop(&dev, 0, req, 33u, 4, 120u, wrap_parse_eweadn, NULL, out);
        } else if (e->variant == VAR_AJAZZ_0X10 && dev.output_len >= 33) {
            req[0] = 0x10u; req[2] = 0x01u; req[3] = 0x0Bu;
            ok = send_and_read_loop(&dev, 0, req, 33u, 4, 120u, wrap_parse_aj10, NULL, out);
        } else if (e->variant == VAR_AJAZZ_0X05 && dev.output_len >= 64) {
            req[0] = 0xF7u;
            ok = send_and_read_loop(&dev, 0, req, 64u, 4, 120u, wrap_parse_aj05, NULL, out);
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
            req[0] = 0x04u; req[1] = 0x01u; req[5] = 0x1Au;
            ok = send_and_read_loop(&dev, 0, req, 32u, 4, 120u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_KEYCHRON && dev.feature_len >= 64) {
            req[0] = 0xB3u; req[1] = 0x06u;
            ok = send_and_read_loop(&dev, 1, req, 64u, 3, 120u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_VIA_RAW && dev.output_len >= 32) {
            size_t len = dev.output_len >= 33 ? 33u : 32u;
            octans_proto_build_via_battery(req);
            ok = send_and_read_loop(&dev, 0, req, len, 2, 150u, wrap_parse_kb, &var, out);
        } else if (e->variant == VAR_KB_MCHOSE_BY && is_mchose_by_kb_name(info->product)) {
            if (dev.feature_len >= 519u) {
                uint8_t by_req[520];
                uint8_t by_resp[600];
                static const uint8_t rids[2] = {0x06u, 0x09u};
                for (int ri = 0; ri < 2 && !ok; ++ri) {
                    size_t flen = (dev.feature_len <= sizeof(by_req)) ? dev.feature_len : 520u;
                    memset(by_req, 0, flen);
                    by_req[0] = rids[ri];
                    by_req[1] = 0x87u; by_req[4] = 0x01u; by_req[6] = 0x02u;
                    if (octans_hid_set_feature(&dev, by_req, flen) == 0) {
                        Sleep(60);
                        int n = octans_hid_get_feature(&dev, rids[ri], by_resp, flen);
                        if (n >= 8 && octans_proto_parse_mchose_by(by_resp, (size_t)n, 1, out)) {
                            out->conn = OCTANS_CONN_WIRED;
                            ok = 1;
                        }
                    }
                }
            } else if (dev.output_len >= 20u && dev.can_read) {
                size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 20u;
                memset(req, 0, olen);
                req[0] = 0x13u; req[1] = 0x4Au; req[2] = 0x01u;
                if (octans_hid_write_output_intr(&dev, req, olen) == 0) {
                    for (int a = 0; a < 6; ++a) {
                        int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                        if (n >= 2 && octans_proto_parse_mchose_by(resp, (size_t)n, 0, out)) {
                            ok = 1;
                            break;
                        }
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
                if (octans_hid_write_output_intr(&dev, req, olen) != 0) break;
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
                if (octans_hid_write_output_intr(&dev, req, olen) != 0) break;
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
                   wstr_contains_nocase(info->product, L"G7 MAX")) {
            size_t flen = dev.feature_len >= 64u ? 64u : dev.feature_len;
            octans_proto_build_mchose_g7max(0x03u, req);
            if (octans_hid_set_feature(&dev, req, flen) == 0) {
                Sleep(50);
                int n = octans_hid_get_feature(&dev, 0x03u, resp, flen);
                if (n >= 8 && octans_proto_parse_mchose_g7max(resp, (size_t)n, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_NORDIC_GEN && dev.feature_len >= 32) {
            req[0] = 0x05u; req[1] = 0x15u; req[4] = 0x01u;
            if (octans_hid_set_feature(&dev, req, 32u) == 0) {
                Sleep(80);
                if (octans_hid_get_feature(&dev, 0x05u, resp, 32u) >= 10 &&
                    octans_proto_parse_indep(resp, 32u, var, out)) {
                    ok = 1;
                }
            }
        } else if (e->variant == VAR_INDEP_NZXT && dev.output_len >= 64) {
            req[0] = 0x4Eu; req[1] = 0x02u; req[2] = 0x81u; req[4] = 0xB0u;
            ok = send_and_read_loop(&dev, 0, req, 64u, 3, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_ALIENWARE && dev.feature_len >= 65) {
            req[0] = 0x00u; req[1] = 0x0Eu; req[2] = 0xA1u;
            ok = send_and_read_loop(&dev, 1, req, 65u, 4, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_MCHOSE_G7 && dev.output_len >= 64u && dev.can_read) {
            static const uint8_t g7_hdr[9] = {0x00,0x55,0x30,0xA5,0x0B,0x2E,0x01,0x01,0x01};
            size_t olen = dev.output_len >= 65u ? 65u : 64u;
            memcpy(req, g7_hdr, sizeof(g7_hdr));
            if (octans_hid_write_output_intr(&dev, req, olen) == 0) {
                for (int a = 0; a < 5; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 220u);
                    if (n >= 10 && octans_proto_parse_indep(resp, (size_t)n, var, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_AUDEZE && dev.output_len >= 64) {
            static const uint8_t aud[6] = {0x05,0x5D,0x04,0x00,0xD6,0x0C};
            memcpy(req, aud, sizeof(aud));
            ok = send_and_read_loop(&dev, 0, req, 64u, 3, 120u, wrap_parse_indep, &var, out);
        } else if (e->variant == VAR_INDEP_MCHOSE_V9 && dev.output_len >= 64u && dev.can_read) {
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            req[0] = 0x55u; req[1] = 0x65u; req[2] = 0x01u;
            int wrc = octans_hid_write_output_intr(&dev, req, olen);
            if (wrc != 0 && olen >= 65u) {
                memset(req, 0, olen);
                req[0] = 0x00u; req[1] = 0x55u; req[2] = 0x65u; req[3] = 0x01u;
                wrc = octans_hid_write_output_intr(&dev, req, olen);
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
            static const uint8_t mc_hdr[9] = {0x55,0xAA,0x01,0x00,0x01,0x00,0x01,0x0B,0x00};
            size_t olen = dev.output_len <= sizeof(req) ? dev.output_len : 64u;
            memset(req, 0, olen);
            memcpy(req, mc_hdr, sizeof(mc_hdr));
            if (octans_hid_write_output_intr(&dev, req, olen) == 0) {
                for (int a = 0; a < 5; ++a) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 260u);
                    if (n >= 9 && octans_proto_parse_mchose_audio_mc(resp, (size_t)n, out)) {
                        ok = 1;
                        break;
                    }
                }
            }
        } else if (e->variant == VAR_INDEP_LOGI_ASTRO && dev.output_len >= 64) {
            static const uint8_t astro[6] = {0x02, 0x0C, 0x03, 0x00, 0x06, 0x0C};
            memcpy(req, astro, sizeof(astro));
            ok = send_and_read_loop(&dev, 0, req, 64u, 6, 200u, wrap_parse_indep, &var, out);
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
