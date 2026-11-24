#ifndef SALUS_SLEEP_MI_SLEEP_PACKETS_H
#define SALUS_SLEEP_MI_SLEEP_PACKETS_H

#include "salus/store/db.h"
#include "salus/store/sleep.h"
#include "salus/devices/xiaomi/mi_sleep_stage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct SalusSleepPacketView {
    uint64_t ts_raw;
    uint8_t type;
    const uint8_t *data;
    uint16_t data_len;
} SalusSleepPacketView;

typedef gboolean (*SalusSleepPacketFn)(const SalusSleepPacketView *pkt, void *user_data);

/* Raw stage nibble of a type-17 packet → stored value (SALUS_SLEEP_STAGE_*). */
int salus_sleep_decode_raw_stage(int raw_stage);

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
