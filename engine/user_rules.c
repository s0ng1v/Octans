#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "user_rules.h"
#include "family_request.h"
#include "../core/hid_dev.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

typedef struct tpl_map_entry {
    const char *name;
    octans_proto_variant_t variant;
} tpl_map_entry_t;

static const tpl_map_entry_t k_tpl_map[] = {
    {"compx_a",     VAR_COMPX_A_OUTPUT},
    {"compx_b",     VAR_COMPX_B_FEATURE},
    {"sinowealth",  VAR_SINO_65B},
    {"mchose_a5",   VAR_SINO_MCHOSE_A5},
    {"atk_64b",     VAR_ATK_54L15_64B},
    {"atk_17b",     VAR_ATK_52840_17B},
    {"mchose_hid4d", VAR_INDEP_MCHOSE_HID4D},
    {"mchose_m7",    VAR_INDEP_MCHOSE_M7},
    {"mchose_g7",    VAR_INDEP_MCHOSE_G7},
    {"mchose_g7max", VAR_INDEP_MCHOSE_G7MAX},
    {"mchose_by",    VAR_KB_MCHOSE_BY},
    {"mchose_v9",    VAR_INDEP_MCHOSE_V9},
    {"mchose_mc",    VAR_INDEP_MCHOSE_AUDIO_MC},
    {"jieli",       VAR_JIELI_TLV},
    {"evision",     VAR_KB_ATK_EVISION},
    {"via",         VAR_KB_VIA_RAW},
    {"keychron",    VAR_KB_KEYCHRON},
    {"semico",      VAR_KB_SEMICO_AA55},
    {"lofree",      VAR_KB_LOFREE_HYZEN},
    {"rapoo_mouse", VAR_RAPOO_MOUSE_R7},
    {"rapoo_kb",    VAR_RAPOO_KB_BD},
    {"eweadn",      VAR_EWEADN_0X30},
    {"ajazz_10",    VAR_AJAZZ_0X10},
    {"ajazz_05",    VAR_AJAZZ_0X05},
    {"nordic_gen",  VAR_INDEP_NORDIC_GEN},
    {"razer_mouse", VAR_RAZER90_MOUSE},
    {"razer_pa",    VAR_RAZER_PA_HEADSET},
    {"hyperx_gen2", VAR_HX_GEN2},
    {"hyperx_c3s",  VAR_HX_CLOUD3S},
    {"steelseries", VAR_SS_AEROX},
    {"rog",         VAR_ROG_DIRECT_1207},
    {"corsair",     VAR_CORSAIR_BRAGI},
    {"vaxee",       VAR_INDEP_VAXEE},
    {"nzxt",        VAR_INDEP_NZXT},
    {"audeze",      VAR_INDEP_AUDEZE},
    {"leobog_20b",  VAR_KB_LEOBOG_20B},
    {"leobog_bytech", VAR_KB_LEOBOG_BYTECH},
    {"aula_kbd",    VAR_INDEP_AULA_KBD_AA},
    {"darmoshark_nordic", VAR_INDEP_DARMOSHARK_NORDIC},
    {"darmoshark_telink", VAR_INDEP_DARMOSHARK_TELINK},
    {"hecate_headset",    VAR_INDEP_HECATE_HEADSET},
    {"hecate_tws",        VAR_INDEP_HECATE_TWS},
    {"hecate_mouse04",    VAR_INDEP_HECATE_MOUSE_04},
    {"edifier_ec",        VAR_INDEP_EDIFIER_EC},
    {"edifier_kbm",       VAR_INDEP_EDIFIER_KBM}
};

const char *octans_variant_to_template_name(octans_proto_variant_t v) {
    for (size_t i = 0; i < sizeof(k_tpl_map) / sizeof(k_tpl_map[0]); ++i) {
        if (k_tpl_map[i].variant == v) return k_tpl_map[i].name;
    }
    return NULL;
}

octans_proto_variant_t octans_template_name_to_variant(const char *name) {
    if (!name || !name[0]) return VAR_NONE;
    for (size_t i = 0; i < sizeof(k_tpl_map) / sizeof(k_tpl_map[0]); ++i) {
        if (_stricmp(k_tpl_map[i].name, name) == 0) return k_tpl_map[i].variant;
    }
    return VAR_NONE;
}

static void trim_wstr(wchar_t *s) {
    if (!s) return;
    size_t start = 0;
    while (s[start] && (s[start] == L' ' || s[start] == L'\t' || s[start] == L'\r' || s[start] == L'\n')) {
        ++start;
    }
    size_t len = wcslen(s + start);
    if (start > 0) memmove(s, s + start, (len + 1) * sizeof(wchar_t));
    while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t' || s[len - 1] == L'\r' || s[len - 1] == L'\n')) {
        s[--len] = L'\0';
    }
}

static int parse_u16_hex_or_star(const char *s, uint16_t *out, uint16_t wildcard_val) {
    if (!s || !s[0]) return 0;
    if (strcmp(s, "*") == 0 || _stricmp(s, "any") == 0) {
        *out = wildcard_val;
        return 1;
    }
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 16);
    if (end == s || v > 0xFFFFul) return 0;
    *out = (uint16_t)v;
    return 1;
}

static size_t parse_hex_bytes(const char *s, uint8_t *out, size_t max_bytes) {
    if (!s || !out || max_bytes == 0) return 0;
    size_t count = 0;
    const char *p = s;
    while (*p && count < max_bytes) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ',' || *p == ':' || *p == '-')) ++p;
        if (!*p) break;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        if (!isxdigit((unsigned char)p[0])) break;
        unsigned int val = 0;
        if (isxdigit((unsigned char)p[1])) {
            char buf[3] = {p[0], p[1], '\0'};
            val = (unsigned int)strtoul(buf, NULL, 16);
            p += 2;
        } else {
            char buf[2] = {p[0], '\0'};
            val = (unsigned int)strtoul(buf, NULL, 16);
            p += 1;
        }
        out[count++] = (uint8_t)val;
    }
    return count;
}

static octans_device_kind_t parse_kind_str(const char *s) {
    if (!s) return OCTANS_DEV_OTHER;
    if (_stricmp(s, "mouse") == 0) return OCTANS_DEV_MOUSE;
    if (_stricmp(s, "keyboard") == 0 || _stricmp(s, "kb") == 0) return OCTANS_DEV_KEYBOARD;
    if (_stricmp(s, "headset") == 0) return OCTANS_DEV_HEADSET;
    if (_stricmp(s, "earbuds") == 0) return OCTANS_DEV_EARBUDS;
    if (_stricmp(s, "gamepad") == 0) return OCTANS_DEV_GAMEPAD;
    if (_stricmp(s, "stylus") == 0) return OCTANS_DEV_STYLUS;
    if (_stricmp(s, "speaker") == 0) return OCTANS_DEV_SPEAKER;
    return OCTANS_DEV_OTHER;
}

static octans_conn_kind_t parse_conn_str(const char *s) {
    if (!s) return OCTANS_CONN_DONGLE_24G;
    if (_stricmp(s, "bluetooth") == 0 || _stricmp(s, "bt") == 0) return OCTANS_CONN_BLUETOOTH;
    if (_stricmp(s, "wired") == 0 || _stricmp(s, "usb") == 0) return OCTANS_CONN_WIRED;
    return OCTANS_CONN_DONGLE_24G;
}

static void init_user_rule(octans_user_rule_t *r, const char *section) {
    memset(r, 0, sizeof(*r));
    if (section) {
        strncpy(r->section, section, sizeof(r->section) - 1);
    }
    r->pid = 0xFFFFu;
    r->interface_num = -1;
    r->kind = OCTANS_DEV_OTHER;
    r->conn = OCTANS_CONN_DONGLE_24G;
    r->tx = TX_OUTPUT_REPORT;
    r->batt_offset = -1;
    r->batt_mode = RULE_BATT_PERCENT;
    r->chg_offset = -1;
    r->chg_mask = 0x01u;
    r->chg_eq = -1;
}

static void apply_rule_kv(octans_user_rule_t *r, const wchar_t *wkey, const wchar_t *wval) {
    char key[64];
    char val[256];
    octans_wide_to_utf8(wkey, key, sizeof(key));
    octans_wide_to_utf8(wval, val, sizeof(val));

    if (_stricmp(key, "name") == 0) {
        wcsncpy(r->name, wval, 63);
        r->name[63] = L'\0';
    } else if (_stricmp(key, "brand") == 0) {
        wcsncpy(r->brand, wval, 31);
        r->brand[31] = L'\0';
    } else if (_stricmp(key, "vid") == 0) {
        parse_u16_hex_or_star(val, &r->vid, 0u);
    } else if (_stricmp(key, "pid") == 0) {
        parse_u16_hex_or_star(val, &r->pid, 0xFFFFu);
    } else if (_stricmp(key, "usage_page") == 0) {
        parse_u16_hex_or_star(val, &r->usage_page, 0u);
    } else if (_stricmp(key, "usage") == 0) {
        parse_u16_hex_or_star(val, &r->usage, 0u);
    } else if (_stricmp(key, "interface") == 0) {
        r->interface_num = (_stricmp(val, "*") == 0) ? -1 : (int16_t)strtol(val, NULL, 10);
    } else if (_stricmp(key, "kind") == 0) {
        r->kind = parse_kind_str(val);
    } else if (_stricmp(key, "conn") == 0) {
        r->conn = parse_conn_str(val);
    } else if (_stricmp(key, "auto") == 0) {
        r->is_auto = (atoi(val) != 0);
    } else if (_stricmp(key, "proto") == 0 || _stricmp(key, "template") == 0) {
        r->template_variant = octans_template_name_to_variant(val);
    } else if (_stricmp(key, "tx") == 0) {
        if (_stricmp(val, "feature") == 0) r->tx = TX_FEATURE;
        else if (_stricmp(val, "output_intr") == 0 || _stricmp(val, "intr") == 0) r->tx = TX_OUTPUT_INTR;
        else if (_stricmp(val, "listen") == 0) r->tx = TX_LISTEN;
        else r->tx = TX_OUTPUT_REPORT;
    } else if (_stricmp(key, "req_hex") == 0) {
        r->req_hex_len = parse_hex_bytes(val, r->req, sizeof(r->req));
    } else if (_stricmp(key, "req_len") == 0) {
        long n = strtol(val, NULL, 10);
        if (n >= 0 && n <= (long)sizeof(r->req)) r->req_len = (size_t)n;
    } else if (_stricmp(key, "checksum") == 0) {
        if (_stricmp(val, "xor_all") == 0 || _stricmp(val, "xor") == 0) r->checksum_kind = RULE_CKSUM_XOR_ALL;
        else if (_stricmp(val, "compx") == 0 || _stricmp(val, "compx_55") == 0) r->checksum_kind = RULE_CKSUM_COMPX_55;
        else if (_stricmp(val, "sum_mod256") == 0 || _stricmp(val, "sum") == 0) r->checksum_kind = RULE_CKSUM_SUM_MOD256;
        else r->checksum_kind = RULE_CKSUM_NONE;
    } else if (_stricmp(key, "resp_head") == 0) {
        r->resp_head_len = parse_hex_bytes(val, r->resp_head, sizeof(r->resp_head));
    } else if (_stricmp(key, "resp_mask") == 0) {
        r->resp_mask_len = parse_hex_bytes(val, r->resp_mask, sizeof(r->resp_mask));
    } else if (_stricmp(key, "batt_offset") == 0) {
        r->batt_offset = (int)strtol(val, NULL, 10);
    } else if (_stricmp(key, "batt_mode") == 0) {
        if (_stricmp(val, "step10") == 0) r->batt_mode = RULE_BATT_STEP10;
        else if (_stricmp(val, "step5") == 0) r->batt_mode = RULE_BATT_STEP5;
        else if (_stricmp(val, "scale255") == 0) r->batt_mode = RULE_BATT_SCALE255;
        else if (_stricmp(val, "mv_be") == 0) r->batt_mode = RULE_BATT_MV_BE;
        else if (_stricmp(val, "mv_le") == 0) r->batt_mode = RULE_BATT_MV_LE;
        else r->batt_mode = RULE_BATT_PERCENT;
    } else if (_stricmp(key, "chg_offset") == 0) {
        r->chg_offset = (int)strtol(val, NULL, 10);
    } else if (_stricmp(key, "chg_mask") == 0) {
        r->chg_mask = (uint8_t)strtoul(val, NULL, 16);
    } else if (_stricmp(key, "chg_eq") == 0) {
        r->chg_eq = (int)strtoul(val, NULL, 16);
    }
}

static int rule_is_usable(const octans_user_rule_t *r) {
    if (!r || r->vid == 0u) return 0;
    if (r->template_variant != VAR_NONE) return 1;
    if (r->batt_offset >= 0 && r->batt_offset < 96) return 1;
    return 0;
}

size_t octans_user_rules_parse_text(const wchar_t *ini_text,
                                    octans_user_rule_t *out,
                                    size_t max_rules) {
    if (!ini_text || !out || max_rules == 0) return 0;

    size_t count = 0;
    octans_user_rule_t cur;
    int in_section = 0;

    const wchar_t *p = ini_text;
    while (*p) {
        const wchar_t *eol = p;
        while (*eol && *eol != L'\r' && *eol != L'\n') ++eol;

        size_t line_len = (size_t)(eol - p);
        wchar_t line[512];
        if (line_len >= 512) line_len = 511;
        wmemcpy(line, p, line_len);
        line[line_len] = L'\0';

        p = eol;
        while (*p == L'\r' || *p == L'\n') ++p;

        trim_wstr(line);
        if (line[0] == L'\0' || line[0] == L';' || line[0] == L'#') continue;

        /* Strip inline ';' comments only if not inside section header */
        if (line[0] == L'[') {
            wchar_t *rb = wcschr(line, L']');
            if (!rb) continue;
            *rb = L'\0';
            if (in_section && rule_is_usable(&cur) && count < max_rules) {
                out[count++] = cur;
            }
            char sec_u8[64];
            octans_wide_to_utf8(line + 1, sec_u8, sizeof(sec_u8));
            init_user_rule(&cur, sec_u8);
            in_section = 1;
            continue;
        }

        if (!in_section) continue;
        wchar_t *eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = L'\0';
        wchar_t *wkey = line;
        wchar_t *wval = eq + 1;

        /* Strip trailing ';' comment from value */
        wchar_t *semi = wcschr(wval, L';');
        if (semi) *semi = L'\0';
        trim_wstr(wkey);
        trim_wstr(wval);
        if (wkey[0]) {
            apply_rule_kv(&cur, wkey, wval);
        }
    }

    if (in_section && rule_is_usable(&cur) && count < max_rules) {
        out[count++] = cur;
    }
    return count;
}

size_t octans_user_rules_load_file(const wchar_t *ini_path,
                                   octans_user_rule_t *out,
                                   size_t max_rules) {
    if (!ini_path || !out || max_rules == 0) return 0;

    HANDLE hf = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return 0;

    LARGE_INTEGER fsz;
    if (!GetFileSizeEx(hf, &fsz) || fsz.QuadPart <= 0 || fsz.QuadPart > 262144) {
        CloseHandle(hf);
        return 0;
    }

    size_t raw_sz = (size_t)fsz.QuadPart;
    uint8_t *raw = (uint8_t *)malloc(raw_sz + 4u);
    if (!raw) {
        CloseHandle(hf);
        return 0;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hf, raw, (DWORD)raw_sz, &bytes_read, NULL);
    CloseHandle(hf);
    if (!ok || bytes_read == 0) {
        free(raw);
        return 0;
    }
    raw[bytes_read] = 0;
    raw[bytes_read + 1] = 0;
    raw[bytes_read + 2] = 0;
    raw[bytes_read + 3] = 0;

    wchar_t *wbuf = NULL;
    if (bytes_read >= 2 && raw[0] == 0xFFu && raw[1] == 0xFEu) {
        /* UTF-16 LE BOM (standard Windows INI Unicode format) */
        size_t wchars = (bytes_read - 2u) / 2u;
        wbuf = (wchar_t *)malloc((wchars + 1u) * sizeof(wchar_t));
        if (wbuf) {
            memcpy(wbuf, raw + 2, wchars * sizeof(wchar_t));
            wbuf[wchars] = L'\0';
        }
    } else {
        const char *src = (const char *)raw;
        size_t src_len = bytes_read;
        if (bytes_read >= 3 && raw[0] == 0xEFu && raw[1] == 0xBBu && raw[2] == 0xBFu) {
            src += 3;
            src_len -= 3;
        }
        int cp_used = CP_UTF8;
        int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, (int)src_len, NULL, 0);
        if (needed <= 0) {
            cp_used = CP_ACP;
            needed = MultiByteToWideChar(CP_ACP, 0, src, (int)src_len, NULL, 0);
        }
        if (needed > 0) {
            wbuf = (wchar_t *)malloc((size_t)(needed + 1) * sizeof(wchar_t));
            if (wbuf) {
                MultiByteToWideChar(cp_used, 0, src, (int)src_len, wbuf, needed);
                wbuf[needed] = L'\0';
            }
        }
    }

    free(raw);
    if (!wbuf) return 0;

    size_t n = octans_user_rules_parse_text(wbuf, out, max_rules);
    free(wbuf);
    return n;
}

int octans_user_rule_matches_iface(const octans_user_rule_t *r,
                                   const octans_hid_info_t *info) {
    if (!r || !info) return 0;
    if (r->vid != 0xFFFFu && info->vid != r->vid) return 0;
    if (r->pid != 0xFFFFu && info->pid != r->pid) return 0;
    if (r->usage_page != 0u && info->usage_page != r->usage_page) return 0;
    if (r->usage != 0u && info->usage != r->usage) return 0;
    if (r->interface_num >= 0 && info->interface_num != r->interface_num) return 0;
    return 1;
}

int octans_user_rule_build_req(const octans_user_rule_t *r,
                               size_t iface_report_len,
                               uint8_t *out_buf,
                               size_t out_cap,
                               size_t *out_len) {
    if (!r || !out_buf || out_cap == 0 || !out_len) return 0;
    size_t send_len = r->req_len;
    if (send_len == 0) send_len = iface_report_len;
    if (send_len < r->req_hex_len) send_len = r->req_hex_len;
    if (send_len == 0 || send_len > out_cap) return 0;

    memset(out_buf, 0, send_len);
    if (r->req_hex_len > 0) {
        memcpy(out_buf, r->req, r->req_hex_len);
    }

    if (r->checksum_kind == RULE_CKSUM_XOR_ALL && send_len >= 2u) {
        out_buf[send_len - 1u] = octans_crc_xor(out_buf, 0u, send_len - 2u);
    } else if (r->checksum_kind == RULE_CKSUM_COMPX_55 && send_len >= 17u) {
        out_buf[16] = octans_crc_compx(out_buf);
    } else if (r->checksum_kind == RULE_CKSUM_SUM_MOD256 && send_len >= 2u) {
        out_buf[send_len - 1u] = octans_crc_sum_range(out_buf, 0u, send_len - 2u);
    }

    *out_len = send_len;
    return 1;
}

int octans_user_rule_parse_reply(const octans_user_rule_t *r,
                                 const uint8_t *buf,
                                 size_t len,
                                 octans_proto_reading_t *out) {
    if (!r || !buf || !out || r->batt_offset < 0) return 0;
    if (r->resp_head_len > 0) {
        if (len < r->resp_head_len) return 0;
        for (size_t i = 0; i < r->resp_head_len; ++i) {
            uint8_t mask = (i < r->resp_mask_len) ? r->resp_mask[i] : 0xFFu;
            if ((buf[i] & mask) != (r->resp_head[i] & mask)) return 0;
        }
    }

    size_t off = (size_t)r->batt_offset;
    if (off >= len) return 0;

    int pct = -1;
    uint16_t mv = 0;
    uint8_t est = 0;

    switch (r->batt_mode) {
    case RULE_BATT_PERCENT:
        if (buf[off] > 100u) return 0;
        pct = (int)buf[off];
        break;
    case RULE_BATT_STEP10:
        if (buf[off] > 10u) return 0;
        pct = (int)buf[off] * 10;
        est = 1;
        break;
    case RULE_BATT_STEP5:
        if (buf[off] > 20u) return 0;
        pct = (int)buf[off] * 5;
        est = 1;
        break;
    case RULE_BATT_SCALE255:
        pct = octans_clamp_percent(octans_round_div((uint32_t)buf[off] * 100u, 255u));
        break;
    case RULE_BATT_MV_BE:
        if (off + 1u >= len) return 0;
        mv = (uint16_t)(((uint16_t)buf[off] << 8) | buf[off + 1u]);
        if (mv < 2500u || mv > 4600u) return 0;
        pct = octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
        est = 1;
        break;
    case RULE_BATT_MV_LE:
        if (off + 1u >= len) return 0;
        mv = (uint16_t)(((uint16_t)buf[off + 1u] << 8) | buf[off]);
        if (mv < 2500u || mv > 4600u) return 0;
        pct = octans_mv_to_percent(mv, CURVE_SOLAAR_13PT);
        est = 1;
        break;
    default:
        return 0;
    }

    out->battery_percent = (int8_t)pct;
    out->voltage_mv = mv;
    out->is_estimated = est;
    out->power_state = OCTANS_PWR_DISCHARGING;

    if (r->chg_offset >= 0 && (size_t)r->chg_offset < len) {
        uint8_t cb = buf[(size_t)r->chg_offset];
        int chg = (r->chg_eq >= 0) ? (cb == (uint8_t)r->chg_eq) : ((cb & r->chg_mask) != 0u);
        if (chg) {
            out->power_state = (pct >= 100) ? OCTANS_PWR_FULL : OCTANS_PWR_CHARGING;
        }
    }

    out->valid = 1;
    return 1;
}

static int find_template_entry(octans_proto_variant_t var, proto_entry_t *out) {
    for (size_t i = 0; i < g_protocols_count; ++i) {
        if (g_protocols[i].variant == var) {
            *out = g_protocols[i];
            return 1;
        }
    }
    return 0;
}

static void init_reading_from_rule(const octans_user_rule_t *r,
                                   const octans_hid_info_t *info,
                                   octans_proto_reading_t *out) {
    octans_reading_init(out);
    out->vid = info->vid;
    out->pid = info->pid;
    out->kind = r->kind;
    out->conn = info->is_bluetooth ? OCTANS_CONN_BLUETOOTH : r->conn;
    if (r->brand[0]) {
        wcsncpy(out->brand, r->brand, 31);
        out->brand[31] = L'\0';
    } else if (info->manufacturer[0]) {
        wcsncpy(out->brand, info->manufacturer, 31);
        out->brand[31] = L'\0';
    }
    if (r->name[0]) {
        wcsncpy(out->name, r->name, 63);
        out->name[63] = L'\0';
    } else if (info->product[0]) {
        wcsncpy(out->name, info->product, 63);
        out->name[63] = L'\0';
    } else {
        _snwprintf(out->name, 64, L"HID %04X:%04X", (unsigned)info->vid, (unsigned)info->pid);
    }
    snprintf(out->stable_id, sizeof(out->stable_id),
             "usr:%04x:%04x:%s", (unsigned)info->vid, (unsigned)info->pid, r->section);
    snprintf(out->source_tag, sizeof(out->source_tag), "rule:%s", r->section);
}

int octans_user_rule_query_iface(const octans_user_rule_t *r,
                                 const octans_hid_info_t *info,
                                 uint32_t listen_ms,
                                 octans_proto_reading_t *out,
                                 uint8_t *raw_reply_out,
                                 size_t raw_reply_cap,
                                 size_t *raw_reply_len) {
    if (!r || !info || !out) return 0;
    if (raw_reply_len) *raw_reply_len = 0;

    if (r->template_variant != VAR_NONE) {
        proto_entry_t pe;
        if (!find_template_entry(r->template_variant, &pe)) return 0;
        pe.vid = info->vid;
        pe.pid = info->pid;
        pe.usage_page = info->usage_page;
        pe.usage = info->usage;
        pe.interface_num = (int16_t)info->interface_num;
        pe.kind = r->kind;
        pe.conn = r->conn;
        if (r->brand[0]) pe.brand = r->brand;
        if (r->name[0]) pe.name = r->name;
        int res = octans_proto_query_iface(&pe, info, listen_ms, 0, out);
        if (res) {
            if (r->name[0]) {
                wcsncpy(out->name, r->name, 63);
                out->name[63] = L'\0';
            } else if (info->product[0]) {
                wcsncpy(out->name, info->product, 63);
                out->name[63] = L'\0';
            }
            if (r->brand[0]) {
                wcsncpy(out->brand, r->brand, 31);
                out->brand[31] = L'\0';
            }
        }
        return res;
    }

    if (info->is_system_exclusive) return 0;
    init_reading_from_rule(r, info, out);

    int want_write = (r->tx != TX_LISTEN && r->tx != TX_FEATURE && r->req_hex_len > 0) ? 1 : 0;
    int want_read = (r->tx != TX_FEATURE || info->input_len > 0) ? 1 : 0;

    octans_hid_dev_t dev;
    if (octans_hid_open(info->path, want_write, want_read, &dev) < 0) {
        return 0;
    }
    if (want_write && !dev.can_write) {
        octans_hid_close(&dev);
        return 0;
    }

    int ok = 0;
    uint8_t resp[128];

    if (r->tx == TX_LISTEN) {
        uint64_t deadline = GetTickCount64() + (listen_ms ? listen_ms : 250u);
        while (GetTickCount64() < deadline) {
            uint64_t rem = deadline - GetTickCount64();
            int n = octans_hid_read_input(&dev, resp, sizeof(resp), (uint32_t)(rem > 80u ? 80u : rem));
            if (n > 0) {
                if (raw_reply_out && raw_reply_len && *raw_reply_len == 0) {
                    size_t cp = (size_t)n < raw_reply_cap ? (size_t)n : raw_reply_cap;
                    memcpy(raw_reply_out, resp, cp);
                    *raw_reply_len = cp;
                }
                if (octans_user_rule_parse_reply(r, resp, (size_t)n, out)) {
                    ok = 1;
                    break;
                }
            }
        }
    } else if (r->tx == TX_FEATURE) {
        size_t flen = r->req_len ? r->req_len : (dev.feature_len ? dev.feature_len : 64u);
        if (flen > sizeof(resp)) flen = sizeof(resp);
        uint8_t req[128];
        size_t send_len = 0;
        uint8_t get_rid = (r->resp_head_len > 0)
            ? r->resp_head[0]
            : (r->req_hex_len > 0 ? r->req[0] : 0x00u);
        if (r->req_hex_len > 0) {
            if (dev.can_read) octans_hid_drain(&dev, 16);
            if (octans_user_rule_build_req(r, flen, req, sizeof(req), &send_len) &&
                octans_hid_set_feature(&dev, req, send_len) == 0) {
                Sleep(60);
                int n = octans_hid_get_feature(&dev, get_rid, resp, flen);
                if (n <= 0 && get_rid != req[0]) {
                    n = octans_hid_get_feature(&dev, req[0], resp, flen);
                }
                if (n > 0) {
                    if (raw_reply_out && raw_reply_len && *raw_reply_len == 0) {
                        size_t cp = (size_t)n < raw_reply_cap ? (size_t)n : raw_reply_cap;
                        memcpy(raw_reply_out, resp, cp);
                        *raw_reply_len = cp;
                    }
                    if (octans_user_rule_parse_reply(r, resp, (size_t)n, out)) ok = 1;
                }
                if (!ok && dev.can_read) {
                    for (int t = 0; t < 3; ++t) {
                        int rn = octans_hid_read_input(&dev, resp, sizeof(resp), 120u);
                        if (rn > 0) {
                            if (raw_reply_out && raw_reply_len && *raw_reply_len == 0) {
                                size_t cp = (size_t)rn < raw_reply_cap ? (size_t)rn : raw_reply_cap;
                                memcpy(raw_reply_out, resp, cp);
                                *raw_reply_len = cp;
                            }
                            if (octans_user_rule_parse_reply(r, resp, (size_t)rn, out)) {
                                ok = 1;
                                break;
                            }
                        }
                    }
                }
            }
        } else if (dev.feature_len > 0) {
            int n = octans_hid_get_feature(&dev, get_rid, resp, flen);
            if (n > 0) {
                if (raw_reply_out && raw_reply_len && *raw_reply_len == 0) {
                    size_t cp = (size_t)n < raw_reply_cap ? (size_t)n : raw_reply_cap;
                    memcpy(raw_reply_out, resp, cp);
                    *raw_reply_len = cp;
                }
                if (octans_user_rule_parse_reply(r, resp, (size_t)n, out)) ok = 1;
            }
        }
    } else {
        /* TX_OUTPUT_REPORT or TX_OUTPUT_INTR */
        if (dev.can_read) octans_hid_drain(&dev, 32);
        size_t olen = r->req_len ? r->req_len : (dev.output_len ? dev.output_len : 64u);
        if (olen > 128u) olen = 128u;
        uint8_t req[128];
        size_t send_len = 0;
        if (octans_user_rule_build_req(r, olen, req, sizeof(req), &send_len)) {
            int wrc = (r->tx == TX_OUTPUT_INTR)
                ? octans_hid_write_output_intr(&dev, req, send_len)
                : octans_hid_write_output(&dev, req, send_len);
            if (wrc == 0 && dev.can_read) {
                for (int t = 0; t < 4; ++t) {
                    int n = octans_hid_read_input(&dev, resp, sizeof(resp), 140u);
                    if (n > 0) {
                        if (raw_reply_out && raw_reply_len && *raw_reply_len == 0) {
                            size_t cp = (size_t)n < raw_reply_cap ? (size_t)n : raw_reply_cap;
                            memcpy(raw_reply_out, resp, cp);
                            *raw_reply_len = cp;
                        }
                        if (octans_user_rule_parse_reply(r, resp, (size_t)n, out)) {
                            ok = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    octans_hid_close(&dev);
    return ok;
}

static void append_hex_w(const uint8_t *buf, size_t len, wchar_t *out, size_t out_cap) {
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 4 < out_cap; ++i) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, i == 0 ? L"%02X" : L" %02X", (unsigned)buf[i]);
    }
    if (pos < out_cap) out[pos] = L'\0';
    else if (out_cap > 0) out[out_cap - 1] = L'\0';
}

int octans_user_rule_format_ini(const octans_user_rule_t *r, wchar_t *out, size_t out_cap) {
    if (!r || !out || out_cap < 64) return -1;

    const char *sec = r->section[0] ? r->section : "Learned_Device";
    const char *kind_s = "other";
    switch (r->kind) {
    case OCTANS_DEV_MOUSE:    kind_s = "mouse"; break;
    case OCTANS_DEV_KEYBOARD: kind_s = "keyboard"; break;
    case OCTANS_DEV_HEADSET:  kind_s = "headset"; break;
    case OCTANS_DEV_EARBUDS:  kind_s = "earbuds"; break;
    case OCTANS_DEV_GAMEPAD:  kind_s = "gamepad"; break;
    case OCTANS_DEV_STYLUS:   kind_s = "stylus"; break;
    case OCTANS_DEV_SPEAKER:  kind_s = "speaker"; break;
    default: break;
    }

    size_t pos = 0;
    pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"[%hs]\r\n", sec);
    if (r->name[0] && pos + 16 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"name = %ls\r\n", r->name);
    }
    if (pos + 64 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos,
            L"kind = %hs\r\nvid = %04X\r\npid = %04X\r\n",
            kind_s, (unsigned)r->vid, (unsigned)r->pid);
    }
    if (r->usage_page != 0 && pos + 32 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"usage_page = %04X\r\n", (unsigned)r->usage_page);
    }
    if (r->usage != 0 && pos + 32 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"usage = %04X\r\n", (unsigned)r->usage);
    }
    if (r->interface_num >= 0 && pos + 32 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"interface = %d\r\n", (int)r->interface_num);
    }

    if (r->template_variant != VAR_NONE) {
        const char *tn = octans_variant_to_template_name(r->template_variant);
        if (tn && pos + 48 < out_cap) {
            pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"proto = %hs\r\n", tn);
        }
        out[out_cap - 1] = L'\0';
        return 0;
    }

    const char *tx_s = "output";
    if (r->tx == TX_FEATURE) tx_s = "feature";
    else if (r->tx == TX_OUTPUT_INTR) tx_s = "output_intr";
    else if (r->tx == TX_LISTEN) tx_s = "listen";

    const char *bm_s = "percent";
    if (r->batt_mode == RULE_BATT_STEP10) bm_s = "step10";
    else if (r->batt_mode == RULE_BATT_STEP5) bm_s = "step5";
    else if (r->batt_mode == RULE_BATT_SCALE255) bm_s = "scale255";
    else if (r->batt_mode == RULE_BATT_MV_BE) bm_s = "mv_be";
    else if (r->batt_mode == RULE_BATT_MV_LE) bm_s = "mv_le";

    if (pos + 32 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"tx = %hs\r\n", tx_s);
    }
    if (r->req_hex_len > 0 && pos + 32 < out_cap) {
        wchar_t hex[320];
        append_hex_w(r->req, r->req_hex_len, hex, 320);
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"req_hex = %ls\r\n", hex);
    }
    if (r->req_len > 0 && pos + 32 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"req_len = %u\r\n", (unsigned)r->req_len);
    }
    if (r->resp_head_len > 0 && pos + 32 < out_cap) {
        wchar_t hex[80];
        append_hex_w(r->resp_head, r->resp_head_len, hex, 80);
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"resp_head = %ls\r\n", hex);
    }
    if (r->resp_mask_len > 0 && pos + 32 < out_cap) {
        int has_zero = 0;
        for (size_t i = 0; i < r->resp_mask_len; ++i) {
            if (r->resp_mask[i] != 0xFFu) { has_zero = 1; break; }
        }
        if (has_zero) {
            wchar_t hex[80];
            append_hex_w(r->resp_mask, r->resp_mask_len, hex, 80);
            pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"resp_mask = %ls\r\n", hex);
        }
    }
    if (pos + 64 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos,
            L"batt_offset = %d\r\nbatt_mode = %hs\r\n",
            r->batt_offset, bm_s);
    }
    if (r->chg_offset >= 0 && pos + 64 < out_cap) {
        pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"chg_offset = %d\r\n", r->chg_offset);
        if (r->chg_eq >= 0) {
            pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"chg_eq = %02X\r\n", (unsigned)r->chg_eq);
        } else {
            pos += (size_t)_snwprintf(out + pos, out_cap - pos, L"chg_mask = %02X\r\n", (unsigned)r->chg_mask);
        }
    }
    out[out_cap - 1] = L'\0';
    return 0;
}

int octans_user_rules_save_section(const wchar_t *ini_path, const wchar_t *rule_text) {
    if (!ini_path || !ini_path[0] || !rule_text || !rule_text[0]) return -1;

    /* Ensure parent directory exists */
    wchar_t dir[MAX_PATH];
    wcsncpy(dir, ini_path, MAX_PATH - 1);
    dir[MAX_PATH - 1] = L'\0';
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) {
        *slash = L'\0';
        CreateDirectoryW(dir, NULL);
    }

    /* Create with UTF-16 LE BOM if absent so WritePrivateProfileStringW preserves Unicode names. */
    if (GetFileAttributesW(ini_path) == INVALID_FILE_ATTRIBUTES) {
        HANDLE hf = CreateFileW(ini_path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf != INVALID_HANDLE_VALUE) {
            static const uint8_t bom[2] = {0xFFu, 0xFEu};
            DWORD wr = 0;
            WriteFile(hf, bom, 2u, &wr, NULL);
            CloseHandle(hf);
        }
    }

    wchar_t cur_sec[64] = {0};
    int wrote_any = 0;
    const wchar_t *p = rule_text;
    while (*p) {
        const wchar_t *eol = p;
        while (*eol && *eol != L'\r' && *eol != L'\n') ++eol;
        size_t line_len = (size_t)(eol - p);
        wchar_t line[512];
        if (line_len >= 512) line_len = 511;
        wmemcpy(line, p, line_len);
        line[line_len] = L'\0';
        p = eol;
        while (*p == L'\r' || *p == L'\n') ++p;

        trim_wstr(line);
        if (line[0] == L'\0' || line[0] == L';' || line[0] == L'#') continue;

        if (line[0] == L'[') {
            wchar_t *rb = wcschr(line, L']');
            if (!rb) continue;
            *rb = L'\0';
            wcsncpy(cur_sec, line + 1, 63);
            cur_sec[63] = L'\0';
            trim_wstr(cur_sec);
            if (cur_sec[0]) {
                WritePrivateProfileSectionW(cur_sec, L"", ini_path);
            }
            continue;
        }

        if (!cur_sec[0]) continue;
        wchar_t *eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = L'\0';
        wchar_t *wkey = line;
        wchar_t *wval = eq + 1;
        wchar_t *semi = wcschr(wval, L';');
        if (semi) *semi = L'\0';
        trim_wstr(wkey);
        trim_wstr(wval);
        if (wkey[0]) {
            if (WritePrivateProfileStringW(cur_sec, wkey, wval, ini_path)) {
                wrote_any = 1;
            }
        }
    }

    return wrote_any ? 0 : -1;
}
