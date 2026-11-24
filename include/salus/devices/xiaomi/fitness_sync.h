#ifndef SALUS_SPP_FITNESS_SYNC_H
#define SALUS_SPP_FITNESS_SYNC_H

#include "salus/app.h"

/*
 * Mi Fitness (com.xiaomi.wearable) device sync orchestration.
 * Reference: FitnessDeviceSyncManager.startSyncWithDevice → FitnessWearSender.requestData
 *   (today IDs via getTodayFitnessIds, then history via requestHistoryData).
 */
void salus_fitness_sync_on_auth(SalusApp *state);
void salus_fitness_sync_on_auth_async(SalusApp *state);

void salus_fitness_sync_resync(SalusApp *state);


#endif
