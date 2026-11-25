#include "merge.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

void octans_merge_init(octans_merge_state_t *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
}

void octans_merge_set_pin(octans_merge_state_t *m, const char *device_id) {
    if (!m) return;
    if (!device_id || !device_id[0]) {
        m->pinned_id[0] = '\0';
        return;
    }
    strncpy(m->pinned_id, device_id, sizeof(m->pinned_id) - 1);
    m->pinned_id[sizeof(m->pinned_id) - 1] = '\0';
}

static const wchar_t *find_saved_alias(const octans_merge_state_t *m, const char *device_id) {
    if (!m || !device_id || !device_id[0]) return NULL;
    for (size_t i = 0; i < m->saved_alias_count && i < OCTANS_MAX_DEVICES; ++i) {
        if (strcmp(m->saved_aliases[i].id, device_id) == 0 && m->saved_aliases[i].alias[0]) {
            return m->saved_aliases[i].alias;
        }
    }
    return NULL;
}

static void store_saved_alias(octans_merge_state_t *m, const char *device_id, const wchar_t *alias) {
    if (!m || !device_id || !device_id[0]) return;
    size_t found = m->saved_alias_count;
    for (size_t i = 0; i < m->saved_alias_count && i < OCTANS_MAX_DEVICES; ++i) {
        if (strcmp(m->saved_aliases[i].id, device_id) == 0) {
            found = i;
            break;
        }
    }

    if (!alias || !alias[0]) {
        if (found < m->saved_alias_count) {
            for (size_t i = found; i + 1 < m->saved_alias_count; ++i) {
                m->saved_aliases[i] = m->saved_aliases[i + 1];
            }
            m->saved_alias_count--;
            memset(&m->saved_aliases[m->saved_alias_count], 0, sizeof(m->saved_aliases[0]));
        }
        return;
    }

    if (found == m->saved_alias_count) {
        if (m->saved_alias_count < OCTANS_MAX_DEVICES) {
            found = m->saved_alias_count++;
        } else {
            found = 0;
        }
        strncpy(m->saved_aliases[found].id, device_id, sizeof(m->saved_aliases[found].id) - 1);
        m->saved_aliases[found].id[sizeof(m->saved_aliases[found].id) - 1] = '\0';
    }
    wcsncpy(m->saved_aliases[found].alias, alias, 63);
    m->saved_aliases[found].alias[63] = L'\0';
}

int octans_merge_set_alias(octans_merge_state_t *m, const char *device_id, const wchar_t *alias) {
    if (!m || !device_id || !device_id[0]) return -1;
    store_saved_alias(m, device_id, alias);
    for (size_t i = 0; i < OCTANS_MAX_DEVICES; ++i) {
        if (!m->slots[i].in_use) continue;
        if (strcmp(m->slots[i].view.id, device_id) == 0) {
            if (alias && alias[0]) {
                wcsncpy(m->slots[i].alias, alias, 63);
                m->slots[i].alias[63] = L'\0';
                wcsncpy(m->slots[i].view.name, m->slots[i].alias, 63);
                m->slots[i].view.name[63] = L'\0';
            } else {
                m->slots[i].alias[0] = L'\0';
            }
            break;
        }
    }
    return 0;
}

static void compute_merge_key(const octans_proto_reading_t *r, char *out, size_t cap) {
    if (strncmp(r->stable_id, "hidpp:", 6) == 0 && strlen(r->stable_id) == 14) {
        /* Exact Logitech unitid (hidpp:xxxxxxxx) merges across dongle & wired cable. */
        strncpy(out, r->stable_id, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    if (strncmp(r->stable_id, "bt:", 3) == 0) {
        strncpy(out, r->stable_id, cap - 1);
        out[cap - 1] = '\0';
        return;
    }
    /* Same VID + normalized product name merges wired & 2.4G dongle PIDs when same model. */
    if (r->vid != 0 && r->name[0]) {
        char nbuf[48];
        size_t pos = 0;
        for (size_t i = 0; r->name[i] && pos + 1 < sizeof(nbuf); ++i) {
            wchar_t c = r->name[i];
            if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
            if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) {
                nbuf[pos++] = (char)c;
            }
        }
        nbuf[pos] = '\0';
        if (pos >= 3) {
            snprintf(out, cap, "dev:%04x:%s", r->vid, nbuf);
            return;
        }
    }
    strncpy(out, r->stable_id, cap - 1);
    out[cap - 1] = '\0';
}

static int find_slot_by_id(const octans_merge_state_t *m, const char *id) {
    for (size_t i = 0; i < OCTANS_MAX_DEVICES; ++i) {
        if (m->slots[i].in_use && strcmp(m->slots[i].view.id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int alloc_slot(octans_merge_state_t *m) {
    for (size_t i = 0; i < OCTANS_MAX_DEVICES; ++i) {
        if (!m->slots[i].in_use) return (int)i;
    }
    /* Evict oldest unseen slot if full. */
    size_t oldest = 0;
    for (size_t i = 1; i < OCTANS_MAX_DEVICES; ++i) {
        if (m->slots[i].last_seen_ms < m->slots[oldest].last_seen_ms) {
            oldest = i;
        }
    }
    return (int)oldest;
}

static int reading_priority(const octans_proto_reading_t *r) {
    int p = 0;
    if (r->valid) p += 100;
    if (r->power_state == OCTANS_PWR_CHARGING || r->power_state == OCTANS_PWR_FULL) p += 40;
    if (r->conn == OCTANS_CONN_WIRED) p += 20;
    if (!r->is_estimated) p += 5;
    return p;
}

void octans_merge_apply(octans_merge_state_t *m,
                        const octans_proto_reading_t *readings,
                        size_t count,
                        uint64_t now_ms) {
    if (!m) return;

    int updated_this_tick[OCTANS_MAX_DEVICES];
    int best_prio_this_tick[OCTANS_MAX_DEVICES];
    memset(updated_this_tick, 0, sizeof(updated_this_tick));
    memset(best_prio_this_tick, 0, sizeof(best_prio_this_tick));

    for (size_t i = 0; i < count; ++i) {
        const octans_proto_reading_t *r = &readings[i];
        if (!r->valid && !r->asleep && !r->offline) continue;

        char key[64];
        compute_merge_key(r, key, sizeof(key));

        int idx = find_slot_by_id(m, key);
        if (idx < 0) {
            idx = alloc_slot(m);
            memset(&m->slots[idx], 0, sizeof(m->slots[idx]));
            m->slots[idx].in_use = 1;
            m->slots[idx].view.battery_percent = -1;
            m->slots[idx].view.signal_percent = -1;
            strncpy(m->slots[idx].view.id, key, sizeof(m->slots[idx].view.id) - 1);
            const wchar_t *saved = find_saved_alias(m, key);
            if (saved) {
                wcsncpy(m->slots[idx].alias, saved, 63);
                m->slots[idx].alias[63] = L'\0';
                wcsncpy(m->slots[idx].view.name, saved, 63);
                m->slots[idx].view.name[63] = L'\0';
            }
        }

        octans_device_slot_t *s = &m->slots[idx];
        s->last_seen_ms = now_ms;

        int prio = reading_priority(r);
        if (updated_this_tick[idx] && prio < best_prio_this_tick[idx]) {
            continue;
        }

        if (r->valid) {
            updated_this_tick[idx] = 1;
            best_prio_this_tick[idx] = prio;
            s->last_valid_ms = now_ms;

            s->view.vid = r->vid;
            s->view.pid = r->pid;
            if (r->kind != OCTANS_DEV_UNKNOWN) s->view.kind = r->kind;
            if (r->conn != OCTANS_CONN_UNKNOWN) s->view.conn = r->conn;
            s->view.power_state = r->power_state;
            s->view.battery_percent = r->battery_percent;
            s->view.voltage_mv = r->voltage_mv;
            s->view.signal_percent = r->signal_percent;
            s->view.is_estimated = r->is_estimated;
            s->view.is_low_battery = (r->battery_percent >= 0 && r->battery_percent <= 20) ? 1u : 0u;
            s->view.updated_ms = now_ms;

            if (s->alias[0]) {
                wcsncpy(s->view.name, s->alias, 63);
            } else if (r->name[0]) {
                wcsncpy(s->view.name, r->name, 63);
            }
            s->view.name[63] = L'\0';
            if (r->brand[0]) {
                wcsncpy(s->view.brand, r->brand, 31);
                s->view.brand[31] = L'\0';
            }
        } else if (r->asleep || r->offline) {
            if (!updated_this_tick[idx]) {
                updated_this_tick[idx] = 1;
                best_prio_this_tick[idx] = prio;
                s->view.vid = r->vid;
                s->view.pid = r->pid;
                if (r->kind != OCTANS_DEV_UNKNOWN) s->view.kind = r->kind;
                if (r->conn != OCTANS_CONN_UNKNOWN) s->view.conn = r->conn;
                if (!s->alias[0] && r->name[0] && !s->view.name[0]) {
                    wcsncpy(s->view.name, r->name, 63);
                    s->view.name[63] = L'\0';
                }
                if (r->brand[0] && !s->view.brand[0]) {
                    wcsncpy(s->view.brand, r->brand, 31);
                    s->view.brand[31] = L'\0';
                }
                s->view.power_state = r->offline ? OCTANS_PWR_OFFLINE : OCTANS_PWR_SLEEPING;
                if (s->last_valid_ms == 0 || (now_ms - s->last_valid_ms) > OCTANS_ASLEEP_KEEP_MS) {
                    s->view.battery_percent = -1;
                    s->view.voltage_mv = 0;
                    s->view.is_low_battery = 0;
                }
            }
        }
    }

    /* Prune slots that disappeared from all buses and exceeded retention window. */
    for (size_t i = 0; i < OCTANS_MAX_DEVICES; ++i) {
        if (!m->slots[i].in_use) continue;
        if (!updated_this_tick[i]) {
            if (m->slots[i].last_valid_ms > 0 &&
                (now_ms - m->slots[i].last_valid_ms) <= OCTANS_ASLEEP_KEEP_MS) {
                m->slots[i].view.power_state = OCTANS_PWR_SLEEPING;
            } else {
                m->slots[i].in_use = 0;
            }
        }
    }
}

size_t octans_merge_export(const octans_merge_state_t *m,
                           octans_device_view_t *out,
                           size_t max_count) {
    if (!m) return 0;

    octans_device_view_t tmp[OCTANS_MAX_DEVICES];
    size_t n = 0;
    for (size_t i = 0; i < OCTANS_MAX_DEVICES; ++i) {
        if (m->slots[i].in_use) {
            tmp[n++] = m->slots[i].view;
        }
    }

    /* Simple insertion sort: pinned first, then name/id. */
    for (size_t i = 1; i < n; ++i) {
        octans_device_view_t cur = tmp[i];
        int cur_pin = (m->pinned_id[0] && strcmp(cur.id, m->pinned_id) == 0) ? 1 : 0;
        size_t j = i;
        while (j > 0) {
            int prev_pin = (m->pinned_id[0] && strcmp(tmp[j - 1].id, m->pinned_id) == 0) ? 1 : 0;
            if (prev_pin > cur_pin) break;
            if (prev_pin == cur_pin) {
                int cmp = _wcsicmp(tmp[j - 1].name, cur.name);
                if (cmp < 0 || (cmp == 0 && strcmp(tmp[j - 1].id, cur.id) <= 0)) break;
            }
            tmp[j] = tmp[j - 1];
            --j;
        }
        tmp[j] = cur;
    }

    if (out && max_count > 0) {
        size_t copy_n = n < max_count ? n : max_count;
        memcpy(out, tmp, copy_n * sizeof(octans_device_view_t));
    }
    return n;
}
