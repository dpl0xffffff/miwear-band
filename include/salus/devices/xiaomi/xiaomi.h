#ifndef SALUS_DEVICES_XIAOMI_H
#define SALUS_DEVICES_XIAOMI_H

/*
 * Xiaomi Smart Band 9 / 10 driver: Bluetooth Classic RFCOMM, SPPv2 framing, AES-CTR session after
 * the Mi Fitness auth handshake, protobuf commands, activity files decoded with Mi Fitness's own
 * schemas. See docs/MIFITNESS_SYNC.md.
 */

#include "salus/app.h"
#include "salus/device.h"

#include <bluetooth/bluetooth.h>

extern const SalusDriver xiaomi_band_driver;

/* The driver's session; the active SalusApp::device when a Xiaomi band is selected. */
typedef struct {
    SalusDevice base;

    /* RFCOMM socket, read from a main-loop fd watch; only the blocking connect runs on a worker
     * thread, which hands the fd back via the main loop. */
    int sock_fd;
    guint rx_watch_id;
    int protocol_version;

    /* Auth + session crypto. */
    gchar auth_key_hex[33];
    uint8_t phone_nonce[16];
    uint8_t watch_nonce[16];
    uint8_t decryption_key[16];
    uint8_t encryption_key[16];
    uint8_t decryption_nonce[4];
    uint8_t encryption_nonce[4];
    gboolean auth_initialized;
    uint8_t spp_v2_seq;

    GByteArray *rx_accumulator;
    GByteArray *activity_chunk_accumulator;

    /* Activity file fetch queue. */
    GQueue *activity_fetch_queue;
    GHashTable *activity_fetch_seen;
    gboolean activity_fetch_inflight;
    uint8_t activity_fetch_current_id[7];
    guint activity_fetch_timeout_source_id;
    guint activity_refetch_source_id;
    guint activity_refetch_round;
    guint activity_live_poll_source_id;
    guint activity_live_poll_round;
    gboolean activity_past_requested;
    gboolean sleep_hint_logged;
    gboolean manual_stress_probe_done;
    uint8_t manual_listed_file_id[7];
    gboolean manual_listed_file_id_valid;

    guint find_band_stop_source;

    gboolean verbose_wire_log;  /* SALUS_VERBOSE_WIRE_LOG */
    gboolean verbose_fetch_log; /* SALUS_FETCH_DEBUG */
} XiaomiDevice;

/* The Xiaomi session of @app. Only valid while a Xiaomi device is active: the driver's code only
 * runs then (its sources and watches are removed when the session is freed). */
static inline XiaomiDevice *xiaomi(SalusApp *app) {
    return (XiaomiDevice *)app->device;
}

/* Starts connecting to the session's address on a worker thread (wire.c). */
void xiaomi_wire_connect(SalusApp *state);

/* Pairing field validation: 32 hex characters. */
gboolean xiaomi_auth_key_valid(const char *value);

#endif
