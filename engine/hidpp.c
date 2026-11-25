#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hidpp.h"
#include "proto_obf.h"
#include "../core/hid_dev.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const uint8_t g_swids[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

static int wcs_contains_ci(const wchar_t *hay, const wchar_t *needle) {
    if (!hay || !needle || !needle[0]) return 0;
    size_t nlen = wcslen(needle);
    for (const wchar_t *p = hay; *p; ++p) {
        if (_wcsnicmp(p, needle, nlen) == 0) return 1;
    }
    return 0;
}

int octans_hidpp_is_receiver_pid(uint16_t pid, const wchar_t *product) {
    static const uint16_t rx_pids[] = {
        OCTANS_ENC16(0xC548, 11),                                                                                                   /* Bolt */
        OCTANS_ENC16(0xC52B, 11), OCTANS_ENC16(0xC532, 11),                                                                         /* Unifying */
        OCTANS_ENC16(0xC52F, 11), OCTANS_ENC16(0xC518, 11), OCTANS_ENC16(0xC51A, 11), OCTANS_ENC16(0xC51B, 11),
        OCTANS_ENC16(0xC521, 11), OCTANS_ENC16(0xC525, 11), OCTANS_ENC16(0xC526, 11),                                               /* Nano */
        OCTANS_ENC16(0xC52E, 11), OCTANS_ENC16(0xC531, 11), OCTANS_ENC16(0xC534, 11), OCTANS_ENC16(0xC535, 11), OCTANS_ENC16(0xC537, 11),
        OCTANS_ENC16(0xC539, 11), OCTANS_ENC16(0xC53A, 11), OCTANS_ENC16(0xC53D, 11), OCTANS_ENC16(0xC53F, 11),
        OCTANS_ENC16(0xC541, 11), OCTANS_ENC16(0xC543, 11), OCTANS_ENC16(0xC545, 11),                                               /* Lightspeed */
        OCTANS_ENC16(0xC547, 11), OCTANS_ENC16(0xC54B, 11), OCTANS_ENC16(0xC54D, 11),
        OCTANS_ENC16(0xC54E, 11), OCTANS_ENC16(0xC54F, 11), OCTANS_ENC16(0xC550, 11)
    };
    for (size_t i = 0; i < sizeof(rx_pids) / sizeof(rx_pids[0]); ++i) {
        if (pid == octans_dec16(rx_pids[i], 11u)) return 1;
    }
    if (product && product[0]) {
        if (wcs_contains_ci(product, L"receiver") ||
            wcs_contains_ci(product, L"lightspeed") ||
            wcs_contains_ci(product, L"unifying") ||
            wcs_contains_ci(product, L"bolt")) {
            return 1;
        }
    }
    return 0;
}

static int is_headset_pid(uint16_t pid, wchar_t *out_name, size_t out_chars) {
    static const struct {
        uint16_t pid;
        const wchar_t *name;
    } headsets[] = {
        {OCTANS_ENC16(0x0A66, 12), L"Logitech G533"},
        {OCTANS_ENC16(0x0AC4, 12), L"Logitech G535"},
        {OCTANS_ENC16(0x0A5C, 12), L"Logitech G633"},
        {OCTANS_ENC16(0x0A89, 12), L"Logitech G635"},
        {OCTANS_ENC16(0x0A5B, 12), L"Logitech G933"},
        {OCTANS_ENC16(0x0A87, 12), L"Logitech G935"},
        {OCTANS_ENC16(0x0AB5, 12), L"Logitech G733"},
        {OCTANS_ENC16(0x0AFE, 12), L"Logitech G733"},
        {OCTANS_ENC16(0x0B1F, 12), L"Logitech G733"},
        {OCTANS_ENC16(0x0ADF, 12), L"Logitech G435"},
        {OCTANS_ENC16(0x0B08, 12), L"Logitech G735"},
        {OCTANS_ENC16(0x0AA7, 12), L"Logitech G PRO"},
        {OCTANS_ENC16(0x0AAA, 12), L"Logitech G PRO X"},
        {OCTANS_ENC16(0x0ABA, 12), L"Logitech G PRO X"},
        {OCTANS_ENC16(0x0AFB, 12), L"Logitech G PRO X 2"},
        {OCTANS_ENC16(0x0AFC, 12), L"Logitech G PRO X 2"},
        {OCTANS_ENC16(0x0AED, 12), L"Logitech Zone Vibe"},
        {OCTANS_ENC16(0x0AEF, 12), L"Logitech Zone Wireless"},
    };
    for (size_t i = 0; i < sizeof(headsets) / sizeof(headsets[0]); ++i) {
        if (pid == octans_dec16(headsets[i].pid, 12u)) {
            if (out_name && out_chars > 0) {
                wcsncpy(out_name, headsets[i].name, out_chars - 1);
                out_name[out_chars - 1] = L'\0';
            }
            return 1;
        }
    }
    return 0;
}

void octans_hidpp_build_long(uint8_t slot,
                             uint8_t feat_idx,
                             uint8_t fn,
                             uint8_t swid,
                             const uint8_t *params,
                             size_t param_len,
                             uint8_t out20[20]) {
    memset(out20, 0, 20);
    out20[0] = 0x11u;
    out20[1] = slot;
    out20[2] = feat_idx;
    out20[3] = (uint8_t)(((fn & 0x0Fu) << 4) | (swid & 0x0Fu));
    if (params && param_len > 0) {
        size_t n = param_len > 16 ? 16 : param_len;
        memcpy(out20 + 4, params, n);
    }
}

void octans_hidpp_build_short(uint8_t slot, uint8_t sub_id, uint8_t reg, uint8_t out7[7]) {
    memset(out7, 0, 7);
    out7[0] = 0x10u;
    out7[1] = slot;
    out7[2] = sub_id;
    out7[3] = reg;
}

int octans_hidpp_parse_error(const uint8_t *frame, size_t len, uint8_t *out_err) {
    if (!frame || len < 6) return 0;
    if ((frame[0] == 0x11u && frame[2] == 0xFFu) ||
        (frame[0] == 0x10u && frame[2] == 0x8Fu)) {
        if (out_err) *out_err = frame[5];
        return 1;
    }
    return 0;
}

int octans_hidpp_classify_model_id(const char *model_id, octans_device_kind_t *out_kind) {
    if (!model_id || !model_id[0]) return 0;
    if (_strnicmp(model_id, "GAMEPAD.", 8) == 0 || _strnicmp(model_id, "WHEEL.", 6) == 0) {
        return -1; /* F310/F510/F710/G13/G29/G920/G923: no battery telemetry */
    }
    if (_strnicmp(model_id, "MOUSE.", 6) == 0) {
        if (out_kind) *out_kind = OCTANS_DEV_MOUSE;
        return 1;
    }
    if (_strnicmp(model_id, "KEYBOARD.", 9) == 0) {
        if (out_kind) *out_kind = OCTANS_DEV_KEYBOARD;
        return 1;
    }
    if (_strnicmp(model_id, "HEADSET.", 8) == 0) {
        if (out_kind) *out_kind = OCTANS_DEV_HEADSET;
        return 1;
    }
    return 0;
}

int octans_hidpp_parse_unified_battery_ex(const uint8_t *params,
                                          size_t len,
                                          int soc_supported,
                                          octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    uint8_t level = params[1];
    uint8_t status = params[2];

    if (soc_supported && pct > 0 && pct <= 100) {
        out->battery_percent = (int8_t)pct;
        out->is_estimated = 0;
    } else if (!soc_supported || pct == 0) {
        /* 0x1004 level bitmap: 1=critical(5%), 2=low(20%), 4=good(60%), 8=full(100%) */
        int approx = -1;
        if (level & 0x08u) approx = 100;
        else if (level & 0x04u) approx = 60;
        else if (level & 0x02u) approx = 20;
        else if (level & 0x01u) approx = 5;
        if (approx < 0) return 0;
        out->battery_percent = (int8_t)approx;
        out->is_estimated = 1;
    } else {
        return 0;
    }

    /* status: 0=discharging, 1=charging, 2=almost_full, 3=full, 4=slow_charge */
    if (status == 3u) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (status >= 1u && status <= 4u) {
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_unified_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    return octans_hidpp_parse_unified_battery_ex(params, len, 1, out);
}

int octans_hidpp_parse_battery_status(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    uint8_t status = params[2];

    /* 0x1000 quirk: while charging, firmware often reports dischargeLevel == 0.
     * Never report 0%; if full (status == 3), map to 100%, else fail and fall through. */
    if (pct == 0u) {
        if (status == 3u) {
            out->battery_percent = 100;
            out->is_estimated = 0;
            out->power_state = OCTANS_PWR_FULL;
            out->valid = 1;
            return 1;
        }
        return 0;
    }
    if (pct > 100u) return 0;

    out->battery_percent = (int8_t)pct;
    out->is_estimated = 0;
    if (status == 3u) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (status >= 1u && status <= 4u) {
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_battery_voltage(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint16_t mv = (uint16_t)(((uint16_t)params[0] << 8) | params[1]);
    if (mv < 2000u || mv > 5000u) return 0;

    /* 0x1001 flags: bit7=externalPower; bit0..2=chargeState (0=charging, 1=full, 2=not charging, 3..7=undefined).
     * Critical guard: flags==0x83 has cs==3 (undefined) and MUST NOT be treated as full or charging! */
    uint8_t flags = params[2];
    int ext = (flags & 0x80u) != 0;
    uint8_t cs = (uint8_t)(flags & 0x07u);
    int charging = ext && (cs == 0u);
    int full = ext && (cs == 1u);

    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
    out->is_estimated = 1;
    out->power_state = full ? OCTANS_PWR_FULL : (charging ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_adc_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t flags = params[2];
    if ((flags & 0x01u) == 0u) return 0; /* bit0=link: headset disconnected/off */
    uint16_t mv = (uint16_t)(((uint16_t)params[0] << 8) | params[1]);
    if (mv < 2000u || mv > 5000u) return 0;

    int full = (flags & 0x04u) != 0;
    int charging = (flags & 0x02u) != 0;
    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
    out->is_estimated = 1;
    out->power_state = full ? OCTANS_PWR_FULL : (charging ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_centpp_soc(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 2 || !out) return 0;
    uint8_t pct = params[0];
    if (pct == 0u || pct > 100u) return 0;
    int charging = (params[1] & 0x80u) != 0;
    out->battery_percent = (int8_t)pct;
    out->is_estimated = 0;
    if (charging && pct >= 100u) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (charging) {
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_v10_reg0d(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    if (pct == 0u || pct > 100u) return 0;

    uint8_t st_hi = (uint8_t)(params[2] & 0xF0u);
    out->battery_percent = (int8_t)pct;
    out->is_estimated = 0;
    if (st_hi == 0x90u) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (st_hi == 0x50u) {
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_v10_reg07(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 2 || !out) return 0;
    uint8_t lvl = params[0];
    uint8_t chg = params[1];

    /* Format A: Classic Unifying 4-level register (lvl in {1,3,5,7} with chg 0x20-series or 2-3B reply) */
    int is_legacy_4lvl = (lvl == 1u || lvl == 3u || lvl == 5u || lvl == 7u) &&
                         (len < 4u || (chg & 0x20u) != 0u || (params[2] == 0u && params[3] == 0u));
    if (is_legacy_4lvl) {
        int pct = -1;
        switch (lvl) {
        case 7: pct = 90; break;
        case 5: pct = 50; break;
        case 3: pct = 20; break;
        case 1: pct = 5; break;
        default: break;
        }
        if (pct < 0) return 0;

        out->battery_percent = (int8_t)pct;
        out->is_estimated = 1;
        if ((chg & 0x22u) == 0x22u) {
            out->power_state = OCTANS_PWR_FULL;
        } else if ((chg & 0x21u) == 0x21u) {
            out->power_state = OCTANS_PWR_CHARGING;
        } else {
            out->power_state = OCTANS_PWR_DISCHARGING;
        }
        out->valid = 1;
        return 1;
    }

    /* Format B: Direct percentage register (p[0]=1..100, p[3] bit0=charging, bit1=full) */
    if (lvl >= 1u && lvl <= 100u) {
        uint8_t st = (len >= 4u) ? params[3] : 0u;
        out->battery_percent = (int8_t)lvl;
        out->is_estimated = 0;
        if (st & 0x02u) {
            out->power_state = OCTANS_PWR_FULL;
        } else if (st & 0x01u) {
            out->power_state = OCTANS_PWR_CHARGING;
        } else {
            out->power_state = OCTANS_PWR_DISCHARGING;
        }
        out->valid = 1;
        return 1;
    }

    return 0;
}

int octans_hidpp_parse_v10_reg0f(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 2 || !out) return 0;
    uint16_t mv = (uint16_t)(((uint16_t)params[0] << 8) | params[1]);
    if (mv < 2000u || mv > 5000u) return 0;
    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
    out->is_estimated = 1;
    out->power_state = OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_headset_reg(const uint8_t *frame, size_t len, octans_proto_reading_t *out) {
    if (!frame || len < 7 || !out) return 0;
    uint16_t mv = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    if (mv < 2000u || mv > 5000u) return 0;
    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_LOGI_G535);
    out->is_estimated = 1;
    out->power_state = (frame[6] == 0x03u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

octans_device_kind_t octans_hidpp_map_kind(uint8_t dev_type) {
    switch (dev_type) {
    case 0:
    case 2:
        return OCTANS_DEV_KEYBOARD;
    case 3:
    case 4:
    case 5:
        return OCTANS_DEV_MOUSE;
    case 8:
        return OCTANS_DEV_HEADSET;
    case 12:
        return OCTANS_DEV_GAMEPAD;
    default:
        return OCTANS_DEV_UNKNOWN;
    }
}

typedef struct hidpp_channel {
    octans_hid_dev_t long_dev;
    octans_hid_dev_t short_dev;
    int has_short;
    uint8_t swid_idx;
    int last_err;
    int last_err_is_v10;
    uint8_t last_err_code;
} hidpp_channel_t;

static int hidpp_xfer_once(hidpp_channel_t *ch,
                           uint8_t slot,
                           uint8_t feat_idx,
                           uint8_t fn,
                           const uint8_t *params,
                           size_t param_len,
                           uint32_t timeout_ms,
                           uint8_t out_params[16]) {
    ch->last_err = 0;
    ch->last_err_is_v10 = 0;
    ch->last_err_code = 0;
    ch->swid_idx = (uint8_t)((ch->swid_idx + 1u) % 6u);
    uint8_t swid = g_swids[ch->swid_idx];
    uint8_t fn_swid = (uint8_t)(((fn & 0x0Fu) << 4) | (swid & 0x0Fu));

    uint8_t req[20];
    octans_hidpp_build_long(slot, feat_idx, fn, swid, params, param_len, req);
    octans_hid_drain(&ch->long_dev, 8);
    if (ch->has_short) octans_hid_drain(&ch->short_dev, 8);

    if (octans_hid_write_output(&ch->long_dev, req, sizeof(req)) < 0) {
        return 0;
    }

    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeout_ms;
    while (GetTickCount64() < deadline) {
        octans_hid_dev_t *readers[2] = {
            &ch->long_dev,
            ch->has_short ? &ch->short_dev : NULL
        };
        for (int i = 0; i < 2; ++i) {
            if (!readers[i] || !readers[i]->can_read) continue;
            uint8_t resp[64];
            int n = octans_hid_read_input(readers[i], resp, sizeof(resp), 25);
            if (n < 4 || resp[1] != slot) continue;

            uint8_t err = 0;
            if (octans_hidpp_parse_error(resp, (size_t)n, &err) &&
                resp[3] == feat_idx && resp[4] == fn_swid) {
                ch->last_err = 1;
                ch->last_err_is_v10 = (resp[0] == 0x10u && resp[2] == 0x8Fu) ? 1 : 0;
                ch->last_err_code = err;
                return 0;
            }
            if (resp[2] == feat_idx && resp[3] == fn_swid) {
                memset(out_params, 0, 16);
                size_t avail = (size_t)(n - 4);
                if (avail > 16) avail = 16;
                memcpy(out_params, resp + 4, avail);
                return 1;
            }
        }
    }
    return 0;
}

static int hidpp_xfer(hidpp_channel_t *ch,
                      uint8_t slot,
                      uint8_t feat_idx,
                      uint8_t fn,
                      const uint8_t *params,
                      size_t param_len,
                      uint32_t timeout_ms,
                      uint8_t out_params[16]) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (hidpp_xfer_once(ch, slot, feat_idx, fn, params, param_len, timeout_ms, out_params)) {
            return 1;
        }
        /* Retry on Busy: HID++ 2.0 err 0x08 or HID++ 1.0 receiver err 0x07 */
        int is_busy = ch->last_err &&
                      ((!ch->last_err_is_v10 && ch->last_err_code == 0x08u) ||
                       (ch->last_err_is_v10 && ch->last_err_code == 0x07u));
        if (!is_busy) break;
        Sleep(40);
    }
    return 0;
}

static int hidpp_probe_feature(hidpp_channel_t *ch, uint8_t slot, uint16_t feature_id, uint8_t *out_idx) {
    uint8_t p[2] = {(uint8_t)(feature_id >> 8), (uint8_t)(feature_id & 0xFFu)};
    uint8_t ans[16];
    if (!hidpp_xfer(ch, slot, 0x00u, 0x00u, p, 2, 600, ans)) return 0;
    if (ans[0] == 0) return 0;
    *out_idx = ans[0];
    return 1;
}

/* Returns -1 if device explicitly identifies as non-battery (GAMEPAD./WHEEL./steering wheel). */
static int hidpp_read_identity(hidpp_channel_t *ch,
                               uint8_t slot,
                               wchar_t *out_name,
                               size_t name_cap,
                               octans_device_kind_t *out_kind,
                               char *out_unitid,
                               size_t uid_cap) {
    int no_battery = 0;

    uint8_t name_idx = 0;
    if (hidpp_probe_feature(ch, slot, 0x0005u, &name_idx)) {
        uint8_t ans[16];
        if (hidpp_xfer(ch, slot, name_idx, 0u, NULL, 0, 500, ans) && ans[0] > 0) {
            size_t total = ans[0] > 60 ? 60 : ans[0];
            char utf8[64];
            size_t pos = 0;
            while (pos < total) {
                uint8_t off = (uint8_t)pos;
                uint8_t chunk[16];
                if (!hidpp_xfer(ch, slot, name_idx, 1u, &off, 1, 400, chunk)) break;
                size_t take = total - pos;
                if (take > 16) take = 16;
                memcpy(utf8 + pos, chunk, take);
                pos += take;
            }
            utf8[pos] = '\0';
            if (pos > 0) {
                MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out_name, (int)name_cap);
                out_name[name_cap - 1] = L'\0';
            }
        }
        if (hidpp_xfer(ch, slot, name_idx, 2u, NULL, 0, 400, ans)) {
            if (ans[0] == 10u || ans[0] == 11u) {
                no_battery = 1; /* steering wheel / joystick */
            }
            octans_device_kind_t k = octans_hidpp_map_kind(ans[0]);
            if (k != OCTANS_DEV_UNKNOWN) *out_kind = k;
        }
    }

    uint8_t info_idx = 0;
    if (hidpp_probe_feature(ch, slot, 0x0003u, &info_idx)) {
        uint8_t ans[16];
        if (hidpp_xfer(ch, slot, info_idx, 0u, NULL, 0, 500, ans)) {
            if ((ans[1] | ans[2] | ans[3] | ans[4]) != 0u) {
                snprintf(out_unitid, uid_cap, "hidpp:%02x%02x%02x%02x",
                         ans[1], ans[2], ans[3], ans[4]);
            }
            char model_str[12];
            memcpy(model_str, ans + 5, 11);
            model_str[11] = '\0';
            int cls = octans_hidpp_classify_model_id(model_str, out_kind);
            if (cls < 0) no_battery = 1;
        }
    }
    return no_battery ? -1 : 0;
}

static int hidpp_read_v10_slot(hidpp_channel_t *ch, uint8_t slot, octans_proto_reading_t *r) {
    if (!ch->has_short) return 0;
    /* Note: HID++ 1.0 register 0x0F is confirmed nonexistent and excluded to avoid wasted probe timeouts. */
    static const uint8_t regs[2] = {0x0Du, 0x07u};
    for (int i = 0; i < 2; ++i) {
        uint8_t req[7];
        octans_hidpp_build_short(slot, 0x81u, regs[i], req);
        octans_hid_drain(&ch->short_dev, 8);
        if (octans_hid_write_output(&ch->short_dev, req, sizeof(req)) < 0) continue;

        ULONGLONG deadline = GetTickCount64() + 450ULL;
        while (GetTickCount64() < deadline) {
            uint8_t resp[32];
            int n = octans_hid_read_input(&ch->short_dev, resp, sizeof(resp), 30);
            if (n >= 7 && resp[0] == 0x10u && resp[1] == slot &&
                resp[2] == 0x81u && resp[3] == regs[i]) {
                if (regs[i] == 0x0Du && octans_hidpp_parse_v10_reg0d(resp + 4, (size_t)(n - 4), r)) {
                    snprintf(r->source_tag, sizeof(r->source_tag), "hidpp10_reg0d");
                    return 1;
                }
                if (regs[i] == 0x07u && octans_hidpp_parse_v10_reg07(resp + 4, (size_t)(n - 4), r)) {
                    snprintf(r->source_tag, sizeof(r->source_tag), "hidpp10_reg07");
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int hidpp_query_slot(hidpp_channel_t *ch,
                            const octans_hid_info_t *info,
                            uint8_t slot,
                            int is_receiver,
                            int is_headset_hint,
                            octans_proto_reading_t *out) {
    octans_reading_init(out);
    out->vid = info->vid;
    out->pid = info->pid;
    if (info->is_bluetooth) {
        out->conn = OCTANS_CONN_BLUETOOTH;
    } else if (is_receiver || is_headset_hint) {
        out->conn = OCTANS_CONN_DONGLE_24G;
    } else {
        out->conn = OCTANS_CONN_WIRED;
    }
    wcsncpy(out->brand, L"Logitech", 31);
    out->brand[31] = L'\0';

    if (info->product[0]) {
        wcsncpy(out->name, info->product, 63);
        out->name[63] = L'\0';
    } else {
        wcscpy(out->name, L"Logitech Device");
    }
    if (is_headset_hint) {
        out->kind = OCTANS_DEV_HEADSET;
    }

    uint8_t ping_ans[16];
    int ping_ok = hidpp_xfer(ch, slot, 0x00u, 0x01u, NULL, 0, is_receiver ? 1000u : 600u, ping_ans);

    /* Wake-up retry if device reports ResourceError (0x0B) on first ping */
    if (!ping_ok && ch->last_err && !ch->last_err_is_v10 && ch->last_err_code == 0x0Bu) {
        Sleep(80);
        ping_ok = hidpp_xfer(ch, slot, 0x00u, 0x01u, NULL, 0, 800u, ping_ans);
    }

    if (!ping_ok && ch->last_err) {
        /* Empty / unpaired receiver slot: HID++ 1.0 0x8F err 0x08 OR HID++ 2.0 0xFF err 0x0C */
        if ((ch->last_err_is_v10 && ch->last_err_code == 0x08u) ||
            (!ch->last_err_is_v10 && ch->last_err_code == 0x0Cu)) {
            return 0;
        }
        /* Out-of-range / sleeping / HW error on receiver slot:
         * Do NOT emit a synthetic "USB Receiver (offline)" device with slot-based ID;
         * previously-seen mice are already retained in sleeping state by merge.c with
         * their real name & unitId, avoiding duplicate phantom receiver entries! */
        if ((ch->last_err_is_v10 && ch->last_err_code == 0x09u) ||
            (!ch->last_err_is_v10 && (ch->last_err_code == 0x0Bu || ch->last_err_code == 0x04u))) {
            return 0;
        }
        /* Legacy HID++ 1.0 device behind Unifying receiver */
        if (ch->last_err_is_v10 && ch->last_err_code == 0x01u) {
            if (hidpp_read_v10_slot(ch, slot, out)) {
                snprintf(out->stable_id, sizeof(out->stable_id),
                         "hidpp10:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
                return 1;
            }
            return 0;
        }
    }

    if (!ping_ok) {
        if (hidpp_read_v10_slot(ch, slot, out)) {
            snprintf(out->stable_id, sizeof(out->stable_id),
                     "hidpp10:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
            return 1;
        }
        return 0;
    }

    /* Read identity early to classify MOUSE / KEYBOARD / HEADSET and skip wheels/gamepads */
    if (hidpp_read_identity(ch, slot, out->name, 64, &out->kind,
                            out->stable_id, sizeof(out->stable_id)) < 0) {
        return 0;
    }
    int prefer_headset = is_headset_hint || (out->kind == OCTANS_DEV_HEADSET);

    typedef struct {
        uint16_t feat;
        uint8_t fn;
        const char *tag;
    } batt_step_t;

    static const batt_step_t chain_default[5] = {
        {0x1004u, 1u, "hidpp_1004"},
        {0x1000u, 0u, "hidpp_1000"},
        {0x1001u, 0u, "hidpp_1001"},
        {0x1F20u, 0u, "hidpp_1f20"},
        {0x0104u, 0u, "hidpp_0104"},
    };
    static const batt_step_t chain_headset[5] = {
        {0x1F20u, 0u, "hidpp_1f20"},
        {0x1004u, 1u, "hidpp_1004"},
        {0x0104u, 0u, "hidpp_0104"},
        {0x1000u, 0u, "hidpp_1000"},
        {0x1001u, 0u, "hidpp_1001"},
    };
    const batt_step_t *chain = prefer_headset ? chain_headset : chain_default;

    int got_batt = 0;
    int headset_unlinked = 0;
    for (int i = 0; i < 5; ++i) {
        uint8_t fidx = 0;
        if (!hidpp_probe_feature(ch, slot, chain[i].feat, &fidx)) continue;

        int soc_supported = 1;
        if (chain[i].feat == 0x1004u) {
            uint8_t caps[16];
            if (hidpp_xfer(ch, slot, fidx, 0u, NULL, 0, 500u, caps)) {
                soc_supported = (caps[1] & 0x02u) ? 1 : 0;
            }
        }

        uint8_t ans[16];
        if (!hidpp_xfer(ch, slot, fidx, chain[i].fn, NULL, 0, 600u, ans)) {
            if (chain[i].feat == 0x1F20u && ch->last_err &&
                !ch->last_err_is_v10 && ch->last_err_code == 0x05u) {
                /* 0x05 LogitechInternal on 0x1F20: wireless headset dongle present, headset off */
                headset_unlinked = 1;
                break;
            }
            continue;
        }

        int parsed = 0;
        if (chain[i].feat == 0x1004u) {
            parsed = octans_hidpp_parse_unified_battery_ex(ans, 16, soc_supported, out);
        } else if (chain[i].feat == 0x1000u) {
            parsed = octans_hidpp_parse_battery_status(ans, 16, out);
        } else if (chain[i].feat == 0x1001u) {
            parsed = octans_hidpp_parse_battery_voltage(ans, 16, out);
        } else if (chain[i].feat == 0x1F20u) {
            parsed = octans_hidpp_parse_adc_battery(ans, 16, out);
            if (!parsed && (ans[2] & 0x01u) == 0u) {
                headset_unlinked = 1;
                break;
            }
        } else {
            parsed = octans_hidpp_parse_centpp_soc(ans, 16, out);
        }

        if (parsed) {
            snprintf(out->source_tag, sizeof(out->source_tag), "%s", chain[i].tag);
            got_batt = 1;
            break;
        }
    }

    if (headset_unlinked) {
        return 0; /* Headset turned off / not linked to dongle */
    }

    if (!got_batt) {
        if (!hidpp_read_v10_slot(ch, slot, out)) {
            return 0;
        }
    }

    if (out->stable_id[0] == '\0') {
        snprintf(out->stable_id, sizeof(out->stable_id),
                 "hidpp:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
    }
    if (!is_receiver && !is_headset_hint && !info->is_bluetooth &&
        out->power_state == OCTANS_PWR_DISCHARGING) {
        out->conn = OCTANS_CONN_WIRED;
    }
    return 1;
}

size_t octans_hidpp_poll_all(const octans_hid_info_t *ifaces,
                             size_t iface_count,
                             octans_proto_reading_t *out,
                             size_t max_out) {
    if (!ifaces || !out || max_out == 0) return 0;

    int seen_group[OCTANS_MAX_HID_IFACES];
    memset(seen_group, 0, sizeof(seen_group));
    size_t count = 0;

    for (size_t i = 0; i < iface_count && count < max_out; ++i) {
        if (ifaces[i].vid != OCTANS_LOGITECH_VID || seen_group[i]) continue;

        const octans_hid_info_t *long_if = NULL;
        const octans_hid_info_t *short_if = NULL;

        for (size_t j = i; j < iface_count; ++j) {
            if (ifaces[j].vid != OCTANS_LOGITECH_VID) continue;
            if (strcmp(ifaces[i].group_key, ifaces[j].group_key) != 0) continue;
            seen_group[j] = 1;

            if (ifaces[j].usage_page == 0xFF00u && ifaces[j].usage == 0x0002u && ifaces[j].output_len >= 20) {
                long_if = &ifaces[j];
            } else if (ifaces[j].usage_page == 0xFF00u && ifaces[j].usage == 0x0001u && ifaces[j].output_len >= 7) {
                short_if = &ifaces[j];
            } else if (ifaces[j].usage_page == 0xFF43u && ifaces[j].usage == 0x0202u && ifaces[j].output_len >= 20) {
                long_if = &ifaces[j];
            } else if (ifaces[j].pid == 0x0AC4u && ifaces[j].usage_page == 0x000Cu && ifaces[j].output_len >= 20) {
                long_if = &ifaces[j];
            }
        }

        if (!long_if) continue;

        hidpp_channel_t ch;
        memset(&ch, 0, sizeof(ch));
        if (octans_hid_open(long_if->path, 1, 1, &ch.long_dev) < 0 || !ch.long_dev.can_write) {
            octans_hid_close(&ch.long_dev);
            continue;
        }
        if (short_if && octans_hid_open(short_if->path, 1, 1, &ch.short_dev) == 0) {
            ch.has_short = 1;
        }

        wchar_t hs_name[64];
        hs_name[0] = L'\0';
        int is_hs = is_headset_pid(long_if->pid, hs_name, 64);
        int is_rx = (!is_hs && !long_if->is_bluetooth) &&
                    octans_hidpp_is_receiver_pid(long_if->pid, long_if->product);
        size_t added_this_group = 0;

        if (is_rx) {
            for (uint8_t slot = 1; slot <= 6 && count < max_out; ++slot) {
                octans_proto_reading_t r;
                if (hidpp_query_slot(&ch, long_if, slot, 1, 0, &r)) {
                    out[count++] = r;
                    ++added_this_group;
                }
            }
        } else if (long_if->is_bluetooth) {
            /* Bluetooth HID++ uses deviceIndex 0x00 (0xFF times out on BT). */
            octans_proto_reading_t r;
            if (hidpp_query_slot(&ch, long_if, 0x00u, 0, is_hs, &r) ||
                hidpp_query_slot(&ch, long_if, 0xFFu, 0, is_hs, &r)) {
                out[count++] = r;
                ++added_this_group;
            }
        } else {
            octans_proto_reading_t r;
            if (is_hs && (long_if->pid == 0x0AC4u || long_if->pid == 0x0A66u)) {
                /* G535/G533 headset 1.0 charge register over long report. */
                uint8_t req[20];
                memset(req, 0, sizeof(req));
                req[0] = 0x11u;
                req[1] = 0xFFu;
                req[2] = 0x05u;
                req[3] = 0x0Du;
                octans_hid_drain(&ch.long_dev, 8);
                if (octans_hid_write_output(&ch.long_dev, req, sizeof(req)) == 0) {
                    uint8_t resp[64];
                    int n = octans_hid_read_input(&ch.long_dev, resp, sizeof(resp), 400);
                    if (n >= 7 && resp[0] == 0x10u && resp[2] == 0x05u && resp[3] == 0x0Du) {
                        octans_reading_init(&r);
                        if (octans_hidpp_parse_headset_reg(resp, (size_t)n, &r)) {
                            r.vid = long_if->vid;
                            r.pid = long_if->pid;
                            r.kind = OCTANS_DEV_HEADSET;
                            r.conn = OCTANS_CONN_DONGLE_24G;
                            wcsncpy(r.brand, L"Logitech", 31);
                            wcsncpy(r.name, hs_name, 63);
                            snprintf(r.stable_id, sizeof(r.stable_id),
                                     "hidpp:%04x:%04x", long_if->vid, long_if->pid);
                            snprintf(r.source_tag, sizeof(r.source_tag), "hidpp_g535");
                            out[count++] = r;
                            ++added_this_group;
                        }
                    }
                }
            }
            if (added_this_group == 0 && count < max_out &&
                hidpp_query_slot(&ch, long_if, 0xFFu, 0, is_hs, &r)) {
                if (is_hs && hs_name[0]) {
                    r.kind = OCTANS_DEV_HEADSET;
                    r.conn = OCTANS_CONN_DONGLE_24G;
                    wcsncpy(r.name, hs_name, 63);
                }
                out[count++] = r;
                ++added_this_group;
            }
            /* Fallback: unknown Logitech dongle PID — probe receiver slots 1..6 */
            if (added_this_group == 0 && !is_hs) {
                for (uint8_t slot = 1; slot <= 6 && count < max_out; ++slot) {
                    if (hidpp_query_slot(&ch, long_if, slot, 1, 0, &r)) {
                        out[count++] = r;
                        ++added_this_group;
                    }
                }
            }
        }

        if (ch.has_short) octans_hid_close(&ch.short_dev);
        octans_hid_close(&ch.long_dev);
    }

    return count;
}
