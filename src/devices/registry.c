#include "salus/device.h"

#include "salus/devices/xiaomi/xiaomi.h"

#include <string.h>

/* Every supported device family. Adding a driver = one line here (docs/ARCHITECTURE.md). */
static const SalusDriver *const drivers[] = {
    &xiaomi_band_driver,
    NULL,
};

const SalusDriver *const *salus_drivers(void) {
    return drivers;
}

const SalusDriver *salus_driver_find(const char *id) {
    for (const SalusDriver *const *d = drivers; id != NULL && *d != NULL; d++) {
        if (strcmp((*d)->id, id) == 0) {
            return *d;
        }
    }
    return NULL;
}

const char *salus_device_kind_title(SalusDeviceKind kind) {
    switch (kind) {
        case SALUS_KIND_BAND: return "Smart Bands";
        case SALUS_KIND_WATCH: return "Smartwatches";
        case SALUS_KIND_RING: return "Smart Rings";
        default: return "Other Devices";
    }
}
