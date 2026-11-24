# Mi Fitness sync reference (Band 10 / SPP)

Source artifact: `com.xiaomi.wearable` **base.apk** 3.59.x (JADX static analysis).

## Stack

| Layer | Mi Fitness class | miwear-band module |
|-------|------------------|-------------------|
| RFCOMM | `com.xiaomi.wearable.connection.SppConnection` | `src/core/bluez.c`, `src/devices/xiaomi/wire.c` |
| Framing | `transport.TransportV2`, `SppTaskQueueV2` | `wire.c` SPP v2 |
| Auth | `wear.api.WearAuthV2`, label `salus-auth` | `src/devices/xiaomi/crypto.c` |
| Protobuf | `xiaomi` Command (same as Gadgetbridge proto) | `src/proto/xiaomi.pb-c.*` |
| Fitness pull | `FitnessDeviceSyncManager` + `FitnessWearSender` | `src/devices/xiaomi/fitness_sync.c` |
| Binary parse | `FitnessDataProcessor` / `FitnessDataParser` | `src/devices/xiaomi/activity.c` (+ `manual.c`, `workout_summary.c`) |
| Schema map | `assets/schemas.zip` + `SchemaFilesUtilsKt` | `third_party/mi-fitness-schemas/`, **`docs/MIFITNESS_SCHEMA_MAPPING.md`** |
| Persistence | Room / `ISyncDB` | `src/store/db.c` |

## Pull sequence (Mi Fitness)

1. **`FitnessDeviceSyncManager.startSyncWithDevice`**
   - `pbImpl.syncWearSportStatus()`
   - `FitnessWearSender.requestData(did, callback, isBgAutoSync)`

2. **`FitnessWearSender.requestData`**
   - `requestTodayData` → `FitnessDeviceSyncPbApi.getTodayFitnessIds`
   - on success → `requestDataIdIfHas` → per-id fetch on wear link
   - then `requestHistoryData` → `getHistoryFitnessIds`

3. **After each file**
   - `FitnessDeviceSyncManager.confirmFitnessId` → `FitnessDeviceSyncPbApi.confirmFitnessId`
   - Maps to protobuf **health subtype 5** ack (`activitySyncAckFileIds`) in our wire layer.

4. **Parse path**
   - `syncTheData` → `parseBinaryData` → `FitnessDataProcessor.parseAndRecord`
   - Sport summaries: `parseAnRecordSportReport` → `FitnessDataParser.parse` → **schema JSON** if wire version exists in `index.json`, else **legacy Java parser** (see mapping doc).

Quick lookup: `python3 scripts/mi_schema_lookup.py --hex <7-byte-id>` or `--sport 22 --detail 1 --version 6`.

## Protobuf health subtypes (device link)

| Subtype | Role in app | miwear-band |
|---------|-------------|-------------|
| 1 | Today activity file ID list | `send_activity_fetch_today_channels` |
| 2 | Past / history ID list | `send_activity_fetch_past_channels` |
| 3 | Request one file by 7-byte ID | `send_activity_fetch_request` |
| 5 | Ack synced file | `send_activity_fetch_ack` |

## Mi Fitness `FitnessDataParser.getDailyDataParserInstance` (APK 3.59)

| `dailyType` | Parser | Band file id (type=0) |
|-------------|--------|------------------------|
| 0 + fileType 0 | `DailyRecordParser` | subtype 0, detail 0 (minute HR/SpO2/stress/steps) |
| 0 + fileType 1 | `DailyReportParser` | subtype 0, detail 1 (daily summary) |
| 2 | `DaytimeSleepParser` | (legacy nap) |
| 3 | `NightSleepParser` | subtype 8 `SLEEP` |
| 6 | `ManualMeasureParser` | subtype 6 `MANUAL_SAMPLES` |
| 8 | `AllDaySleepParser` | subtype 3 `SLEEP_STAGES` |

`activity.c` mirrors these parsers; results land in SQLite and the recorded charts via `recorded_merge_xiaomi_activity_rows` / `health_apply_daily_summary`. On connect, `recorded_rebuild_from_db` hydrates charts from disk.

## Desktop UI (`src/ui/dashboard.c`)

Mi Fitness **Today Health**–style layout: date pager, metric cards (steps, calories, HR, SpO₂, stress, sleep, standing, vitality), **AdwTabView** tabs (Health / Sleep / Workouts / Device). Sleep tab shows a hypnogram; Device tab has sync + protocol log.

## Gaps vs full Mi Fitness

- **Server/cloud sync** — not in scope.
- **Weight / ECG / temperature / blood pressure cards** — band files logged when IDs appear; full native parsers still TODO (Mi Fitness uses schema/nativeParse).
- **Workout summaries** — Band 10 often uses **wire v6** summaries while Mi Fitness schemas start at **v8+** for `outdoor_foot`; app falls back to `OutdoorStepReportParser` / Gadgetbridge layouts (`workout_summary.c`). Newer versions and **record/GPS** blobs should use **`docs/MIFITNESS_SCHEMA_MAPPING.md`** + `schemas.zip`.
- **`syncWearSportStatus`** — stub in `fitness_sync.c`.
