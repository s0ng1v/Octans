#include "broad_scan.h"
#include "family_request.h"
#include "hidpp.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static uint64_t hash_group_key(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    if (!s) return h;
    for (; *s; ++s) {
        h ^= (uint8_t)(*s);
        h *= 1099511628211ULL;
    }
    return h;
}

static int cache_contains(const octans_broad_cache_t *c, uint64_t h) {
    if (!c) return 0;
    for (size_t i = 0; i < c->count && i < OCTANS_BROAD_CACHE_CAP; ++i) {
        if (c->group_hashes[i] == h) return 1;
    }
    return 0;
}

static void cache_insert(octans_broad_cache_t *c, uint64_t h) {
    if (!c || cache_contains(c, h)) return;
    if (c->count < OCTANS_BROAD_CACHE_CAP) {
        c->group_hashes[c->count++] = h;
    } else {
        memmove(&c->group_hashes[0], &c->group_hashes[1],
                (OCTANS_BROAD_CACHE_CAP - 1u) * sizeof(uint64_t));
        c->group_hashes[OCTANS_BROAD_CACHE_CAP - 1u] = h;
    }
}

void octans_broad_cache_clear(octans_broad_cache_t *c) {
    if (c) memset(c, 0, sizeof(*c));
}

const char *octans_kind_to_ini_str(octans_device_kind_t k) {
    switch (k) {
    case OCTANS_DEV_MOUSE:    return "mouse";
    case OCTANS_DEV_KEYBOARD: return "keyboard";
    case OCTANS_DEV_HEADSET:  return "headset";
    case OCTANS_DEV_EARBUDS:  return "earbuds";
    case OCTANS_DEV_GAMEPAD:  return "gamepad";
    case OCTANS_DEV_STYLUS:   return "stylus";
    case OCTANS_DEV_SPEAKER:  return "speaker";
    default:                  return "other";
    }
}

static octans_device_kind_t infer_kind(const octans_hid_info_t *info,
                                       const octans_hid_info_t *all,
                                       size_t count,
                                       octans_proto_variant_t var) {
    if (var == VAR_JIELI_TLV || var == VAR_INDEP_MCHOSE_V9 ||
        var == VAR_INDEP_MCHOSE_AUDIO_MC ||
        var == VAR_RAZER_PA_HEADSET || var == VAR_HX_GEN2 || var == VAR_HX_CLOUD3S) {
        return OCTANS_DEV_HEADSET;
    }
    if (var == VAR_KB_ATK_EVISION || var == VAR_KB_VIA_RAW ||
        var == VAR_KB_KEYCHRON || var == VAR_KB_SEMICO_AA55 ||
        var == VAR_KB_LOFREE_HYZEN || var == VAR_KB_MCHOSE_BY ||
        var == VAR_KB_LEOBOG_20B || var == VAR_KB_LEOBOG_BYTECH ||
        var == VAR_INDEP_AULA_KBD_AA) {
        return OCTANS_DEV_KEYBOARD;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(all[i].group_key, info->group_key) != 0) continue;
        if (all[i].usage_page == 0x0001u) {
            if (all[i].usage == 0x0002u) return OCTANS_DEV_MOUSE;
            if (all[i].usage == 0x0006u) return OCTANS_DEV_KEYBOARD;
            if (all[i].usage == 0x0005u) return OCTANS_DEV_GAMEPAD;
        }
    }
    return OCTANS_DEV_MOUSE;
}

void octans_broad_format_rule_ini(const octans_hid_info_t *info,
                                  octans_device_kind_t kind,
                                  const char *proto_name,
                                  int is_auto,
                                  wchar_t *out,
                                  size_t out_cap) {
    if (!info || !out || out_cap == 0) return;

    wchar_t disp[64];
    if (info->product[0]) {
        wcsncpy(disp, info->product, 63);
        disp[63] = L'\0';
    } else {
        _snwprintf(disp, 63, L"Device %04X:%04X", (unsigned)info->vid, (unsigned)info->pid);
        disp[63] = L'\0';
    }

    const char *kind_s = octans_kind_to_ini_str(kind);
    const char *pname = proto_name ? proto_name : "compx_a";

    _snwprintf(
        out, out_cap - 1,
        L"[Auto_%04X_%04X]\r\n"
        L"name = %ls\r\n"
        L"kind = %hs\r\n"
        L"vid = %04X\r\n"
        L"pid = %04X\r\n"
        L"usage_page = %04X\r\n"
        L"usage = %04X\r\n"
        L"proto = %hs\r\n"
        L"auto = %d\r\n",
        (unsigned)info->vid, (unsigned)info->pid,
        disp,
        kind_s,
        (unsigned)info->vid,
        (unsigned)info->pid,
        (unsigned)info->usage_page,
        (unsigned)info->usage,
        pname,
        is_auto ? 1 : 0
    );
    out[out_cap - 1] = L'\0';
}

static size_t select_candidates(const octans_hid_info_t *info,
                                octans_proto_variant_t out[6]) {
    size_t n = 0;
    uint16_t up = info->usage_page;
    uint16_t u  = info->usage;
    uint16_t in_l = info->input_len;
    uint16_t out_l = info->output_len;
    uint16_t feat_l = info->feature_len;

    if (up == 0xFF02u && out_l == 17u && in_l == 17u) {
        out[n++] = VAR_COMPX_A_OUTPUT;
        out[n++] = VAR_COMPX_B_FEATURE;
    } else if (up == 0xFF02u && out_l == 20u && in_l == 20u) {
        out[n++] = VAR_KB_LEOBOG_20B;
        out[n++] = VAR_KB_MCHOSE_BY;
    } else if (up == 0xFF02u && out_l >= 64u && in_l >= 64u) {
        out[n++] = VAR_JIELI_TLV;
        out[n++] = VAR_KB_SEMICO_AA55;
    } else if (up == 0xFF0Au && u == 0x0001u && out_l >= 64u && in_l >= 64u) {
        out[n++] = VAR_INDEP_DARMOSHARK_NORDIC;
    } else if ((up == 0xFFFFu || up == 0xFFA0u) && feat_l >= 65u) {
        out[n++] = VAR_SINO_65B;
        out[n++] = VAR_SINO_MCHOSE_A5;
    } else if (up == 0xFF05u && out_l >= 64u && in_l >= 64u) {
        out[n++] = VAR_ATK_54L15_64B;
    } else if (up == 0xFF05u && feat_l >= 64u) {
        out[n++] = VAR_INDEP_VAXEE;
    } else if (up == 0xFF1Cu && u == 0x0092u && out_l >= 32u) {
        out[n++] = VAR_KB_ATK_EVISION;
    } else if ((up == 0xFF01u || up == 0xFF0Bu) && out_l >= 61u && in_l >= 61u) {
        out[n++] = VAR_INDEP_MCHOSE_HID4D;
        if (feat_l >= 20u) out[n++] = VAR_INDEP_MCHOSE_M7;
        if (out_l >= 65u) out[n++] = VAR_INDEP_MCHOSE_G7;
        out[n++] = VAR_KB_SEMICO_AA55;
    } else if (up == 0xFF01u && feat_l >= 20u) {
        out[n++] = VAR_INDEP_MCHOSE_M7;
    } else if ((up == 0xFF00u || up == 0xFF90u) && out_l >= 64u && in_l >= 64u) {
        out[n++] = VAR_INDEP_MCHOSE_V9;
        out[n++] = VAR_INDEP_MCHOSE_AUDIO_MC;
        out[n++] = VAR_RAZER_PA_HEADSET;
    } else if (up == 0xFF60u && u == 0x0061u && out_l >= 32u && in_l >= 32u) {
        if (out_l >= 63u) out[n++] = VAR_KB_LEOBOG_BYTECH;
        out[n++] = VAR_KB_VIA_RAW;
    } else if (up == 0xFF80u && out_l >= 32u && in_l >= 32u) {
        out[n++] = VAR_INDEP_AULA_KBD_AA;
    } else if (up == 0xFFA0u && feat_l >= 32u && feat_l < 65u) {
        out[n++] = VAR_INDEP_NORDIC_GEN;
    } else if (up == 0xFF42u && out_l >= 64u && in_l >= 64u) {
        out[n++] = VAR_CORSAIR_BRAGI;
    }

    return n;
}

size_t octans_broad_scan_unknowns(const octans_hid_info_t *ifaces,
                                  size_t iface_count,
                                  int *group_hit,
                                  octans_broad_cache_t *cache,
                                  const wchar_t *auto_save_ini_path,
                                  int debug_log,
                                  octans_proto_reading_t *out_readings,
                                  size_t max_readings) {
    if (!ifaces || iface_count == 0 || !group_hit || !out_readings || max_readings == 0) return 0;

    size_t produced = 0;

    for (size_t i = 0; i < iface_count && produced < max_readings; ++i) {
        const octans_hid_info_t *info = &ifaces[i];
        if (group_hit[i] || info->vid == 0u || info->is_system_exclusive) continue;
        if (info->vid == OCTANS_LOGITECH_VID) continue;
        if (info->usage_page < 0xFF00u) continue;

        uint64_t gh = hash_group_key(info->group_key);
        if (cache_contains(cache, gh)) continue;

        octans_proto_variant_t cands[6];
        size_t cand_n = select_candidates(info, cands);
        if (cand_n == 0) continue;

        int hit_this_iface = 0;
        for (size_t c = 0; c < cand_n; ++c) {
            octans_user_rule_t probe_rule;
            memset(&probe_rule, 0, sizeof(probe_rule));
            snprintf(probe_rule.section, sizeof(probe_rule.section), "broad_%04x", (unsigned)info->vid);
            probe_rule.vid = info->vid;
            probe_rule.pid = info->pid;
            probe_rule.usage_page = info->usage_page;
            probe_rule.usage = info->usage;
            probe_rule.interface_num = (int16_t)info->interface_num;
            probe_rule.kind = infer_kind(info, ifaces, iface_count, cands[c]);
            probe_rule.conn = info->is_bluetooth ? OCTANS_CONN_BLUETOOTH : OCTANS_CONN_DONGLE_24G;
            probe_rule.template_variant = cands[c];
            probe_rule.is_auto = 1;

            octans_proto_reading_t r;
            if (octans_user_rule_query_iface(&probe_rule, info, 150u, &r, NULL, 0, NULL) &&
                r.valid && r.battery_percent >= 1 && r.battery_percent <= 100) {
                const char *tpl_name = octans_variant_to_template_name(cands[c]);
                snprintf(r.source_tag, sizeof(r.source_tag), "broad:%s", tpl_name ? tpl_name : "tpl");
                out_readings[produced++] = r;
                hit_this_iface = 1;

                for (size_t k = 0; k < iface_count; ++k) {
                    if (strcmp(ifaces[k].group_key, info->group_key) == 0) {
                        group_hit[k] = 1;
                    }
                }

                if (auto_save_ini_path && auto_save_ini_path[0] && tpl_name) {
                    wchar_t ini_buf[512];
                    octans_broad_format_rule_ini(info, probe_rule.kind, tpl_name, 1, ini_buf, 512);
                    octans_user_rules_save_section(auto_save_ini_path, ini_buf);
                }

                if (debug_log) {
                    fprintf(stderr, "[octans] broad_scan matched %04X:%04X via %s -> %d%%\n",
                            info->vid, info->pid, tpl_name ? tpl_name : "?", (int)r.battery_percent);
                }
                break;
            }
        }

        if (!hit_this_iface) {
            /* If no more matching unhit vendor interfaces remain in this group, negative-cache it. */
            int has_more_in_group = 0;
            for (size_t k = i + 1; k < iface_count; ++k) {
                if (strcmp(ifaces[k].group_key, info->group_key) == 0 &&
                    ifaces[k].usage_page >= 0xFF00u &&
                    !ifaces[k].is_system_exclusive) {
                    octans_proto_variant_t tmp[6];
                    if (select_candidates(&ifaces[k], tmp) > 0) {
                        has_more_in_group = 1;
                        break;
                    }
                }
            }
            if (!has_more_in_group) {
                cache_insert(cache, gh);
            }
        }
    }

    return produced;
}
