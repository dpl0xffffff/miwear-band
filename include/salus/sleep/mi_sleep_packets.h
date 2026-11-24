#ifndef SALUS_SLEEP_MI_SLEEP_PACKETS_H
#define SALUS_SLEEP_MI_SLEEP_PACKETS_H

#include "salus/db/db.h"
#include "salus/sleep/mi_sleep_duration.h"
#include "salus/sleep/mi_sleep_stage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct SalusSleepPacketView {
    uint64_t ts_raw;
    uint8_t type;
    const uint8_t *data;
    uint16_t data_len;
} SalusSleepPacketView;

typedef gboolean (*SalusSleepPacketFn)(const SalusSleepPacketView *pkt, void *user_data);

/* Stored sleep-stage values (Gadgetbridge): 0 not asleep, 1 asleep/unknown, 2 deep, 3 light,
 * 4 REM, 5 awake. */
#define SALUS_SLEEP_STAGE_ASLEEP_UNKNOWN 1
/* Raw stage nibble of a type-17 packet → stored value. */
int salus_sleep_decode_raw_stage(int raw_stage);
/* Stored value → timeline stage to paint, or -1 when the minute is not sleep. */
int salus_normalize_sleep_stage(int stage);

/* Band sleep packets carry epoch time as ms (HR) or sometimes sec; never multiply ms again. */
int64_t salus_sleep_packet_timestamp_ms(uint64_t ts_raw);
gboolean salus_sleep_timestamp_plausible(int64_t ts_ms);

gboolean salus_sleep_foreach_packet(const uint8_t *payload,
                                   size_t len,
                                   SalusSleepPacketFn fn,
                                   void *user_data);

gboolean mi_sleep_try_embedded_stage_packets(const uint8_t *sleep_src,
                                             size_t sleep_src_len,
                                             uint32_t bed_sec,
                                             uint32_t wake_sec,
                                             SalusSleepStageRow **rows_out,
                                             size_t *count_out,
                                             SalusSleepStageDurations *durations_out);

#endif
