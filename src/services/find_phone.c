#include "salus/services/find_phone.h"
#include "salus/ui/dashboard.h"

#include <gio/gio.h>
#include <glib.h>

static GSubprocess *s_ring_proc = NULL;

static const char *pick_ring_sound_path(void) {
    static const char *const candidates[] = {
        "/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga",
        "/usr/share/sounds/freedesktop/stereo/phone-incoming-call.wav",
        "/usr/share/sounds/freedesktop/stereo/bell.oga",
        "/usr/share/sounds/sound-icons/phone-incoming-call.wav",
        "/usr/share/sounds/alsa/Front_Left.wav",
        NULL,
    };
    for (guint i = 0; candidates[i] != NULL; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
            return candidates[i];
        }
    }
    return NULL;
}

static gchar *build_ring_loop_shell(const char *sound_path) {
    if (sound_path != NULL) {
        /* Prefer PipeWire/Pulse (paplay), then ALSA (aplay), then libcanberra theme. */
        return g_strdup_printf(
            "S=%s; export S; "
            "while true; do "
            "if command -v paplay >/dev/null 2>&1; then paplay \"$S\" && continue; fi; "
            "if command -v aplay >/dev/null 2>&1; then aplay -q \"$S\" && continue; fi; "
            "if command -v canberra-gtk-play >/dev/null 2>&1; then canberra-gtk-play -i phone-incoming-call && continue; fi; "
            "sleep 1; "
            "done",
            sound_path);
    }
    return g_strdup(
        "while true; do "
        "if command -v canberra-gtk-play >/dev/null 2>&1; then canberra-gtk-play -i phone-incoming-call && continue; fi; "
        "printf '\\a'; sleep 2; "
        "done");
}

static void stop_ring_audio(void) {
    if (s_ring_proc != NULL) {
        g_subprocess_force_exit(s_ring_proc);
        g_clear_object(&s_ring_proc);
    }
}

static void start_ring_audio(SalusApp *state) {
    stop_ring_audio();

    const char *sound = pick_ring_sound_path();
    gchar *shell = build_ring_loop_shell(sound);
    GError *err = NULL;

    s_ring_proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDERR_SILENCE | G_SUBPROCESS_FLAGS_STDOUT_SILENCE,
        &err,
        "sh",
        "-c",
        shell,
        NULL);
    g_free(shell);

    if (s_ring_proc == NULL) {
        gchar *line = g_strdup_printf(
            "Find phone: could not start ring audio (%s).",
            err != NULL ? err->message : "unknown error");
        append_log(state, line);
        g_free(line);
        g_clear_error(&err);
        return;
    }

    if (sound != NULL) {
        gchar *line = g_strdup_printf("Find phone: ringing with paplay/aplay (%s).", sound);
        append_log(state, line);
        g_free(line);
    } else {
        append_log(state, "Find phone: ringing with canberra-gtk-play / terminal bell (no sound file found).");
    }
}

static void stop_beep(SalusApp *state) {
    if (state == NULL) {
        return;
    }
    stop_ring_audio();
    state->find_phone_active = FALSE;
}

void salus_find_phone_shutdown(SalusApp *state) {
    stop_beep(state);
}

void salus_find_phone_on_band_request(SalusApp *state, guint find_device) {
    if (state == NULL) {
        return;
    }
    if (find_device == 0) {
        if (!state->find_phone_active) {
            append_log(state, "Find phone: band requested ring (Mi Fit find phone).");
        }
        state->find_phone_active = TRUE;
        start_ring_audio(state);
        set_status(state, "Find phone: ringing — use Stop on Device tab or dismiss on band.");
        salus_dashboard_schedule_refresh(state);
    } else {
        append_log(state, "Find phone: band stopped ring.");
        stop_beep(state);
        set_status(state, "Find phone stopped.");
        salus_dashboard_schedule_refresh(state);
    }
}

void salus_find_phone_dismiss(SalusApp *state) {
    if (state == NULL || !state->find_phone_active) {
        return;
    }
    stop_beep(state);
    if (salus_device_connected(state) && state->device->ops->find_phone_stopped != NULL) {
        state->device->ops->find_phone_stopped(state->device);
    }
    append_log(state, "Find phone: stopped from Linux app.");
    set_status(state, "Find phone stopped.");
    salus_dashboard_schedule_refresh(state);
}

gboolean salus_find_phone_is_active(const SalusApp *state) {
    return state != NULL && state->find_phone_active;
}
