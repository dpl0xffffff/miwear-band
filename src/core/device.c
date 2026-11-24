#include "salus/device.h"

#include "salus/app.h"
#include "salus/services/location.h"
#include "salus/services/notifications.h"
#include "salus/ui/dashboard.h"

/* ---- Config ----------------------------------------------------------------------------- */

SalusDeviceConfig *salus_device_config_new(const char *address, const char *driver_id) {
    SalusDeviceConfig *cfg = g_new0(SalusDeviceConfig, 1);
    cfg->address = g_strdup(address);
    cfg->driver_id = g_strdup(driver_id);
    cfg->fields = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    return cfg;
}

SalusDeviceConfig *salus_device_config_copy(const SalusDeviceConfig *cfg) {
    if (cfg == NULL) {
        return NULL;
    }
    SalusDeviceConfig *c = salus_device_config_new(cfg->address, cfg->driver_id);
    c->name = g_strdup(cfg->name);
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, cfg->fields);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        g_hash_table_replace(c->fields, g_strdup(k), g_strdup(v));
    }
    return c;
}

void salus_device_config_free(SalusDeviceConfig *cfg) {
    if (cfg == NULL) {
        return;
    }
    g_free(cfg->address);
    g_free(cfg->driver_id);
    g_free(cfg->name);
    g_hash_table_unref(cfg->fields);
    g_free(cfg);
}

const char *salus_device_config_get(const SalusDeviceConfig *cfg, const char *key) {
    return cfg != NULL ? g_hash_table_lookup(cfg->fields, key) : NULL;
}

void salus_device_config_set(SalusDeviceConfig *cfg, const char *key, const char *value) {
    if (value != NULL) {
        g_hash_table_replace(cfg->fields, g_strdup(key), g_strdup(value));
    } else {
        g_hash_table_remove(cfg->fields, key);
    }
}

/* ---- Device lifecycle ------------------------------------------------------------------- */

void salus_device_init(SalusDevice *dev, const SalusDriver *driver, const SalusDeviceOps *ops, SalusApp *app,
                       const SalusDeviceConfig *cfg) {
    dev->driver = driver;
    dev->ops = ops;
    dev->app = app;
    dev->config = salus_device_config_copy(cfg);
    dev->link = SALUS_LINK_DISCONNECTED;
}

void salus_device_finalize(SalusDevice *dev) {
    salus_device_config_free(dev->config);
    dev->config = NULL;
}

void salus_device_set_link(SalusDevice *dev, SalusLinkState link, const char *status) {
    SalusApp *app = dev->app;
    const SalusLinkState was = dev->link;
    dev->link = link;
    switch (link) {
        case SALUS_LINK_CONNECTING:
            set_status(app, status != NULL ? status : "Connecting…");
            set_connect_busy(app, TRUE);
            break;
        case SALUS_LINK_CONNECTED:
            set_status(app, status != NULL ? status : "Connected.");
            set_connect_busy(app, FALSE);
            if (was != SALUS_LINK_CONNECTED) {
                switch_to_dashboard(app);
            }
            break;
        case SALUS_LINK_DISCONNECTED:
            set_status(app, status != NULL ? status : "Disconnected");
            set_connect_busy(app, FALSE);
            app->has_live_steps = FALSE;
            app->has_live_hr = FALSE;
            salus_location_stop(app, "device disconnected");
            salus_notifications_on_disconnect(app);
            if (was == SALUS_LINK_CONNECTED) {
                switch_to_pairing(app);
            }
            break;
    }
    salus_dashboard_schedule_refresh(app);
}

static void device_free(SalusDevice *dev) {
    if (dev != NULL && dev->ops->free != NULL) {
        dev->ops->free(dev);
    }
}

/* ---- Saved devices ---------------------------------------------------------------------- */

static GPtrArray *saved(SalusApp *app) {
    if (app->saved_devices == NULL) {
        app->saved_devices = g_ptr_array_new_with_free_func((GDestroyNotify)salus_device_config_free);
    }
    return app->saved_devices;
}

static gint saved_index(SalusApp *app, const char *address) {
    GPtrArray *list = saved(app);
    for (guint i = 0; address != NULL && i < list->len; i++) {
        if (g_ascii_strcasecmp(((SalusDeviceConfig *)g_ptr_array_index(list, i))->address, address) == 0) {
            return (gint)i;
        }
    }
    return -1;
}

const SalusDeviceConfig *salus_app_saved_device(SalusApp *app, const char *address) {
    const gint i = saved_index(app, address);
    return i >= 0 ? g_ptr_array_index(app->saved_devices, i) : NULL;
}

void salus_app_save_device(SalusApp *app, const SalusDeviceConfig *cfg) {
    const gint i = saved_index(app, cfg->address);
    SalusDeviceConfig *copy = salus_device_config_copy(cfg);
    if (i >= 0) {
        salus_device_config_free(g_ptr_array_index(app->saved_devices, i));
        g_ptr_array_index(app->saved_devices, i) = copy;
    } else {
        g_ptr_array_add(saved(app), copy);
    }
}

void salus_app_forget_device(SalusApp *app, const char *address) {
    const gint i = saved_index(app, address);
    if (i < 0) {
        return;
    }
    const gboolean was_active =
        app->device != NULL && g_ascii_strcasecmp(app->device->config->address, address) == 0;
    g_ptr_array_remove_index(app->saved_devices, (guint)i);
    if (was_active) {
        if (app->saved_devices->len > 0) {
            salus_app_activate_device(app, ((SalusDeviceConfig *)g_ptr_array_index(app->saved_devices, 0))->address);
        } else {
            salus_app_set_device(app, NULL);
        }
    }
}

gboolean salus_app_activate_device(SalusApp *app, const char *address) {
    const SalusDeviceConfig *cfg = salus_app_saved_device(app, address);
    if (cfg == NULL) {
        return FALSE;
    }
    salus_app_set_device(app, salus_device_config_copy(cfg));
    return TRUE;
}

gchar *salus_device_config_display_name(const SalusDeviceConfig *cfg) {
    if (cfg->name != NULL && cfg->name[0] != '\0') {
        return g_strdup(cfg->name);
    }
    gchar *bluez = bluez_device_display_name(cfg->address);
    if (bluez != NULL && bluez[0] != '\0') {
        return bluez;
    }
    g_free(bluez);
    return g_strdup(cfg->address);
}

/* ---- Active device ---------------------------------------------------------------------- */

void salus_app_set_device(SalusApp *app, SalusDeviceConfig *cfg) {
    salus_location_stop(app, NULL);
    if (app->device != NULL) {
        SalusDevice *old = app->device;
        app->device = NULL; /* events from the old session during its teardown are ignored */
        device_free(old);
    }
    const SalusDriver *driver = cfg != NULL ? salus_driver_find(cfg->driver_id) : NULL;
    if (driver != NULL) {
        app->device = driver->create(app, cfg);
    } else if (cfg != NULL) {
        g_warning("No driver \"%s\" for %s", cfg->driver_id, cfg->address);
    }
    g_free(app->selected_address);
    app->selected_address = cfg != NULL ? g_strdup(cfg->address) : NULL;
    g_free(app->device_display_name);
    app->device_display_name = cfg != NULL ? salus_device_config_display_name(cfg) : NULL;
    salus_device_config_free(cfg);
}

gboolean salus_device_connected(SalusApp *app) {
    return app->device != NULL && app->device->link == SALUS_LINK_CONNECTED;
}

gboolean salus_device_has(SalusApp *app, guint caps) {
    return app->device != NULL && (app->device->driver->caps & caps) == caps;
}

void salus_device_connect(SalusApp *app) {
    if (app->device != NULL && app->device->ops->connect != NULL) {
        app->device->ops->connect(app->device);
    }
}

void salus_device_disconnect(SalusApp *app) {
    if (app->device != NULL && app->device->ops->disconnect != NULL) {
        app->device->link = SALUS_LINK_DISCONNECTED; /* asked for: not a "connection lost" */
        app->device->ops->disconnect(app->device);
    }
}

void salus_device_sync(SalusApp *app) {
    if (salus_device_connected(app) && app->device->ops->sync != NULL) {
        app->device->ops->sync(app->device);
    }
}

gboolean salus_device_find(SalusApp *app, gboolean start) {
    return salus_device_connected(app) && app->device->ops->find_device != NULL &&
           app->device->ops->find_device(app->device, start);
}
