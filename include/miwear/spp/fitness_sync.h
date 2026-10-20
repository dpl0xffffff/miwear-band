#ifndef MIWEAR_SPP_FITNESS_SYNC_H
#define MIWEAR_SPP_FITNESS_SYNC_H

#include "miwear/app.h"

/*
 * Mi Fitness (com.xiaomi.wearable) device sync orchestration.
 * Reference: FitnessDeviceSyncManager.startSyncWithDevice → FitnessWearSender.requestData
 *   (today IDs via getTodayFitnessIds, then history via requestHistoryData).
 */
void miwear_fitness_sync_on_auth(AppState *state);
void miwear_fitness_sync_on_auth_async(AppState *state);

void miwear_fitness_sync_resync(AppState *state);

/* UI / wire: same as miwear_fitness_sync_resync */
void miwear_request_history_resync(AppState *state);

#endif
