#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t acting_player;
    uint8_t action;
    uint8_t phase;
    uint8_t result;  // outcome flags (challenge success/fail, block, etc.)
} HistoryEntry;

typedef struct {
    HistoryEntry entries[64];
    uint8_t head;
    uint8_t len;
} HistoryBuffer;

static inline void history_init(HistoryBuffer *buf) {
    buf->head = 0;
    buf->len = 0;
    for (int i = 0; i < 64; i++) {
        buf->entries[i].acting_player = 0;
        buf->entries[i].action = 0;
        buf->entries[i].phase = 0;
        buf->entries[i].result = 0;
    }
}

static inline void history_push(HistoryBuffer *buf, HistoryEntry entry) {
    buf->entries[buf->head] = entry;
    buf->head = (buf->head + 1) & 63;  // mod 64
    if (buf->len < 64) buf->len++;
}

// index 0 = most recent
static inline HistoryEntry history_get(const HistoryBuffer *buf, int index) {
    HistoryEntry empty = {0, 0, 0, 0};
    if (index >= buf->len) return empty;
    int pos = ((int)buf->head - 1 - index) & 63;
    return buf->entries[pos];
}

#ifdef __cplusplus
}
#endif

#endif // HISTORY_H
