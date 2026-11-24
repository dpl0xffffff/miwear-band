#include "salus/devices/xiaomi/xiaomi.h"

#include "salus/devices/xiaomi/fitness_sync.h"
#include "salus/devices/xiaomi/wire.h"
#include "salus/devices/xiaomi/wire_internal.h"

#include <string.h>

gboolean xiaomi_auth_key_valid(const char *value) {
    uint8_t key[16];
    return value != NULL && hex_to_bytes16(value, key);
}

static void xiaomi_connect(SalusDevice *dev) {
    xiaomi_wire_connect(dev->app);
}

static void xiaomi_disconnect(SalusDevice *dev) {
    disconnect_socket(dev->app);
    salus_device_set_link(dev, SALUS_LINK_DISCONNECTED, "Disconnected");
}

static void xiaomi_sync(SalusDevice *dev) {
    salus_fitness_sync_resync(dev->app);
}

static gboolean xiaomi_find_device(SalusDevice *dev, gboolean start) {
    return salus_wire_find_band(dev->app, start);
}

static void remove_source(guint *id) {
    if (*id != 0) {
        g_source_remove(*id);
        *id = 0;
    }
}

static void xiaomi_free(SalusDevice *dev) {
    XiaomiDevice *x = (XiaomiDevice *)dev;
    SalusApp *app = dev->app;
    /* The wire code reaches the session through app->device: keep it reachable while tearing
     * down, then detach. */
    SalusDevice *was_active = app->device;
    app->device = dev;
    disconnect_socket(app);
    reset_activity_fetch_state(app);
    app->device = was_active;
    remove_source(&x->activity_refetch_source_id);
    remove_source(&x->activity_live_poll_source_id);
    remove_source(&x->find_band_stop_source);
    if (x->activity_fetch_queue != NULL) {
        g_queue_free_full(x->activity_fetch_queue, g_free);
    }
    if (x->activity_fetch_seen != NULL) {
        g_hash_table_unref(x->activity_fetch_seen);
    }
    memset(x->auth_key_hex, 0, sizeof(x->auth_key_hex));
    salus_device_finalize(dev);
    g_free(x);
}

static const SalusDeviceOps xiaomi_ops = {
    .connect = xiaomi_connect,
    .disconnect = xiaomi_disconnect,
    .sync = xiaomi_sync,
    .find_device = xiaomi_find_device,
    .free = xiaomi_free,
};

static SalusDevice *xiaomi_create(SalusApp *app, const SalusDeviceConfig *cfg) {
    XiaomiDevice *x = g_new0(XiaomiDevice, 1);
    salus_device_init(&x->base, &xiaomi_band_driver, &xiaomi_ops, app, cfg);
    x->sock_fd = -1;
    const char *key = salus_device_config_get(cfg, "auth_key");
    if (xiaomi_auth_key_valid(key)) {
        g_strlcpy(x->auth_key_hex, key, sizeof(x->auth_key_hex));
    }
    x->verbose_wire_log = g_getenv("SALUS_VERBOSE_WIRE_LOG") != NULL;
    x->verbose_fetch_log = g_getenv("SALUS_FETCH_DEBUG") != NULL;
    return &x->base;
}

static const SalusPairingField xiaomi_fields[] = {
    {
        .key = "auth_key",
        .title = "Auth Key",
        .help = "32 hexadecimal characters. The README explains how to get it from Mi Fitness.",
        .secret = TRUE,
        .validate = xiaomi_auth_key_valid,
    },
};

static const char *const xiaomi_name_patterns[] = {"Xiaomi Smart Band*", "Xiaomi Band*", "Mi Smart Band*", NULL};

const SalusDriver xiaomi_band_driver = {
    .id = "xiaomi-band",
    .vendor = "Xiaomi",
    .model = "Smart Band 9 / 10",
    .icon = "salus-watch-symbolic",
    .kind = SALUS_KIND_BAND,
    .caps = SALUS_CAP_HEART_RATE | SALUS_CAP_SPO2 | SALUS_CAP_STRESS | SALUS_CAP_STEPS | SALUS_CAP_SLEEP |
            SALUS_CAP_WORKOUTS | SALUS_CAP_VITALITY | SALUS_CAP_LIVE_STATS | SALUS_CAP_BATTERY |
            SALUS_CAP_NOTIFICATIONS | SALUS_CAP_WEATHER | SALUS_CAP_FIND_DEVICE | SALUS_CAP_FIND_PHONE |
            SALUS_CAP_LOCATION_SHARE | SALUS_CAP_USER_PROFILE,
    .bt_name_patterns = xiaomi_name_patterns,
    .fields = xiaomi_fields,
    .n_fields = G_N_ELEMENTS(xiaomi_fields),
    .pairing_help = "Pair the band once with Mi Fitness on a phone and extract its auth key; Salus then "
                    "talks to the band directly over Bluetooth.",
    .create = xiaomi_create,
};
