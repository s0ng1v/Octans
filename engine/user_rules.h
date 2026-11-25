#ifndef OCTANS_USER_RULES_H
#define OCTANS_USER_RULES_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "prototable.h"
#include "../core/hid_enum.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_MAX_USER_RULES 32

typedef enum octans_rule_checksum {
    RULE_CKSUM_NONE = 0,
    RULE_CKSUM_XOR_ALL,
    RULE_CKSUM_COMPX_55,
    RULE_CKSUM_SUM_MOD256
} octans_rule_checksum_t;

typedef enum octans_rule_batt_mode {
    RULE_BATT_PERCENT = 0,
    RULE_BATT_STEP10,
    RULE_BATT_STEP5,
    RULE_BATT_SCALE255,
    RULE_BATT_MV_BE,
    RULE_BATT_MV_LE
} octans_rule_batt_mode_t;

typedef struct octans_user_rule {
    char section[64];
    wchar_t name[64];
    wchar_t brand[32];

    uint16_t vid;
    uint16_t pid;        /**< 0xFFFF = wildcard */
    uint16_t usage_page; /**< 0 = any */
    uint16_t usage;      /**< 0 = any */
    int16_t interface_num; /**< -1 = any */

    octans_device_kind_t kind;
    octans_conn_kind_t conn;
    int is_auto;

    /* Mode A: Built-in protocol template (VAR_NONE if Mode B) */
    octans_proto_variant_t template_variant;

    /* Mode B: Custom byte-level protocol rule */
    octans_tx_kind_t tx;
    uint8_t req[96];
    size_t req_hex_len;
    size_t req_len;      /**< 0 = auto device report length */
    uint8_t checksum_kind;
    uint8_t resp_head[16];
    size_t resp_head_len;
    uint8_t resp_mask[16];
    size_t resp_mask_len;
    int batt_offset;     /**< -1 if invalid */
    uint8_t batt_mode;
    int chg_offset;      /**< -1 if unused */
    uint8_t chg_mask;
    int chg_eq;          /**< >= 0 to match exact byte, else test bitmask */
} octans_user_rule_t;

/** Map built-in variant enum <-> short config template name (e.g. "compx_a"). */
const char *octans_variant_to_template_name(octans_proto_variant_t v);
octans_proto_variant_t octans_template_name_to_variant(const char *name);

/** Parse multi-section INI rule text in memory into `out[0..max_rules-1]`. */
size_t octans_user_rules_parse_text(const wchar_t *ini_text,
                                    octans_user_rule_t *out,
                                    size_t max_rules);

/** Load rules from UTF-16/ANSI/UTF-8 INI file path. Returns count loaded. */
size_t octans_user_rules_load_file(const wchar_t *ini_path,
                                   octans_user_rule_t *out,
                                   size_t max_rules);

/** Check whether a user rule matches an enumerated HID interface. */
int octans_user_rule_matches_iface(const octans_user_rule_t *r,
                                   const octans_hid_info_t *info);

/** Build outgoing request buffer for a custom byte rule (`out_len` receives final packet length). */
int octans_user_rule_build_req(const octans_user_rule_t *r,
                               size_t iface_report_len,
                               uint8_t *out_buf,
                               size_t out_cap,
                               size_t *out_len);

/** Parse a raw HID response buffer according to custom byte rule `r`. */
int octans_user_rule_parse_reply(const octans_user_rule_t *r,
                                 const uint8_t *buf,
                                 size_t len,
                                 octans_proto_reading_t *out);

/**
 * Execute rule `r` against live HID interface `info`.
 * Optionally captures first received raw packet into `raw_reply_out` / `raw_reply_len`.
 * Returns 1 if a valid reading was produced, 0 otherwise.
 */
int octans_user_rule_query_iface(const octans_user_rule_t *r,
                                 const octans_hid_info_t *info,
                                 uint32_t listen_ms,
                                 octans_proto_reading_t *out,
                                 uint8_t *raw_reply_out,
                                 size_t raw_reply_cap,
                                 size_t *raw_reply_len);

/** Format one user rule struct back into a canonical `[Section]` INI block. */
int octans_user_rule_format_ini(const octans_user_rule_t *r, wchar_t *out, size_t out_cap);

/** Save/upsert one `[Section]` INI rule block into `ini_path`. Returns 0 on success. */
int octans_user_rules_save_section(const wchar_t *ini_path, const wchar_t *rule_text);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_USER_RULES_H */
