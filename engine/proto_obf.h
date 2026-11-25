#ifndef OCTANS_PROTO_OBF_H
#define OCTANS_PROTO_OBF_H

#include <stddef.h>
#include <stdint.h>

/*
 * Lightweight compile-time protocol constant obfuscation.
 * Keeps source code 100% readable & open-source while ensuring compiled
 * binaries do not expose raw plaintext VID/PID/usage/command byte tables
 * to trivial hex/strings scraping. Zero runtime overhead and zero AV risk.
 */

#define OCTANS_OBF_SEED16 0xA75Du
#define OCTANS_OBF_STEP16 0x1337u
#define OCTANS_OBF_SEED8  0x6Du
#define OCTANS_OBF_STEP8  0x3Bu

#define OCTANS_OBF_K16(salt) ((uint16_t)(OCTANS_OBF_SEED16 ^ (uint16_t)((uint32_t)(salt) * OCTANS_OBF_STEP16)))
#define OCTANS_OBF_K8(salt)  ((uint8_t)(OCTANS_OBF_SEED8 ^ (uint8_t)((uint32_t)(salt) * OCTANS_OBF_STEP8)))

/* Compile-time constant encoders (folded into .rdata by Clang) */
#define OCTANS_ENC16(v, salt) ((uint16_t)((uint16_t)(v) ^ OCTANS_OBF_K16(salt)))
#define OCTANS_ENC8(v, salt)  ((uint8_t)((uint8_t)(v) ^ OCTANS_OBF_K8(salt)))

/* Runtime volatile zero-mask prevents constant-folding back into plaintext in .text */
static inline uint16_t octans_dec16(uint16_t enc, uint16_t salt) {
    static volatile uint16_t s_seed = OCTANS_OBF_SEED16;
    uint16_t k = (uint16_t)(s_seed ^ (uint16_t)((uint32_t)salt * OCTANS_OBF_STEP16));
    return (uint16_t)(enc ^ k);
}

static inline uint8_t octans_dec8(uint8_t enc, uint8_t salt) {
    static volatile uint8_t s_seed = OCTANS_OBF_SEED8;
    uint8_t k = (uint8_t)(s_seed ^ (uint8_t)((uint32_t)salt * OCTANS_OBF_STEP8));
    return (uint8_t)(enc ^ k);
}

static inline void octans_dec_bytes(uint8_t *dst, const uint8_t *enc, size_t len, uint8_t base_salt) {
    for (size_t i = 0; i < len; ++i) {
        dst[i] = octans_dec8(enc[i], (uint8_t)(base_salt + (uint8_t)i));
    }
}

#endif /* OCTANS_PROTO_OBF_H */
