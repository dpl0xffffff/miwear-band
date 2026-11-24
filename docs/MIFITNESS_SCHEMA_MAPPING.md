# Mi Fitness binary → parser mapping (APK 3.59)

This is the **authoritative conversion table** Mi Fitness uses instead of hand-tuning offsets per workout. It comes from decompiling `base.apk` and vendoring `assets/schemas.zip`.

Artifacts in-repo:

- `third_party/mi-fitness-schemas/index.json` — lookup index
- `third_party/mi-fitness-schemas/schemas.zip` — field layouts (JSON)
- `scripts/mi_schema_lookup.py` — CLI resolver

## 7-byte file ID (same as Gadgetbridge `XiaomiActivityFileId`)

Little-endian, from `FitnessDataId(byte[])`:

| Offset | Field | Notes |
|--------|--------|--------|
| 0–3 | `timestamp` | uint32 Unix seconds |
| 4 | `tzIn15Min` | timezone in 15-minute steps |
| 5 | `version` | **wire version** — used as `v{N}` in schema lookup |
| 6 | flags byte | bit7=`dataType` (0=health, 1=sport); bits6..2=`sportType` or `dailyType`; bits1..0=`fileType` |

```text
fileType (sport): 0=record, 1=report, 2=gps, 3=hrr
fileType (health): 0=record, 1=report
```

Example: **outdoor walk summary** — `sportType=22`, `fileType=1`, `version=6` → Gadgetbridge “Outdoor walk v2” / Mi Fitness `OutdoorStepReportParser`.

## End-to-end parse pipeline

```text
FitnessDataProcessor.parseAndRecord(dataId, payload)
  dataType 0 → parseAndRecordFitnessData
    dailyType 31 → sport path (HM legacy)
    else → parseAndRecordDailyData → FitnessDataParser.parseDailyData
  dataType 1 → parseAndRecordSportData
    fileType 1 → parseAnRecordSportReport → FitnessDataParser.parseSportData(..., 1)
    fileType 0,2,3 → recordFDSData (record stream / GPS / HRR)

FitnessDataParser.parse(dataId, bytes)
  1. getDataParserInstance(dataId)  // Java class switch (below)
  2. if findSchemaFilePath(dataId) != null → schemaParse(JSON + native)
     else → nativeParse(legacy layouts in Java/native code)
```

**Important:** Schema lookup is **exact** on wire `version`. Band 10 outdoor walk summaries often use **v6**, while `index.json` only lists `outdoor_foot` **report v8/v9**. For v6, Mi Fitness uses **`OutdoorStepReportParser` + `nativeParse`**, not the v8 JSON schema.

### How miwear-band decodes workout summaries

`src/devices/xiaomi/mi_fitness_sport_report.c` follows `FitnessDataParser.parse` for every sport type 1–26:

1. If `index.json` has a schema for the exact `(sport, report, version)`, the schema is turned into a
   field table at runtime (field names → report ids via `schema_field_ids.json`).
2. Otherwise the sport's native table (`*ReportParser.reportDataTypeArray`) is used, with the
   validity length from `FitnessDataValidity`.
3. The body is read by `mi_fitness_oneden.c` (`OneDimenDataParser`), and every value is applied by
   its global report field id (`SportReportBaseParser.ReportType`, e.g. 16 avg HR, 110 training load).
   Values without a dedicated summary field are kept in `SalusWorkoutSummary.metrics`.

All tables are generated from the app — see `tools/mi-fitness-extract/README.md`. Where Mi Fitness
has both a schema and a native table they agree field-for-field (checked by `test/report_test.c`),
with one exception in the app itself: the native tables add the cloud course id for
`selectedCourse == 251`, the schemas only for 252/253/255.

Field 65 (`subSportType`) carries the specific sport (`FitnessSportType`, e.g. 1 outdoor run inside
file type 22); `sport_types.json` names all 180 of them.

Older versions that Mi Fitness itself cannot decode fall back to the Gadgetbridge-style parsers in
`workout_summary.c`.

## Schema resolution

1. `SchemaFilesUtilsKt.getSchemaInfo(dataId)` → `(activityFolder, fileType, "v" + version)`
2. `index.json[activity][fileType]["vN"]` → path inside `schemas.zip`
3. `ParserWayControl.getSchema` loads JSON; `schemaParse` runs native schema engine

`index.json` also defines `$types` for semantic names (e.g. sport `22` → `outdoor_foot`).

### Sport wire subtype → schema folder (`$types.activityType.sport`)

| Subtype | Schema folder | Report versions (index) | Record versions |
|--------:|----------------|-------------------------|-----------------|
| 3 | indoor_running | v13, v14 | v9, v10 |
| 7 | indoor_cycling | v10 | v6 |
| 8 | free_training | v11–v14 | v4–v6 |
| 9–10 | swimming | v8 | v3 |
| 11 | elliptical_training | v7 | v3 |
| 12 | yoga | v7 | v1 |
| 13 | rowing_workout | v8 | v4 |
| 14 | rope_skipping | v7 | v6 |
| 16 | hiit | v7 | v2 |
| 18 | ball_games | v5 | v1 |
| 19 | basketball | v5 | v1 |
| 20 | golf | v4 | v1 |
| 21 | skiing | v6 | v4 |
| **22** | **outdoor_foot** | **v8, v9** | **v9** |
| 23 | outdoor_non_foot | v8, v9 | v6, v7 |
| 24 | climbing | v2 | v1, v2 |
| 25 | diving | v2 | v2 |
| 26 | esports | v1 | v1 |

Outdoor foot GPS schemas: v3–v5. HRR uses shared `hrr/hrr_v1.json`.

Subtypes **1, 2, 4, 5, 6, 15, 17** have Java parsers but **no** dedicated folder in this `index.json` slice (legacy outdoor run/bike/hike/triathlon paths).

### Health `dailyType` → schema folder

| dailyType | Schema folder | Notes |
|----------:|---------------|--------|
| 0 | daily_activity | record v3–v5, report v5–v6 |
| 6 | manual_measurement | record v2 |
| 8 | full_day_sleep | report/record v5–v6 |
| 9 | abnormal | record v2 |
| 10 | weight_fat | (storage keys in app) |
| 11 | ecg | record v1–v2 |
| 12 | temperature | (storage keys in app) |

Sleep types **2** (daytime) and **3** (night) use dedicated Java parsers; schema index focuses on `full_day_sleep` for staged sleep v2.

## Java parser dispatch (`FitnessDataParser`)

### Sport report (`fileType == 1`)

| sportType | Class |
|----------:|-------|
| 1,2,4,5 | `OutdoorSportReportParser` |
| 3 | `IndoorRunReportParser` |
| 6 | `OutdoorBikingReportParser` |
| 7 | `IndoorBikingReportParser` |
| 8 | `FreeTrainingReportParser` |
| 9,10 | `SwimmingReportParser` |
| 11 | `EllipticalMachineReportParser` |
| 12 | `YogaReportParser` |
| 13 | `RowingMachineReportParser` |
| 14 | `RopeSkippingReportParser` |
| 15 | `HikingReportParser` |
| 16 | `HITrainingReportParser` |
| 17 | `TriathlonReportParser` |
| 18 | `OrdinaryBallReportParser` |
| 19 | `BasketballReportParser` |
| 20 | `GolfReportParser` |
| 21 | `SkiReportParser` |
| **22** | **`OutdoorStepReportParser`** |
| 23 | `OutdoorNoStepReportParser` |
| 24 | `RockClimbingReportParser` |
| 25 | `DivingReportParser` |
| 26 | `GameModeReportRarser` |
| default | `SportReportSchemaBaseParser` |

### Sport record (`fileType == 0`)

Same sportType mapping with `*RecordParser` classes; default `SportRecordSchemaBaseParser`.

### Daily / health (`dataType == 0`)

See `getDailyDataParserInstance` in `docs/MIFITNESS_SYNC.md` (DailyRecord, NightSleep, ManualMeasure, Weight, ECG, Temperature, etc.).

## Practical workflow for miwear-band

1. Log the 7-byte file ID from sync (already in `activity.c`).
2. Run `python3 scripts/mi_schema_lookup.py --hex <14 hex chars>`.
3. If `schema_path` is set → implement or generate parser from that JSON (future: schema-driven C or subprocess to Mi Fitness native lib).
4. If `schema_path` is null → port fields from the listed `*ReportParser` / `*RecordParser` or Gadgetbridge equivalent (`WorkoutSummaryParser`).

### Your outdoor walk (subtype 22, report, v6)

```bash
python3 scripts/mi_schema_lookup.py --sport 22 --detail 1 --version 6
```

→ `OutdoorStepReportParser`, **nativeParse**, no v6 schema entry (use GB/`workout_summary.c` until band emits v8+ or you decode `OutdoorStepReportParser`).

### Record stream (subtype 22, record, v5)

```bash
python3 scripts/mi_schema_lookup.py --sport 22 --detail 0 --version 5
```

→ still no exact v5 in index (record lists **v9** only); legacy `OutdoorStepRecordParser` or nearest schema after verifying band version byte.

## Schema reader + test harness (in-repo)

CMake extracts `schemas.zip` into `build/mi-fitness-schemas/` and defines `SALUS_SCHEMA_DIR`.

| Tool | Purpose |
|------|---------|
| `build/salus-schema-dump` | C decoder → JSON (`--bin`, `--sport`, `--detail`, `--version`, optional `--summary`) |
| `scripts/schema_binary_dump.py` | Same layout in Python (reference + CI); reads JSON directly from `schemas.zip` |
| `scripts/mi_schema_lookup.py` | Resolve 7-byte id → schema path + Java parser class |

**Capture payloads from sync** (no extra walks needed for regression):

```bash
export SALUS_CAPTURE_DIR="$HOME/.local/share/salus/captures"
./build/salus   # sync workouts; writes sport_22_detail1_v6_<ts>.bin + .json
python3 scripts/schema_binary_dump.py --sport 22 --detail 1 --version 6 --bin "$SALUS_CAPTURE_DIR/sport_22_detail1_v6_....bin"
./build/salus-schema-dump --bin ... --sport 22 --detail 1 --version 8   # when schema version matches index
```

The app tries **schema decode first** when `index.json` has an exact wire version, then falls back to Gadgetbridge-style parsers in `workout_summary.c`.

Supported in C today: sequential scalars, `bitField`, nested `object`, fixed/`EOF` arrays. Dynamic `itemCount` (`"$data": "1/recordCount"`) and `$ref` record streams are Python-only until extended.

## Next steps

1. Extend C array `$data` refs for **record** files (`outdoor_foot_record_v9.json`).
2. **Optional native** — wrap `libfitness-schema-parser` from split APKs for bit-identical Mi Fitness output.
3. Always run `mi_schema_lookup.py` before hand-porting a new activity.
