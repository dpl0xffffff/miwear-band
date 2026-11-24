#include "salus/activity/activity.h"
#include "salus/sleep/mi_sleep_packets.h"
#include "salus/util.h"

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int salus_sleep_decode_raw_stage(int raw_stage) {
    /* Gadgetbridge SleepDetailsParser.decodeStage() */
    switch (raw_stage) {
        case 0: return 5; /* awake */
        case 1: return 3; /* light */
        case 2: return 2; /* deep */
        case 3: return 4; /* REM */
        case 4: return 0; /* not asleep */
        default: return 1; /* n/a */
    }
}

int salus_normalize_sleep_stage(int stage) {
    if (stage >= 2 && stage <= 5) {
        return stage;
    }
    return stage == SALUS_SLEEP_STAGE_ASLEEP_UNKNOWN ? stage : -1; /* 0 = not asleep, other = invalid */
}

int64_t salus_sleep_packet_timestamp_ms(uint64_t ts_raw) {
    if (ts_raw == 0) {
        return 0;
    }
    if (ts_raw >= 1000000000000ULL) {
        return (int64_t)ts_raw;
    }
    if (ts_raw >= 1000000000ULL) {
        return (int64_t)ts_raw * 1000LL;
    }
    return 0;
}

gboolean salus_sleep_timestamp_plausible(int64_t ts_ms) {
    if (ts_ms <= 0) {
        return FALSE;
    }
    /* 2020-01-01 .. 2035-01-01 UTC */
    return ts_ms >= 1577836800000LL && ts_ms <= 2051222400000LL;
}

static gboolean read_sleep_stage_packet_header(const uint8_t *buf, size_t len, size_t *off) {
    while (*off + 17 <= len) {
        if ((buf[*off] == 0xFF && buf[*off + 1] == 0xFC && buf[*off + 2] == 0xFA && buf[*off + 3] == 0xFB) ||
            (buf[*off] == 0xFB && buf[*off + 1] == 0xFA && buf[*off + 2] == 0xFC && buf[*off + 3] == 0xFF)) {
            *off += 4;
            return TRUE;
        }
        *off += 1;
    }
    return FALSE;
}

gboolean salus_sleep_foreach_packet(const uint8_t *payload,
                                     size_t len,
                                     SalusSleepPacketFn fn,
                                     void *user_data) {
    if (payload == NULL || fn == NULL) {
        return FALSE;
    }
    size_t pos = 0;
    gboolean any = FALSE;
    while (read_sleep_stage_packet_header(payload, len, &pos)) {
        if (pos + 13 > len) {
            break;
        }
        (void)payload[pos++]; /* header_len */
        uint64_t ts_raw = 0;
        for (int i = 0; i < 8; i++) {
            ts_raw = (ts_raw << 8) | payload[pos + i];
        }
        pos += 8;
        pos += 1; /* parity */
        const uint8_t type = payload[pos++];
        const uint16_t data_len = ((uint16_t)payload[pos] << 8) | payload[pos + 1];
        pos += 2;
        if (pos + data_len > len) {
            break;
        }
        SalusSleepPacketView view = {
            .ts_raw = ts_raw,
            .type = type,
            .data = payload + pos,
            .data_len = data_len,
        };
        any = TRUE;
        if (!fn(&view, user_data)) {
            break;
        }
        pos += data_len;
    }
    return any;
}

typedef struct EmbeddedStageCtx {
    SalusSleepStageRow *rows;
    size_t count;
    size_t cap;
    SalusSleepStageDurations durations;
    uint32_t bed_sec;
    uint32_t wake_sec;
} EmbeddedStageCtx;

static gboolean collect_embedded_stage_packet(const SalusSleepPacketView *pkt, void *user_data) {
    EmbeddedStageCtx *ctx = user_data;
    if (ctx == NULL) {
        return TRUE;
    }
    if (pkt->type == 16 && pkt->data_len >= 11) {
        /* Big-endian: total, awake, light, REM, deep (Gadgetbridge SleepDetailsParser type 16). */
        const uint16_t sleep_duration = salus_read_u16_be(pkt->data + 1);
        const uint16_t wake_duration = salus_read_u16_be(pkt->data + 3);
        const uint16_t light_duration = salus_read_u16_be(pkt->data + 5);
        const uint16_t rem_duration = salus_read_u16_be(pkt->data + 7);
        const uint16_t deep_duration = salus_read_u16_be(pkt->data + 9);
        ctx->durations.total_min = sleep_duration > 0 ? (int)sleep_duration : ctx->durations.total_min;
        ctx->durations.awake_min = wake_duration > 0 ? (int)wake_duration : ctx->durations.awake_min;
        ctx->durations.light_min = light_duration > 0 ? (int)light_duration : ctx->durations.light_min;
        ctx->durations.deep_min = deep_duration > 0 ? (int)deep_duration : ctx->durations.deep_min;
        ctx->durations.rem_min = rem_duration > 0 ? (int)rem_duration : ctx->durations.rem_min;
        return TRUE;
    }
    if (pkt->type != 17) {
        return TRUE;
    }
    int64_t current_ms = salus_sleep_packet_timestamp_ms(pkt->ts_raw);
    if (!salus_sleep_timestamp_plausible(current_ms)) {
        return TRUE; /* each packet is anchored at its own timestamp; never chain an unreadable one */
    }
    for (uint16_t i = 0; i + 1 < pkt->data_len; i += 2) {
        const uint16_t val = ((uint16_t)pkt->data[i] << 8) | pkt->data[i + 1];
        const int stage = salus_sleep_decode_raw_stage((val >> 12) & 0x0F);
        int offset_min = (int)(val & 0x0FFF);
        if (offset_min < 0 || offset_min > 720) {
            continue;
        }
        if (offset_min == 0) {
            offset_min = 1;
        }
        if (ctx->count >= ctx->cap) {
            size_t new_cap = ctx->cap == 0 ? 64 : ctx->cap * 2;
            SalusSleepStageRow *nrows = g_realloc(ctx->rows, new_cap * sizeof(SalusSleepStageRow));
            if (nrows == NULL) {
                return FALSE;
            }
            ctx->rows = nrows;
            ctx->cap = new_cap;
        }
        ctx->rows[ctx->count].timestamp_ms = current_ms;
        ctx->rows[ctx->count].stage = stage;
        ctx->count++;
        current_ms += (int64_t)offset_min * 60000LL;
    }
    return TRUE;
}

gboolean mi_sleep_try_embedded_stage_packets(const uint8_t *sleep_src,
                                             size_t sleep_src_len,
                                             uint32_t bed_sec,
                                             uint32_t wake_sec,
                                             SalusSleepStageRow **rows_out,
                                             size_t *count_out,
                                             SalusSleepStageDurations *durations_out) {
    if (sleep_src == NULL || rows_out == NULL || count_out == NULL) {
        return FALSE;
    }
    *rows_out = NULL;
    *count_out = 0;
    EmbeddedStageCtx ctx = {0};
    ctx.bed_sec = bed_sec;
    ctx.wake_sec = wake_sec;
    if (!salus_sleep_foreach_packet(sleep_src, sleep_src_len, collect_embedded_stage_packet, &ctx)) {
        g_free(ctx.rows);
        return FALSE;
    }
    if (ctx.count == 0 && ctx.durations.deep_min <= 0 && ctx.durations.light_min <= 0) {
        g_free(ctx.rows);
        return FALSE;
    }
    *rows_out = ctx.rows;
    *count_out = ctx.count;
    if (durations_out != NULL) {
        *durations_out = ctx.durations;
        salus_sleep_normalize_stage_durations(durations_out);
    }
    return ctx.count > 0 || ctx.durations.deep_min > 0;
}
