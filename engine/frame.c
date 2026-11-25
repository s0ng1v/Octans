#include "frame.h"

#include <string.h>

typedef struct mv_knot {
    uint16_t mv;
    uint8_t pct;
} mv_knot_t;

static const mv_knot_t g_curve_solaar[] = {
    {4186, 100}, {4067, 90}, {3989, 80}, {3922, 70}, {3859, 60},
    {3811, 50},  {3778, 40}, {3751, 30}, {3717, 20}, {3671, 10},
    {3646, 5},   {3579, 2},  {3500, 0}
};

static const mv_knot_t g_curve_g535[] = {
    {4175, 100}, {3817, 50}, {3766, 30}, {3730, 20}, {3664, 5}, {3310, 0}
};

static const mv_knot_t g_curve_airoha[] = {
    {4150, 100}, {4000, 80}, {3880, 55}, {3780, 35}, {3680, 15}, {3300, 0}
};

static int interp_knots(uint16_t mv, const mv_knot_t *knots, size_t n) {
    if (mv >= knots[0].mv) return knots[0].pct;
    for (size_t i = 0; i + 1 < n; ++i) {
        uint16_t hi_mv = knots[i].mv;
        uint16_t lo_mv = knots[i + 1].mv;
        if (mv >= lo_mv) {
            uint32_t num = (uint32_t)(mv - lo_mv) * (uint32_t)(knots[i].pct - knots[i + 1].pct);
            uint32_t den = (uint32_t)(hi_mv - lo_mv);
            return (int)knots[i + 1].pct + octans_round_div(num, den);
        }
    }
    return knots[n - 1].pct;
}

uint8_t octans_crc_xor(const uint8_t *data, size_t start, size_t end) {
    if (!data || start > end) return 0;
    uint8_t x = 0;
    for (size_t i = start; i <= end; ++i) {
        x ^= data[i];
    }
    return x;
}

uint8_t octans_crc_compx(const uint8_t *frame16) {
    if (!frame16) return 0;
    uint32_t s = 0;
    for (size_t i = 0; i < 16; ++i) {
        s += frame16[i];
    }
    return (uint8_t)((0x55u - (s & 0xFFu)) & 0xFFu);
}

uint8_t octans_crc_sum_range(const uint8_t *data, size_t start, size_t end) {
    if (!data || start > end) return 0;
    uint32_t s = 0;
    for (size_t i = start; i <= end; ++i) {
        s += data[i];
    }
    return (uint8_t)(s & 0xFFu);
}

uint16_t octans_crc16_ccitt_false(const uint8_t *data, size_t len) {
    if (!data) return 0;
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

int octans_mv_to_percent(uint16_t mv, octans_curve_id_t curve) {
    switch (curve) {
    case CURVE_SOLAAR_13PT:
        return interp_knots(mv, g_curve_solaar, sizeof(g_curve_solaar) / sizeof(g_curve_solaar[0]));
    case CURVE_LOGI_G535:
        return interp_knots(mv, g_curve_g535, sizeof(g_curve_g535) / sizeof(g_curve_g535[0]));
    case CURVE_AIROHA_BARRACUDA:
        return interp_knots(mv, g_curve_airoha, sizeof(g_curve_airoha) / sizeof(g_curve_airoha[0]));
    default:
        return -1;
    }
}

int octans_clamp_percent(int val) {
    if (val < 0) return 0;
    if (val > 100) return 100;
    return val;
}

int octans_round_div(uint32_t num, uint32_t den) {
    if (den == 0) return 0;
    return (int)((num + den / 2u) / den);
}

void octans_reading_init(octans_proto_reading_t *r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->battery_percent = -1;
    r->signal_percent = -1;
    r->power_state = OCTANS_PWR_UNKNOWN;
    r->kind = OCTANS_DEV_UNKNOWN;
    r->conn = OCTANS_CONN_UNKNOWN;
}
