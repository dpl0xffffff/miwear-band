#ifndef SALUS_SPP_FITNESS_SYNC_H
#define SALUS_SPP_FITNESS_SYNC_H

#include "salus/app.h"

/*
 * Mi Fitness (com.xiaomi.wearable) device sync orchestration.
 * Reference: FitnessDeviceSyncManager.startSyncWithDevice → FitnessWearSender.requestData
 *   (today IDs via getTodayFitnessIds, then history via requestHistoryData).
 */
void salus_fitness_sync_on_auth(AppState *state);
void salus_fitness_sync_on_auth_async(AppState *state);

void salus_fitness_sync_resync(AppState *state);


#endif
