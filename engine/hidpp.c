#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "hidpp.h"
#include "../core/hid_dev.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const uint8_t g_swids[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

int octans_hidpp_is_receiver_pid(uint16_t pid, const wchar_t *product) {
    static const uint16_t rx_pids[] = {
        0xC548,                                                 /* Bolt */
        0xC52B, 0xC532,                                         /* Unifying */
        0xC52F, 0xC518, 0xC51A, 0xC51B, 0xC521, 0xC525, 0xC526, /* Nano */
        0xC52E, 0xC531, 0xC534, 0xC535, 0xC537,
        0xC539, 0xC53A, 0xC53D, 0xC53F, 0xC541, 0xC543, 0xC545, 0xC547, 0xC54D /* Lightspeed */
    };
    for (size_t i = 0; i < sizeof(rx_pids) / sizeof(rx_pids[0]); ++i) {
        if (pid == rx_pids[i]) return 1;
    }
    if (product && product[0]) {
        for (const wchar_t *p = product; *p; ++p) {
            if (_wcsnicmp(p, L"receiver", 8) == 0) return 1;
        }
    }
    return 0;
}

static int is_headset_pid(uint16_t pid, wchar_t *out_name, size_t out_chars) {
    static const struct {
        uint16_t pid;
        const wchar_t *name;
    } headsets[] = {
        {0x0A66, L"Logitech G533"},
        {0x0AC4, L"Logitech G535"},
        {0x0A5C, L"Logitech G633"},
        {0x0A89, L"Logitech G635"},
        {0x0A5B, L"Logitech G933"},
        {0x0A87, L"Logitech G935"},
        {0x0AB5, L"Logitech G733"},
        {0x0AFE, L"Logitech G733"},
        {0x0B1F, L"Logitech G733"},
        {0x0AA7, L"Logitech G PRO"},
        {0x0AAA, L"Logitech G PRO X"},
        {0x0ABA, L"Logitech G PRO X"},
        {0x0AFB, L"Logitech G PRO X 2"},
        {0x0AFC, L"Logitech G PRO X 2"},
    };
    for (size_t i = 0; i < sizeof(headsets) / sizeof(headsets[0]); ++i) {
        if (pid == headsets[i].pid) {
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

int octans_hidpp_parse_unified_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    uint8_t flags = params[1];
    uint8_t status = params[2];

    int charging = (status >= 1 && status <= 4);
    if (pct > 0 && pct <= 100) {
        out->battery_percent = (int8_t)pct;
        out->is_estimated = 0;
    } else if (pct == 0) {
        int approx = -1;
        if (flags & 0x08u) approx = 90;
        else if (flags & 0x04u) approx = 50;
        else if (flags & 0x02u) approx = 20;
        else if (flags & 0x01u) approx = 5;
        if (approx < 0) return 0;
        out->battery_percent = (int8_t)approx;
        out->is_estimated = 1;
    } else {
        return 0;
    }

    if (status == 3) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (charging) {
        out->power_state = OCTANS_PWR_CHARGING;
    } else {
        out->power_state = OCTANS_PWR_DISCHARGING;
    }
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_battery_status(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    uint8_t status = params[2];
    if (pct == 0 || pct > 100) return 0;

    out->battery_percent = (int8_t)pct;
    out->is_estimated = 0;
    if (status == 3) {
        out->power_state = OCTANS_PWR_FULL;
    } else if (status >= 1 && status <= 4) {
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
    if (mv < 2500 || mv > 5000) return 0;

    uint8_t flags = params[2];
    int charging = (flags & 0x80u) != 0;
    int full = charging && ((flags & 0x03u) == 1);

    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
    out->is_estimated = 1;
    out->power_state = full ? OCTANS_PWR_FULL : (charging ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING);
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_adc_battery(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    if ((params[2] & 0x01u) == 0) return 0; /* headset disconnected */
    uint16_t mv = (uint16_t)(((uint16_t)params[0] << 8) | params[1]);
    if (mv < 2500 || mv > 5000) return 0;

    out->voltage_mv = mv;
    out->battery_percent = (int8_t)octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
    out->is_estimated = 1;
    out->power_state = (params[2] & 0x02u) ? OCTANS_PWR_CHARGING : OCTANS_PWR_DISCHARGING;
    out->valid = 1;
    return 1;
}

int octans_hidpp_parse_v10_reg0d(const uint8_t *params, size_t len, octans_proto_reading_t *out) {
    if (!params || len < 3 || !out) return 0;
    uint8_t pct = params[0];
    if (pct == 0 || pct > 100) return 0;

    uint8_t st_hi = params[2] & 0xF0u;
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

int octans_hidpp_parse_headset_reg(const uint8_t *frame, size_t len, octans_proto_reading_t *out) {
    if (!frame || len < 7 || !out) return 0;
    uint16_t mv = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    if (mv < 2500 || mv > 5000) return 0;
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
    uint8_t last_err_code;
} hidpp_channel_t;

static int hidpp_xfer(hidpp_channel_t *ch,
                      uint8_t slot,
                      uint8_t feat_idx,
                      uint8_t fn,
                      const uint8_t *params,
                      size_t param_len,
                      uint32_t timeout_ms,
                      uint8_t out_params[16]) {
    ch->last_err = 0;
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

static int hidpp_probe_feature(hidpp_channel_t *ch, uint8_t slot, uint16_t feature_id, uint8_t *out_idx) {
    uint8_t p[2] = {(uint8_t)(feature_id >> 8), (uint8_t)(feature_id & 0xFFu)};
    uint8_t ans[16];
    if (!hidpp_xfer(ch, slot, 0x00u, 0x00u, p, 2, 600, ans)) return 0;
    if (ans[0] == 0) return 0;
    *out_idx = ans[0];
    return 1;
}

static void hidpp_read_identity(hidpp_channel_t *ch,
                                uint8_t slot,
                                wchar_t *out_name,
                                size_t name_cap,
                                octans_device_kind_t *out_kind,
                                char *out_unitid,
                                size_t uid_cap) {
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
            octans_device_kind_t k = octans_hidpp_map_kind(ans[0]);
            if (k != OCTANS_DEV_UNKNOWN) *out_kind = k;
        }
    }

    uint8_t info_idx = 0;
    if (hidpp_probe_feature(ch, slot, 0x0003u, &info_idx)) {
        uint8_t ans[16];
        if (hidpp_xfer(ch, slot, info_idx, 0u, NULL, 0, 500, ans)) {
            snprintf(out_unitid, uid_cap, "hidpp:%02x%02x%02x%02x",
                     ans[1], ans[2], ans[3], ans[4]);
        }
    }
}

static int hidpp_read_v10_slot(hidpp_channel_t *ch, uint8_t slot, octans_proto_reading_t *r) {
    if (!ch->has_short) return 0;
    static const uint8_t regs[2] = {0x0Du, 0x07u};
    for (int i = 0; i < 2; ++i) {
        uint8_t req[7];
        octans_hidpp_build_short(slot, 0x81u, regs[i], req);
        octans_hid_drain(&ch->short_dev, 8);
        if (octans_hid_write_output(&ch->short_dev, req, sizeof(req)) < 0) continue;

        ULONGLONG deadline = GetTickCount64() + 500ULL;
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
                            octans_proto_reading_t *out) {
    octans_reading_init(out);
    out->vid = info->vid;
    out->pid = info->pid;
    out->conn = is_receiver ? OCTANS_CONN_DONGLE_24G : OCTANS_CONN_WIRED;
    wcsncpy(out->brand, L"Logitech", 31);
    out->brand[31] = L'\0';

    if (info->product[0]) {
        wcsncpy(out->name, info->product, 63);
        out->name[63] = L'\0';
    } else {
        wcscpy(out->name, L"Logitech Device");
    }

    uint8_t ping_ans[16];
    int ping_ok = hidpp_xfer(ch, slot, 0x00u, 0x01u, NULL, 0, is_receiver ? 1200u : 600u, ping_ans);
    if (!ping_ok && ch->last_err) {
        if (ch->last_err_code == 0x08u) {
            return 0; /* empty receiver slot */
        }
        if (ch->last_err_code == 0x09u || ch->last_err_code == 0x04u) {
            out->offline = 1;
            out->power_state = OCTANS_PWR_OFFLINE;
            snprintf(out->stable_id, sizeof(out->stable_id),
                     "hidpp:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
            snprintf(out->source_tag, sizeof(out->source_tag), "hidpp_offline");
            return 1;
        }
        if (ch->last_err_code == 0x01u) {
            if (hidpp_read_v10_slot(ch, slot, out)) {
                snprintf(out->stable_id, sizeof(out->stable_id),
                         "hidpp10:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
                return 1;
            }
            return 0;
        }
    }

    if (!ping_ok) {
        /* Try fallback register read on wired/headset or paired quiet device. */
        if (hidpp_read_v10_slot(ch, slot, out)) {
            snprintf(out->stable_id, sizeof(out->stable_id),
                     "hidpp10:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
            return 1;
        }
        return 0;
    }

    static const struct {
        uint16_t feat;
        uint8_t fn;
        const char *tag;
    } chain[4] = {
        {0x1004u, 1u, "hidpp_1004"},
        {0x1000u, 0u, "hidpp_1000"},
        {0x1001u, 0u, "hidpp_1001"},
        {0x1F20u, 0u, "hidpp_1f20"},
    };

    int got_batt = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t fidx = 0;
        if (!hidpp_probe_feature(ch, slot, chain[i].feat, &fidx)) continue;
        uint8_t ans[16];
        if (!hidpp_xfer(ch, slot, fidx, chain[i].fn, NULL, 0, 600u, ans)) continue;

        int parsed = 0;
        if (chain[i].feat == 0x1004u) parsed = octans_hidpp_parse_unified_battery(ans, 16, out);
        else if (chain[i].feat == 0x1000u) parsed = octans_hidpp_parse_battery_status(ans, 16, out);
        else if (chain[i].feat == 0x1001u) parsed = octans_hidpp_parse_battery_voltage(ans, 16, out);
        else parsed = octans_hidpp_parse_adc_battery(ans, 16, out);

        if (parsed) {
            snprintf(out->source_tag, sizeof(out->source_tag), "%s", chain[i].tag);
            got_batt = 1;
            break;
        }
    }

    if (!got_batt) {
        if (!hidpp_read_v10_slot(ch, slot, out)) {
            return 0;
        }
    }

    hidpp_read_identity(ch, slot, out->name, 64, &out->kind,
                        out->stable_id, sizeof(out->stable_id));
    if (out->stable_id[0] == '\0') {
        snprintf(out->stable_id, sizeof(out->stable_id),
                 "hidpp:%04x:%04x:slot%u", info->vid, info->pid, (unsigned)slot);
    }
    if (!is_receiver && out->power_state == OCTANS_PWR_DISCHARGING) {
        /* Direct USB cable connection implies wired link. */
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
        int is_hs = is_headset_pid(long_if->pid, hs_name, 64);
        int is_rx = (!is_hs) && octans_hidpp_is_receiver_pid(long_if->pid, long_if->product);

        if (is_rx) {
            for (uint8_t slot = 1; slot <= 6 && count < max_out; ++slot) {
                octans_proto_reading_t r;
                if (hidpp_query_slot(&ch, long_if, slot, 1, &r)) {
                    out[count++] = r;
                }
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
                        }
                    }
                }
            }
            if (count == 0 && hidpp_query_slot(&ch, long_if, 0xFFu, 0, &r)) {
                if (is_hs) {
                    r.kind = OCTANS_DEV_HEADSET;
                    r.conn = OCTANS_CONN_DONGLE_24G;
                    wcsncpy(r.name, hs_name, 63);
                }
                out[count++] = r;
            }
        }

        if (ch.has_short) octans_hid_close(&ch.short_dev);
        octans_hid_close(&ch.long_dev);
    }

    return count;
}
