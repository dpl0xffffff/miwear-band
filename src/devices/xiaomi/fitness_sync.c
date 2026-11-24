#include "salus/devices/xiaomi/xiaomi.h"
#include "salus/devices/xiaomi/fitness_sync.h"
#include "salus/notifications/notifications.h"
#include "salus/devices/xiaomi/wire_internal.h"

/*
 * Mi Fitness 3.59.x (com.xiaomi.wearable) fitness pull path:
 *   FitnessDeviceSyncManager.startSyncWithDevice
 *     → pbImpl.syncWearSportStatus()
 *     → FitnessWearSender.requestData
 *         → requestTodayData → getTodayFitnessIds → requestDataIdIfHas
 *         → requestHistoryData
 *   After each file: confirmFitnessId (health subtype 5 ack) — handled in wire fetch queue.
 *
 * Transport/auth: com.xiaomi.wearable.spp + wear.api.WearAuthV2 (salus-auth), same protobuf Command.
 */

void salus_fitness_sync_on_auth(SalusApp *state) {
    if (!xiaomi(state)->auth_initialized || xiaomi(state)->sock_fd < 0) {
        return;
    }
    /* No rebuild here: switch_to_dashboard() (queued just before this on auth) already rebuilt
     * the timelines from the DB, and no file data arrives before begin_file_sync below. */
    append_log(state, "Mi Fitness sync: startSyncWithDevice → requestData (today, then history).");
    salus_wire_send_device_bootstrap(state);
    salus_notifications_on_connect(state);
    salus_wire_begin_file_sync(state, "Mi Fitness today file IDs");
}

static gboolean fitness_sync_on_auth_idle(gpointer user_data) {
    salus_fitness_sync_on_auth((SalusApp *)user_data);
    return G_SOURCE_REMOVE;
}

void salus_fitness_sync_on_auth_async(SalusApp *state) {
    if (state == NULL) {
        return;
    }
    g_idle_add(fitness_sync_on_auth_idle, state);
}

void salus_fitness_sync_resync(SalusApp *state) {
    if (!xiaomi(state)->auth_initialized) {
        append_log(state, "History fetch needs authenticated session.");
        return;
    }
    append_log(state, "Mi Fitness sync: manual resync (today + history lists).");
    salus_wire_resync_file_lists(state, "Mi Fitness resync today IDs");
}

