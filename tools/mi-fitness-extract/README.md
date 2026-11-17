# Mi Fitness table extraction

Workout summaries and health records are decoded with the field tables Mi Fitness itself uses.
This directory regenerates them from the app instead of hand-porting:

```bash
tools/mi-fitness-extract/extract.sh path/to/base.apk path/to/jadx-all.jar [workdir]
```

1. Unpacks the DEX holding `com.xiaomi.fit.fitness.parser` and decompiles only the report parsers,
   the health/GPS parsers and `FitnessDataValidity` with jadx (`JadxDump.java`).
2. `prepare_sources.py` keeps each parser's constructor (its field table) and compiles it against
   the stubs in `stubs/`, which reproduce `FitnessDataBaseParser.OneDimenDataType` including the
   Kotlin default-argument mask (unset `isUnsigned` means **unsigned**).
3. `DumpTables.java` instantiates every parser, runs `FitnessDataValidity` for every sport, file type
   and version, and writes `third_party/mi-fitness-tables/sport_reports.json`.
4. `scripts/gen_mi_fitness_reports.py` turns that JSON (plus `sport_types.json` and
   `schema_field_ids.json`) into `src/activity/mi_fitness_report_tables.c`.

Running the code rather than parsing it keeps defaults, version gates and dependencies exactly as
the app builds them. `test/report_test.c` checks the result against the Mi Fitness schemas.
