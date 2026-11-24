/* Device driver layer: registry, link events, saved devices and their config. A fake driver
 * stands in for real hardware; the UI hooks the core calls are recorded by stubs. */

#include "salus/app.h"
#include "salus/config.h"
#include "salus/device.h"
#include "salus/store/db.h"
#include "salus/weight_log.h"

#include <glib/gstdio.h>
#include <string.h>

/* ---- Stubs for the UI/services the core notifies ------------------------------------------ */

static int n_dashboard, n_pairing, busy = -1;
static gchar *last_status;

void set_status(SalusApp *s, const gchar *t) { (void)s; g_free(last_status); last_status = g_strdup(t); }
void set_connect_busy(SalusApp *s, gboolean b) { (void)s; busy = b; }
void switch_to_dashboard(SalusApp *s) { (void)s; n_dashboard++; }
void switch_to_pairing(SalusApp *s) { (void)s; n_pairing++; }
void salus_dashboard_schedule_refresh(SalusApp *s) { (void)s; }
void salus_location_stop(SalusApp *s, const char *r) { (void)s; (void)r; }
void salus_notifications_on_disconnect(SalusApp *s) { (void)s; }
gchar *bluez_device_display_name(const gchar *a) { (void)a; return NULL; }
void append_log(SalusApp *s, const gchar *l) { (void)s; (void)l; }

/* ---- Fake driver ------------------------------------------------------------------------- */

typedef struct {
    SalusDevice base;
    int connects, syncs;
    gboolean *freed;
} FakeDevice;

static gboolean fake_freed;

static void fake_connect(SalusDevice *dev) {
    ((FakeDevice *)dev)->connects++;
    salus_device_set_link(dev, SALUS_LINK_CONNECTING, NULL);
    salus_device_set_link(dev, SALUS_LINK_CONNECTED, NULL);
}
static void fake_disconnect(SalusDevice *dev) { salus_device_set_link(dev, SALUS_LINK_DISCONNECTED, NULL); }
static void fake_sync(SalusDevice *dev) { ((FakeDevice *)dev)->syncs++; }
static void fake_free(SalusDevice *dev) {
    fake_freed = TRUE;
    salus_device_finalize(dev);
    g_free(dev);
}
static const SalusDeviceOps fake_ops = {
    .connect = fake_connect, .disconnect = fake_disconnect, .sync = fake_sync, .free = fake_free};

static gboolean key_valid(const char *v) { return v != NULL && strlen(v) == 4; }
static const SalusPairingField fake_fields[] = {{.key = "pin", .title = "PIN", .validate = key_valid}};
static SalusDevice *fake_create(SalusApp *app, const SalusDeviceConfig *cfg);
static const SalusDriver fake_driver = {
    .id = "fake-strap",
    .vendor = "Example",
    .model = "Strap",
    .kind = SALUS_KIND_OTHER,
    .caps = SALUS_CAP_HEART_RATE | SALUS_CAP_BATTERY,
    .fields = fake_fields,
    .n_fields = 1,
    .create = fake_create,
};
static SalusDevice *fake_create(SalusApp *app, const SalusDeviceConfig *cfg) {
    FakeDevice *d = g_new0(FakeDevice, 1);
    salus_device_init(&d->base, &fake_driver, &fake_ops, app, cfg);
    return &d->base;
}

/* Test registry (replaces src/devices/registry.c). */
static const SalusDriver *const drivers[] = {&fake_driver, NULL};
const SalusDriver *const *salus_drivers(void) { return drivers; }
const SalusDriver *salus_driver_find(const char *id) {
    return g_strcmp0(id, fake_driver.id) == 0 ? &fake_driver : NULL;
}
const char *salus_device_kind_title(SalusDeviceKind k) { (void)k; return "Other"; }

/* ---- Helpers ----------------------------------------------------------------------------- */

static SalusApp *app_new(void) {
    SalusApp *app = g_new0(SalusApp, 1);
    g_mutex_init(&app->log_lock);
    g_mutex_init(&app->recorded_lock);
    return app;
}

static void app_free(SalusApp *app) {
    salus_app_set_device(app, NULL);
    g_clear_pointer(&app->saved_devices, g_ptr_array_unref);
    g_free(app->selected_address);
    g_free(app->device_display_name);
    g_free(app->weather_location_name);
    g_free(app->location_unavailable_reply);
    if (app->weight_log != NULL) {
        g_array_unref(app->weight_log);
    }
    g_free(app);
}

static void write_config(const char *text) {
    const char *path = salus_config_default_path();
    gchar *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_assert_true(g_file_set_contents(path, text, -1, NULL));
}

/* ---- Tests ------------------------------------------------------------------------------- */

static void test_link_and_save_on_connect(void) {
    write_config("");
    SalusApp *app = app_new();
    SalusDeviceConfig *cfg = salus_device_config_new("AA:BB:CC:DD:EE:01", "fake-strap");
    salus_device_config_set(cfg, "pin", "1234");
    salus_app_set_device(app, cfg);
    g_assert_nonnull(app->device);
    g_assert_cmpstr(app->selected_address, ==, "AA:BB:CC:DD:EE:01");
    g_assert_true(salus_device_has(app, SALUS_CAP_HEART_RATE));
    g_assert_false(salus_device_has(app, SALUS_CAP_SLEEP));
    g_assert_null(salus_app_saved_device(app, "AA:BB:CC:DD:EE:01")); /* not saved before it connects */

    n_dashboard = 0;
    salus_device_connect(app);
    g_assert_true(salus_device_connected(app));
    g_assert_cmpint(busy, ==, FALSE);
    g_assert_cmpint(n_dashboard, ==, 1);
    const SalusDeviceConfig *saved = salus_app_saved_device(app, "aa:bb:cc:dd:ee:01"); /* case-insensitive */
    g_assert_nonnull(saved);
    g_assert_cmpstr(salus_device_config_get(saved, "pin"), ==, "1234");

    salus_device_sync(app);
    g_assert_cmpint(((FakeDevice *)app->device)->syncs, ==, 1);

    /* An asked-for disconnect isn't a "connection lost". */
    n_pairing = 0;
    salus_device_disconnect(app);
    g_assert_false(salus_device_connected(app));
    g_assert_cmpint(n_pairing, ==, 0);
    salus_device_sync(app); /* no-op while disconnected */
    g_assert_cmpint(((FakeDevice *)app->device)->syncs, ==, 1);

    /* A dropped connection is. */
    salus_device_connect(app);
    salus_device_set_link(app->device, SALUS_LINK_DISCONNECTED, "lost");
    g_assert_cmpint(n_pairing, ==, 1);
    g_assert_cmpstr(last_status, ==, "lost");

    fake_freed = FALSE;
    app_free(app);
    g_assert_true(fake_freed);
}

static void test_config_round_trip(void) {
    write_config("[weight]\nlog=20261120:61.5\ncurrent_kg=61.5\ncurrent_date=20261120\n");
    SalusApp *app = app_new();
    for (int i = 0; i < 2; i++) {
        gchar *addr = g_strdup_printf("AA:BB:CC:DD:EE:0%d", i + 1);
        SalusDeviceConfig *cfg = salus_device_config_new(addr, "fake-strap");
        cfg->name = g_strdup_printf("Strap %d", i + 1);
        salus_device_config_set(cfg, "pin", i == 0 ? "1111" : "2222");
        salus_app_save_device(app, cfg);
        salus_device_config_free(cfg);
        g_free(addr);
    }
    g_assert_true(salus_app_activate_device(app, "AA:BB:CC:DD:EE:02"));
    g_assert_cmpstr(app->device_display_name, ==, "Strap 2");
    g_assert_true(salus_config_save(app, NULL));
    app_free(app);

    SalusApp *loaded = app_new();
    g_assert_true(salus_config_load(loaded, NULL));
    salus_weight_log_load(loaded);
    g_assert_cmpuint(loaded->saved_devices->len, ==, 2);
    g_assert_nonnull(loaded->device);
    g_assert_cmpstr(loaded->device->config->address, ==, "AA:BB:CC:DD:EE:02");
    g_assert_cmpstr(salus_device_config_get(loaded->device->config, "pin"), ==, "2222");
    g_assert_cmpuint(loaded->weight_log->len, ==, 1); /* other groups survive a save */

    /* Forgetting the active device activates the next one, then none. */
    salus_app_forget_device(loaded, "AA:BB:CC:DD:EE:02");
    g_assert_cmpstr(loaded->device->config->address, ==, "AA:BB:CC:DD:EE:01");
    salus_app_forget_device(loaded, "AA:BB:CC:DD:EE:01");
    g_assert_null(loaded->device);
    g_assert_cmpuint(loaded->saved_devices->len, ==, 0);
    app_free(loaded);
}

static void test_legacy_config(void) {
    /* miwear-band's single-device config becomes a saved Xiaomi band. */
    write_config("[device]\naddress=74:FB:17:28:37:42\nauth_key=00112233445566778899aabbccddeeff\n");
    SalusApp *app = app_new();
    /* This test registry has no Xiaomi driver: the entry is kept, just not activated. */
    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "No driver \"xiaomi-band\"*");
    g_assert_true(salus_config_load(app, NULL));
    g_test_assert_expected_messages();
    g_assert_null(app->device);
    const SalusDeviceConfig *cfg = salus_app_saved_device(app, "74:FB:17:28:37:42");
    g_assert_nonnull(cfg);
    g_assert_cmpstr(cfg->driver_id, ==, "xiaomi-band");
    g_assert_cmpstr(salus_device_config_get(cfg, "auth_key"), ==, "00112233445566778899aabbccddeeff");
    g_assert_true(salus_config_save(app, NULL));
    gchar *text = NULL;
    g_assert_true(g_file_get_contents(salus_config_default_path(), &text, NULL, NULL));
    g_assert_null(strstr(text, "[device]\n"));
    g_assert_nonnull(strstr(text, "[device 74:FB:17:28:37:42]"));
    g_free(text);
    app_free(app);
}

int main(int argc, char **argv) {
    gchar *home = g_dir_make_tmp("salus-driver-test-XXXXXX", NULL);
    gchar *cfg = g_build_filename(home, "config", NULL);
    gchar *data = g_build_filename(home, "data", NULL);
    g_setenv("XDG_CONFIG_HOME", cfg, TRUE);
    g_setenv("XDG_DATA_HOME", data, TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/driver/link-and-save-on-connect", test_link_and_save_on_connect);
    g_test_add_func("/driver/config-round-trip", test_config_round_trip);
    g_test_add_func("/driver/legacy-config", test_legacy_config);
    const int rc = g_test_run();
    salus_db_close();
    gchar *rm = g_strdup_printf("rm -rf '%s'", home);
    (void)system(rm);
    g_free(rm);
    g_free(cfg);
    g_free(data);
    g_free(home);
    return rc;
}
