#ifndef OCTANS_MERGE_H
#define OCTANS_MERGE_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "octans_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OCTANS_ASLEEP_KEEP_MS 300000ULL

typedef struct octans_device_slot {
    int in_use;
    octans_device_view_t view;
    uint64_t last_seen_ms;
    uint64_t last_valid_ms;
    wchar_t raw_name[64];
    wchar_t alias[64];
} octans_device_slot_t;

typedef struct octans_saved_alias {
    char id[64];
    wchar_t alias[64];
} octans_saved_alias_t;

typedef struct octans_merge_state {
    octans_device_slot_t slots[OCTANS_MAX_DEVICES];
    char pinned_id[64];
    octans_saved_alias_t saved_aliases[OCTANS_MAX_DEVICES];
    size_t saved_alias_count;
} octans_merge_state_t;

/** Initialize merge state. */
void octans_merge_init(octans_merge_state_t *m);

/** Set/clear pinned device ID (shown first). */
void octans_merge_set_pin(octans_merge_state_t *m, const char *device_id);

/** Set user display alias for a device ID. */
int octans_merge_set_alias(octans_merge_state_t *m, const char *device_id, const wchar_t *alias);

/** Apply a batch of raw protocol readings at `now_ms`, deduplicating and retaining sleeping devices. */
void octans_merge_apply(octans_merge_state_t *m,
                        const octans_proto_reading_t *readings,
                        size_t count,
                        uint64_t now_ms);

/** Copy sorted merged snapshots into caller buffer; returns total active count. */
size_t octans_merge_export(const octans_merge_state_t *m,
                           octans_device_view_t *out,
                           size_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* OCTANS_MERGE_H */
