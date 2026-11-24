# Mi Fitness table extraction

Workout summaries, workout time series and health records are decoded with the field tables Mi
Fitness itself uses.
This directory regenerates them from the app instead of hand-porting:

```bash
tools/mi-fitness-extract/extract.sh path/to/base.apk path/to/jadx-all.jar [workdir]
```

1. Unpacks the DEX holding `com.xiaomi.fit.fitness.parser` and decompiles only the report and
   record parsers, the health/GPS parsers and `FitnessDataValidity` with jadx (`JadxDump.java`).
2. `prepare_sources.py` keeps each parser's constructor (its field table) and compiles it against
   the stubs in `stubs/`, which reproduce `FitnessDataBaseParser.OneDimenDataType` including the
   Kotlin default-argument mask (unset `isUnsigned` means **unsigned**).
   Record parsers keep their constructor plus `parseRecord` (which layout each version uses),
   `getInitDataTypeArray`, `getITSummaryDataType`, `isSwimmingSport` and `getDistanceRecordTimes`;
   jadx's reused null registers are inlined so the constructors compile.
3. `DumpTables.java` instantiates every parser, calls each record parser's `parseRecord` for every
   version to see which `FourDimenDataInfo` / `OneDimenDataType` array it picks, runs `FitnessDataValidity` for every sport, file type
   and version, and writes `third_party/mi-fitness-tables/sport_reports.json`.
4. `scripts/gen_mi_fitness_reports.py` turns that JSON (plus `sport_types.json` and
   `schema_field_ids.json`) into `src/devices/xiaomi/mi_fitness_report_tables.c`.

Running the code rather than parsing it keeps defaults, version gates and dependencies exactly as
the app builds them. `test/report_test.c` checks reports against the Mi Fitness schemas.

The record decoder (`src/devices/xiaomi/mi_fitness_sport_record.c`) was also checked against Mi Fitness's
own `FourDimenDataParser` / `OneDimenDataParser` running on the same bytes: about 3,700 random files
across every sport and version, 5.5 million records, decoded identically. That harness is not kept
here. It needs jadx's broken control flow in `FourDimenDataParser.parseDataValid` fixed by hand.
Note also that a Java recompile of `OneDimenDataParser.parseDataValue` turns 4-byte values into
floats, because `cond ? Float : Long` unboxes; the Kotlin original keeps them as `Long`.
