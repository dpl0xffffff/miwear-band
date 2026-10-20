# Mi Fitness `assets/schemas.zip`

Extracted from `com.xiaomi.wearable` **base.apk** (`assets/schemas.zip`).

| File | Role |
|------|------|
| `index.json` | Maps `(activity, fileType, vN)` → relative JSON path inside the zip |
| `schemas.zip` | Full schema bundle shipped with the app |

Use `scripts/mi_schema_lookup.py` to resolve a 7-byte band file ID to a schema path and Java parser class.

See `docs/MIFITNESS_SCHEMA_MAPPING.md` for the full mapping tables and parse pipeline.
