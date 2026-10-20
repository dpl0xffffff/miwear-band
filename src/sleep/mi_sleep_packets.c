#include "miwear/activity/activity.h"
#include "miwear/sleep/mi_sleep_packets.h"
#include "miwear/util.h"

#include <glib.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

gboolean miwear_sleep_foreach_packet(const uint8_t *payload,
                                     size_t len,
                                     MiwearSleepPacketFn fn,
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
        MiwearSleepPacketView view = {
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

typedef struct HrCollectCtx {
    int64_t *hr_ms;
    int *hr_bpm;
    size_t cap;
    size_t count;
    int64_t last_hb_ms;
} HrCollectCtx;

static gboolean collect_hr_packet(const MiwearSleepPacketView *pkt, void *user_data) {
    HrCollectCtx *ctx = user_data;
    if (pkt->type != 1 || ctx == NULL) {
        return TRUE;
    }
    const long long tms = (long long)pkt->ts_raw;
    if (ctx->count == 0 || (ctx->last_hb_ms == 0) || (tms > 0 && llabs(ctx->last_hb_ms - tms) > 30000LL)) {
        ctx->last_hb_ms = tms;
    }
    for (uint16_t i = 0; i < pkt->data_len; i++) {
        const int delta = pkt->data[i];
        if (delta <= 0) {
            continue;
        }
        ctx->last_hb_ms += (long long)delta * 10LL;
        const int rr_ms = delta * 10;
        const int hr = rr_ms > 0 ? (60000 / rr_ms) : 0;
        if (hr <= 0 || hr >= 240) {
            continue;
        }
        if (ctx->count >= ctx->cap) {
            size_t new_cap = ctx->cap == 0 ? 256 : ctx->cap * 2;
            int64_t *nms = g_realloc(ctx->hr_ms, new_cap * sizeof(int64_t));
            int *nbpm = g_realloc(ctx->hr_bpm, new_cap * sizeof(int));
            if (nms == NULL || nbpm == NULL) {
                g_free(nms);
                g_free(nbpm);
                return FALSE;
            }
            ctx->hr_ms = nms;
            ctx->hr_bpm = nbpm;
            ctx->cap = new_cap;
        }
        ctx->hr_ms[ctx->count] = ctx->last_hb_ms;
        ctx->hr_bpm[ctx->count] = hr;
        ctx->count++;
    }
    return TRUE;
}

size_t mi_sleep_build_hr_epoch_features(const uint8_t *sleep_src,
                                        size_t sleep_src_len,
                                        uint32_t bed_sec,
                                        uint32_t wake_sec,
                                        float *features_out,
                                        size_t max_epochs) {
    if (sleep_src == NULL || features_out == NULL || max_epochs == 0 || wake_sec <= bed_sec) {
        return 0;
    }
    HrCollectCtx ctx = {0};
    if (!miwear_sleep_foreach_packet(sleep_src, sleep_src_len, collect_hr_packet, &ctx) || ctx.count == 0) {
        g_free(ctx.hr_ms);
        g_free(ctx.hr_bpm);
        return 0;
    }

    const int64_t bed_ms = (int64_t)bed_sec * 1000LL;
    const int64_t wake_ms = (int64_t)wake_sec * 1000LL;
    const int64_t span_ms = wake_ms - bed_ms;
    const size_t epoch_cap =
        (size_t)((span_ms + (MI_SLEEP_EPOCH_SEC * 1000LL) - 1) / (MI_SLEEP_EPOCH_SEC * 1000LL));
    size_t epochs = epoch_cap;
    if (epochs > max_epochs) {
        epochs = max_epochs;
    }
    if (epochs > MI_SLEEP_MAX_EPOCHS) {
        epochs = MI_SLEEP_MAX_EPOCHS;
    }
    if (epochs == 0) {
        g_free(ctx.hr_ms);
        g_free(ctx.hr_bpm);
        return 0;
    }

    memset(features_out, 0, epochs * MI_SLEEP_FEATURE_DIM * sizeof(float));
    for (size_t e = 0; e < epochs; e++) {
        const int64_t e_start = bed_ms + (int64_t)e * MI_SLEEP_EPOCH_SEC * 1000LL;
        const int64_t e_end = e_start + MI_SLEEP_EPOCH_SEC * 1000LL;
        float sum = 0.0f;
        float sumsq = 0.0f;
        int n = 0;
        int min_hr = 255;
        int max_hr = 0;
        for (size_t i = 0; i < ctx.count; i++) {
            const int64_t t = ctx.hr_ms[i];
            if (t < e_start || t >= e_end) {
                continue;
            }
            const float hr = (float)ctx.hr_bpm[i];
            sum += hr;
            sumsq += hr * hr;
            n++;
            if (ctx.hr_bpm[i] < min_hr) {
                min_hr = ctx.hr_bpm[i];
            }
            if (ctx.hr_bpm[i] > max_hr) {
                max_hr = ctx.hr_bpm[i];
            }
        }
        float *row = features_out + e * MI_SLEEP_FEATURE_DIM;
        if (n > 0) {
            const float mean = sum / (float)n;
            const float var = sumsq / (float)n - mean * mean;
            row[0] = mean / 120.0f;
            row[1] = (var > 0.0f ? sqrtf(var) : 0.0f) / 30.0f;
            row[2] = (float)min_hr / 120.0f;
            row[3] = (float)max_hr / 120.0f;
            row[4] = (float)n / 25.0f;
        }
    }

    g_free(ctx.hr_ms);
    g_free(ctx.hr_bpm);

    size_t used = epochs;
    while (used > 0) {
        const float *row = features_out + (used - 1) * MI_SLEEP_FEATURE_DIM;
        int empty = 1;
        for (int j = 0; j < MI_SLEEP_FEATURE_DIM; j++) {
            if (row[j] != 0.0f) {
                empty = 0;
                break;
            }
        }
        if (!empty) {
            break;
        }
        used--;
    }
    return used;
}

typedef struct EmbeddedStageCtx {
    MiwearSleepStageRow *rows;
    size_t count;
    size_t cap;
    MiwearSleepStageDurations durations;
} EmbeddedStageCtx;

static gboolean collect_embedded_stage_packet(const MiwearSleepPacketView *pkt, void *user_data) {
    EmbeddedStageCtx *ctx = user_data;
    if (ctx == NULL) {
        return TRUE;
    }
    if (pkt->type == 16 && pkt->data_len >= 11) {
        const uint16_t sleep_duration = miwear_read_u16_le(pkt->data + 1);
        const uint16_t wake_duration = miwear_read_u16_le(pkt->data + 3);
        const uint16_t light_duration = miwear_read_u16_le(pkt->data + 5);
        const uint16_t deep_duration = miwear_read_u16_le(pkt->data + 7);
        const uint16_t rem_duration = miwear_read_u16_le(pkt->data + 9);
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
    int64_t current_ms = (int64_t)pkt->ts_raw * 1000LL;
    for (uint16_t i = 0; i + 1 < pkt->data_len; i += 2) {
        const uint16_t val = ((uint16_t)pkt->data[i] << 8) | pkt->data[i + 1];
        const int stage = (val >> 12) & 0x0F;
        const int offset_min = val & 0x0FFF;
        if (offset_min <= 0 || offset_min > 720) {
            continue;
        }
        if (ctx->count >= ctx->cap) {
            size_t new_cap = ctx->cap == 0 ? 64 : ctx->cap * 2;
            MiwearSleepStageRow *nrows = g_realloc(ctx->rows, new_cap * sizeof(MiwearSleepStageRow));
            if (nrows == NULL) {
                return FALSE;
            }
            ctx->rows = nrows;
            ctx->cap = new_cap;
        }
        ctx->rows[ctx->count].timestamp_ms = current_ms;
        ctx->rows[ctx->count].stage = miwear_normalize_sleep_stage(stage);
        ctx->count++;
        current_ms += (int64_t)offset_min * 60000LL;
    }
    return TRUE;
}

gboolean mi_sleep_try_embedded_stage_packets(const uint8_t *sleep_src,
                                             size_t sleep_src_len,
                                             MiwearSleepStageRow **rows_out,
                                             size_t *count_out,
                                             MiwearSleepStageDurations *durations_out) {
    if (sleep_src == NULL || rows_out == NULL || count_out == NULL) {
        return FALSE;
    }
    *rows_out = NULL;
    *count_out = 0;
    EmbeddedStageCtx ctx = {0};
    if (!miwear_sleep_foreach_packet(sleep_src, sleep_src_len, collect_embedded_stage_packet, &ctx)) {
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
        miwear_sleep_normalize_stage_durations(durations_out);
    }
    return ctx.count > 0 || ctx.durations.deep_min > 0;
}
