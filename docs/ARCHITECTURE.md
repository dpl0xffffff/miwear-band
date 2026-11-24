# Architecture

Salus is split into a device-independent core and device drivers. The core owns the UI, the
health data store and the services every device can use; a driver knows one family of devices:
how to connect, authenticate, sync and talk to it.

```
          ┌──────────────────────── core ────────────────────────┐
 user ──► │ src/ui        Health · Sleep · Workouts · Device,    │
          │               connect flow (connect.c)               │
          │ src/core      SalusApp, config + saved devices,      │
          │               active device (device.c), migration    │
          │ src/store     SQLite (db.c), day timelines,          │
          │               workout/sleep model                    │
          │ src/services  weather · location · notifications ·   │
          │               find phone                             │
          └───────────▲──────────────────────────┬───────────────┘
        events, store │                          │ SalusDeviceOps
        writes        │                          ▼
          ┌───────────┴─────── src/devices ───────────────────────┐
          │ registry.c   list of drivers                          │
          │ xiaomi/      Xiaomi Smart Band 9/10: RFCOMM, SPPv2,   │
          │              auth, Mi Fitness file decoding, encoders │
          └───────────────────────────────────────────────────────┘
```

Everything runs on the GTK main thread; drivers do blocking work (connecting) on worker threads
and hand results back through the main loop. `append_log()`, `set_status()` and
`set_connect_busy()` are the only calls that are safe from any thread.

## Layout

| Path | Contents |
|------|----------|
| `include/salus/device.h` | Driver interface: `SalusDriver`, `SalusDeviceOps`, `SalusDevice`, capabilities, pairing fields, saved devices |
| `src/core/` | `device.c` (active device, link events, saved devices), `config.c` (`config.ini`), `migrate.c` (copy miwear-band files), profile, weight log, BlueZ helpers |
| `src/store/` | `db.c` (SQLite, Gadgetbridge-compatible tables, per-device rows), `timeline.c` (per-minute day timelines, rebuild from DB), `workout.c`/`sleep.c` (data model), `health_data.c` (day aggregates) |
| `src/services/` | `weather.c` (Open-Meteo), `location.c` (GeoClue), `notifications.c` (desktop capture), `find_phone.c` (ringer) |
| `src/ui/` | libadwaita UI; `connect.c` is the Add Device flow |
| `src/devices/registry.c` | The drivers Salus knows |
| `src/devices/xiaomi/` | The Xiaomi driver (`driver.c` describes it; `wire.c` transport + auth; `activity.c`, `mi_fitness_*` file decoding; `weather.c`/`notify.c`/`workout_gps.c` protocol encoders) |
| `src/proto/` | `xiaomi.pb-c` generated code |

## Data model

Data is stored per device (`DEVICE` row per Bluetooth address; every sample row carries its
`deviceId`) and the UI shows the active device's data. Drivers write through the store:

- per-minute samples: `salus_db_replace_xiaomi_activity_batch()` (steps, heart rate, SpO₂, stress,
  calories, intensity) + `recorded_merge_xiaomi_activity_rows()` for the live timeline;
- daily totals: `salus_db_replace_daily_summary()` + `health_apply_daily_summary()`;
- sleep: `salus_db_replace_sleep_details_header()` (session + stage totals),
  `salus_db_replace_sleep_stage_samples()` (timeline, `SALUS_SLEEP_STAGE_*` values);
- workouts: `salus_sport_add_entry()` / `salus_sport_merge_attachment()` with a `SalusWorkoutSummary`;
- live values while connected: `salus_live_metrics_update()`, `schedule_state_update()`.

The table names come from Gadgetbridge (`XIAOMI_*`) and are kept for compatibility; they hold
data from any driver.

## Adding a device driver

1. **Create `src/devices/<vendor>/`** with a session struct that embeds `SalusDevice` first:
   ```c
   typedef struct { SalusDevice base; /* your connection state */ } AcmeDevice;
   ```
2. **Describe the driver** (`const SalusDriver acme_watch_driver`): stable `id` (stored in
   config), `vendor`, `model`, `icon`, `kind` (band/watch/ring/other), `caps` — only what the
   device really provides, since the UI hides everything else — `bt_name_patterns` to preselect it
   in the Bluetooth list, and `fields` for what the user enters when pairing (with `validate`).
3. **Implement `create`** (`salus_device_init()`, read your fields with
   `salus_device_config_get()`) and the ops you support: `connect`, `disconnect`, `sync`, `free`
   (remove every GSource, call `salus_device_finalize()`), plus optional `find_device`,
   `find_phone_stopped`, `send_notification`, `push_weather`, `settings_changed`.
4. **Report the link** with `salus_device_set_link()`: CONNECTING while connecting, CONNECTED once
   the device is usable (the core then saves it and shows the dashboard), DISCONNECTED with a
   reason on failure or loss.
5. **Write data** through the store calls above; map your sleep stages to `SALUS_SLEEP_STAGE_*`.
6. **Register it**: one line in `src/devices/registry.c`, its sources in `CMakeLists.txt`.
7. **Test it** like `test/driver_test.c` (a fake driver exercising link events and config) and
   with decoding tests for recorded payloads (`test/parser_test.c`, `test/report_test.c`).

Services the driver can use instead of reimplementing: `salus_weather_refresh()` (fetches and calls
`push_weather`), `salus_location_start()` (GeoClue fixes for connected GPS),
`salus_find_phone_on_band_request()` (ring the computer).

See [MIFITNESS_SYNC.md](MIFITNESS_SYNC.md) for the Xiaomi sync semantics.
