#ifndef MIWEAR_SLEEP_MI_SLEEP_PACKETS_H
#define MIWEAR_SLEEP_MI_SLEEP_PACKETS_H

#include "miwear/db/db.h"
#include "miwear/sleep/mi_sleep_duration.h"
#include "miwear/sleep/mi_sleep_stage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct MiwearSleepPacketView {
    uint64_t ts_raw;
    uint8_t type;
    const uint8_t *data;
    uint16_t data_len;
} MiwearSleepPacketView;

typedef gboolean (*MiwearSleepPacketFn)(const MiwearSleepPacketView *pkt, void *user_data);

/* Band sleep packets carry epoch time as ms (HR) or sometimes sec; never multiply ms again. */
int64_t miwear_sleep_packet_timestamp_ms(uint64_t ts_raw);
gboolean miwear_sleep_timestamp_plausible(int64_t ts_ms);

gboolean miwear_sleep_foreach_packet(const uint8_t *payload,
                                   size_t len,
                                   MiwearSleepPacketFn fn,
                                   void *user_data);

size_t mi_sleep_build_hr_epoch_features(const uint8_t *sleep_src,
                                        size_t sleep_src_len,
                                        uint32_t bed_sec,
                                        uint32_t wake_sec,
                                        float *features_out,
                                        size_t max_epochs);

gboolean mi_sleep_try_embedded_stage_packets(const uint8_t *sleep_src,
                                             size_t sleep_src_len,
                                             uint32_t bed_sec,
                                             uint32_t wake_sec,
                                             MiwearSleepStageRow **rows_out,
                                             size_t *count_out,
                                             MiwearSleepStageDurations *durations_out);

#endif
