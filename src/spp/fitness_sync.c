#include "miwear/spp/fitness_sync.h"
#include "miwear/notifications/notifications.h"
#include "miwear/spp/wire_internal.h"

/*
 * Mi Fitness 3.59.x (com.xiaomi.wearable) fitness pull path:
 *   FitnessDeviceSyncManager.startSyncWithDevice
 *     → pbImpl.syncWearSportStatus()
 *     → FitnessWearSender.requestData
 *         → requestTodayData → getTodayFitnessIds → requestDataIdIfHas
 *         → requestHistoryData
 *   After each file: confirmFitnessId (health subtype 5 ack) — handled in wire fetch queue.
 *
 * Transport/auth: com.xiaomi.wearable.spp + wear.api.WearAuthV2 (miwear-auth), same protobuf Command.
 */

static void sync_wear_sport_status(AppState *state) {
    append_log(state, "Mi Fitness: syncWearSportStatus (pre-sync sport state probe).");
}

void miwear_fitness_sync_on_auth(AppState *state) {
    if (!state->auth_initialized || state->sock_fd < 0) {
        return;
    }
    /* No rebuild here: switch_to_dashboard() (queued just before this on auth) already rebuilt
     * the timelines from the DB, and no file data arrives before begin_file_sync below. */
    append_log(state, "Mi Fitness sync: startSyncWithDevice → requestData (today, then history).");
    sync_wear_sport_status(state);
    miwear_wire_send_device_bootstrap(state);
    miwear_notifications_on_connect(state);
    miwear_wire_begin_file_sync(state, "Mi Fitness today file IDs");
}

static gboolean fitness_sync_on_auth_idle(gpointer user_data) {
    miwear_fitness_sync_on_auth((AppState *)user_data);
    return G_SOURCE_REMOVE;
}

void miwear_fitness_sync_on_auth_async(AppState *state) {
    if (state == NULL) {
        return;
    }
    g_idle_add(fitness_sync_on_auth_idle, state);
}

void miwear_fitness_sync_resync(AppState *state) {
    if (!state->auth_initialized) {
        append_log(state, "History fetch needs authenticated session.");
        return;
    }
    append_log(state, "Mi Fitness sync: manual resync (today + history lists).");
    miwear_wire_resync_file_lists(state, "Mi Fitness resync today IDs");
}

void miwear_request_history_resync(AppState *state) {
    miwear_fitness_sync_resync(state);
}
