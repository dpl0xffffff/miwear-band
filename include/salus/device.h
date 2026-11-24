#ifndef SALUS_DEVICE_H
#define SALUS_DEVICE_H

/*
 * Device drivers.
 *
 * A driver (SalusDriver) describes one family of devices: what it is, which data and features it
 * offers (caps), and what the user must enter to pair it (fields, e.g. an auth key). For the
 * active device the core creates a SalusDevice through driver->create(); the driver embeds that
 * struct in its own session struct and implements SalusDeviceOps.
 *
 * Everything runs on the GTK main thread. Drivers report back through the salus_device_*()
 * events below and write data through the store (salus/store/…, recorded_*()).
 * docs/ARCHITECTURE.md has a checklist for adding a driver.
 */

#include <glib.h>

typedef struct SalusApp SalusApp;
typedef struct SalusDevice SalusDevice;

typedef enum {
    SALUS_KIND_BAND,
    SALUS_KIND_WATCH,
    SALUS_KIND_RING,
    SALUS_KIND_OTHER,
} SalusDeviceKind;

/* What a device can provide; the UI only shows tiles, tabs and actions the active device has. */
typedef enum {
    SALUS_CAP_HEART_RATE = 1u << 0,
    SALUS_CAP_SPO2 = 1u << 1,
    SALUS_CAP_STRESS = 1u << 2,
    SALUS_CAP_STEPS = 1u << 3,       /* steps, calories, active minutes */
    SALUS_CAP_SLEEP = 1u << 4,
    SALUS_CAP_WORKOUTS = 1u << 5,
    SALUS_CAP_VITALITY = 1u << 6,
    SALUS_CAP_LIVE_STATS = 1u << 7,  /* live heart rate / steps while connected */
    SALUS_CAP_BATTERY = 1u << 8,
    SALUS_CAP_NOTIFICATIONS = 1u << 9,
    SALUS_CAP_WEATHER = 1u << 10,
    SALUS_CAP_FIND_DEVICE = 1u << 11,
    SALUS_CAP_FIND_PHONE = 1u << 12,
    SALUS_CAP_LOCATION_SHARE = 1u << 13,
    SALUS_CAP_USER_PROFILE = 1u << 14, /* height/weight/birthday are sent to the device */
} SalusCapability;

typedef enum {
    SALUS_LINK_DISCONNECTED,
    SALUS_LINK_CONNECTING,
    SALUS_LINK_CONNECTED, /* authenticated and usable */
} SalusLinkState;

/* Something the user enters when pairing (stored in the device's config group). */
typedef struct {
    const char *key;   /* config key, e.g. "auth_key" */
    const char *title; /* "Auth Key" */
    const char *help;  /* one or two sentences shown under the field */
    gboolean secret;   /* password entry */
    gboolean (*validate)(const char *value);
} SalusPairingField;

/* A saved device: Bluetooth address, driver, name and the driver's pairing fields. */
typedef struct {
    gchar *address;   /* "AA:BB:CC:DD:EE:FF" */
    gchar *driver_id;
    gchar *name;      /* shown in the UI; may be NULL */
    GHashTable *fields; /* field key → value (owned) */
} SalusDeviceConfig;

SalusDeviceConfig *salus_device_config_new(const char *address, const char *driver_id);
SalusDeviceConfig *salus_device_config_copy(const SalusDeviceConfig *cfg);
void salus_device_config_free(SalusDeviceConfig *cfg);
const char *salus_device_config_get(const SalusDeviceConfig *cfg, const char *key);
void salus_device_config_set(SalusDeviceConfig *cfg, const char *key, const char *value);

/* A notification to show on the device. */
typedef struct {
    guint32 id;
    const char *app_id;   /* reverse-DNS id of the sending app */
    const char *app_name;
    const char *title;
    const char *body;
    gint64 when_unix_sec; /* 0 = now */
} SalusNotification;

typedef struct {
    void (*connect)(SalusDevice *dev);
    void (*disconnect)(SalusDevice *dev);
    /* Fetch everything new from the device. */
    void (*sync)(SalusDevice *dev);
    /* Make the device ring/vibrate (start) or stop it. */
    gboolean (*find_device)(SalusDevice *dev, gboolean start);
    /* The user stopped the "find my phone" ringer on the computer. */
    void (*find_phone_stopped)(SalusDevice *dev);
    gboolean (*send_notification)(SalusDevice *dev, const SalusNotification *n);
    /* Settings that live on the device changed (profile, notification forwarding, …). */
    void (*settings_changed)(SalusDevice *dev);
    /* Disconnects if needed, cancels timers and frees the session. */
    void (*free)(SalusDevice *dev);
} SalusDeviceOps;

typedef struct SalusDriver {
    const char *id;     /* stable, stored in config: "xiaomi-band" */
    const char *vendor; /* "Xiaomi" */
    const char *model;  /* "Smart Band 9 / 10" */
    const char *icon;   /* icon name */
    SalusDeviceKind kind;
    guint caps;         /* SalusCapability flags */
    /* Bluetooth names (glob patterns) that are probably this device, to preselect it. */
    const char *const *bt_name_patterns;
    const SalusPairingField *fields;
    guint n_fields;
    /* One line for the connect page, e.g. where to find the auth key. */
    const char *pairing_help;
    SalusDevice *(*create)(SalusApp *app, const SalusDeviceConfig *cfg);
} SalusDriver;

struct SalusDevice {
    const SalusDriver *driver;
    const SalusDeviceOps *ops;
    SalusApp *app;
    SalusDeviceConfig *config; /* owned */
    SalusLinkState link;
};

/* Registry (src/devices/registry.c). */
const SalusDriver *const *salus_drivers(void); /* NULL-terminated */
const SalusDriver *salus_driver_find(const char *id);
const char *salus_device_kind_title(SalusDeviceKind kind); /* "Smart bands", … */

/* For driver->create(): fills the common part. */
void salus_device_init(SalusDevice *dev, const SalusDriver *driver, const SalusDeviceOps *ops, SalusApp *app,
                       const SalusDeviceConfig *cfg);
/* For ops->free(): releases the common part. */
void salus_device_finalize(SalusDevice *dev);

/* Driver → core events. */
void salus_device_set_link(SalusDevice *dev, SalusLinkState link, const char *status);

/* Core helpers for the active device (all safe with no active device). */
SalusDevice *salus_app_device(SalusApp *app);
/* Makes @cfg the active device (taking ownership), replacing and disconnecting the previous one. */
void salus_app_set_device(SalusApp *app, SalusDeviceConfig *cfg);
gboolean salus_device_connected(SalusApp *app);
gboolean salus_device_has(SalusApp *app, guint caps);
void salus_device_connect(SalusApp *app);
void salus_device_disconnect(SalusApp *app);
void salus_device_sync(SalusApp *app);
gboolean salus_device_find(SalusApp *app, gboolean start);

#endif
