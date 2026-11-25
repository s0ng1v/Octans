#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "hid_parse.h"

#include <string.h>

static int is_battery_usage(uint16_t up, uint16_t u, int *is_signal) {
    *is_signal = 0;
    if (up == 0x0006 && u == 0x0020) return 1;
    if (up == 0x000D && u == 0x003B) return 1;
    if (up == 0x0085 && (u == 0x0001 || u == 0x0066 || u == 0x0065)) return 1;
    if (up == 0x0006 && u == 0x0029) {
        *is_signal = 1;
        return 1;
    }
    return 0;
}

static int scale_usage_value(ULONG raw, LONG logical_max) {
    if (logical_max <= 0 || logical_max == 100) {
        if (raw > 100) {
            return (int)((raw * 100ULL + 127ULL) / 255ULL);
        }
        return (int)raw;
    }
    if (logical_max == 0xFF && raw <= 100) {
        /* Some dongles declare LogicalMax=255 while reporting 0..100 directly. */
        return (int)raw;
    }
    int pct = (int)((raw * 100ULL + (uint64_t)logical_max / 2ULL) / (uint64_t)logical_max);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static int payload_all_zero(const uint8_t *buf, size_t len) {
    for (size_t i = 1; i < len; ++i) {
        if (buf[i] != 0) return 0;
    }
    return 1;
}

int octans_hid_read_std_battery(octans_hid_dev_t *dev,
                                int *out_percent,
                                int *out_signal,
                                uint16_t *out_usage_page,
                                uint16_t *out_usage) {
    if (out_percent) *out_percent = -1;
    if (out_signal) *out_signal = -1;
    if (out_usage_page) *out_usage_page = 0;
    if (out_usage) *out_usage = 0;
    if (!dev || !dev->preparsed || dev->feature_len == 0) return -1;

    USHORT cap_count = 0;
    HidP_GetValueCaps(HidP_Feature, NULL, &cap_count, (PHIDP_PREPARSED_DATA)dev->preparsed);
    if (cap_count == 0) return -1;

    HIDP_VALUE_CAPS caps[64];
    if (cap_count > 64) cap_count = 64;
    if (HidP_GetValueCaps(HidP_Feature, caps, &cap_count, (PHIDP_PREPARSED_DATA)dev->preparsed) !=
        HIDP_STATUS_SUCCESS) {
        return -1;
    }

    int found_battery = 0;
    for (USHORT i = 0; i < cap_count; ++i) {
        const HIDP_VALUE_CAPS *c = &caps[i];
        uint16_t u_min = c->IsRange ? c->Range.UsageMin : c->NotRange.Usage;
        uint16_t u_max = c->IsRange ? c->Range.UsageMax : c->NotRange.Usage;
        for (uint32_t u = u_min; u <= u_max; ++u) {
            int is_signal = 0;
            if (!is_battery_usage(c->UsagePage, (uint16_t)u, &is_signal)) continue;

            uint8_t report[512];
            size_t rlen = dev->feature_len;
            if (rlen > sizeof(report)) rlen = sizeof(report);
            if (octans_hid_get_feature(dev, c->ReportID, report, rlen) < 0) continue;

            ULONG val = 0;
            NTSTATUS st = HidP_GetUsageValue(
                HidP_Feature,
                c->UsagePage,
                c->LinkCollection,
                (USAGE)u,
                &val,
                (PHIDP_PREPARSED_DATA)dev->preparsed,
                (PCHAR)report,
                (ULONG)rlen);
            if (st != HIDP_STATUS_SUCCESS) continue;

            /* Guard against stub descriptors where Feature report returns all zero bytes. */
            if (val == 0 && payload_all_zero(report, rlen)) continue;

            int pct = scale_usage_value(val, c->LogicalMax);
            if (is_signal) {
                if (out_signal && *out_signal < 0) *out_signal = pct;
            } else if (!found_battery) {
                found_battery = 1;
                if (out_percent) *out_percent = pct;
                if (out_usage_page) *out_usage_page = c->UsagePage;
                if (out_usage) *out_usage = (uint16_t)u;
            }
        }
    }

    return found_battery ? 0 : -1;
}

void octans_hid_dump_caps(octans_hid_dev_t *dev, FILE *out) {
    if (!dev || !dev->preparsed || !out) return;

    static const struct {
        HIDP_REPORT_TYPE rtype;
        const char *label;
    } types[3] = {
        {HidP_Input, "IN"},
        {HidP_Output, "OUT"},
        {HidP_Feature, "FEAT"},
    };

    for (int t = 0; t < 3; ++t) {
        USHORT cnt = 0;
        HidP_GetValueCaps(types[t].rtype, NULL, &cnt, (PHIDP_PREPARSED_DATA)dev->preparsed);
        if (cnt == 0) continue;

        HIDP_VALUE_CAPS caps[64];
        USHORT n = cnt > 64 ? 64 : cnt;
        if (HidP_GetValueCaps(types[t].rtype, caps, &n, (PHIDP_PREPARSED_DATA)dev->preparsed) !=
            HIDP_STATUS_SUCCESS) {
            continue;
        }
        for (USHORT i = 0; i < n; ++i) {
            const HIDP_VALUE_CAPS *c = &caps[i];
            uint16_t umin = c->IsRange ? c->Range.UsageMin : c->NotRange.Usage;
            uint16_t umax = c->IsRange ? c->Range.UsageMax : c->NotRange.Usage;
            fprintf(out,
                    "    %s cap: rid=0x%02X up=0x%04X u=0x%04X..0x%04X bits=%u cnt=%u lmin=%ld lmax=%ld\n",
                    types[t].label,
                    (unsigned)c->ReportID,
                    (unsigned)c->UsagePage,
                    (unsigned)umin,
                    (unsigned)umax,
                    (unsigned)c->BitSize,
                    (unsigned)c->ReportCount,
                    (long)c->LogicalMin,
                    (long)c->LogicalMax);
        }
    }
}

size_t octans_hid_collect_feature_rids(octans_hid_dev_t *dev, uint8_t *out_rids, size_t max_rids) {
    if (!out_rids || max_rids == 0) return 0;
    size_t count = 0;
    out_rids[count++] = 0x00u;
    if (!dev || !dev->preparsed) return count;

    USHORT cnt = 0;
    HidP_GetValueCaps(HidP_Feature, NULL, &cnt, (PHIDP_PREPARSED_DATA)dev->preparsed);
    if (cnt == 0) return count;

    HIDP_VALUE_CAPS caps[64];
    USHORT cap_n = cnt > 64 ? 64 : cnt;
    if (HidP_GetValueCaps(HidP_Feature, caps, &cap_n,
                          (PHIDP_PREPARSED_DATA)dev->preparsed) != HIDP_STATUS_SUCCESS) {
        return count;
    }
    for (USHORT c = 0; c < cap_n && count < max_rids; ++c) {
        uint8_t rid = caps[c].ReportID;
        int dup = 0;
        for (size_t k = 0; k < count; ++k) {
            if (out_rids[k] == rid) { dup = 1; break; }
        }
        if (!dup) out_rids[count++] = rid;
    }
    return count;
}
