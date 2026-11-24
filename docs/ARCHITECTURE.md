# Architecture

```
┌──────────────┐     D-Bus      ┌─────────────┐
│  src/ui/ui   │ ─────────────► │ spp/bluez   │
└──────┬───────┘                └─────────────┘
       │ connect_worker
       ▼
┌──────────────┐   RFCOMM/SPP   ┌─────────────┐
│ spp/wire     │ ◄────────────► │  Band 10    │
│ spp/crypto   │                └─────────────┘
└──────┬───────┘
       │ auth OK
       ▼
┌──────────────────┐
│ spp/fitness_sync │  ← Mi Fitness FitnessWearSender flow
└────────┬─────────┘
         │ protobuf Command + activity chunks
         ▼
┌──────────────────┐     ┌─────────────┐
│ activity/*       │ ──► │ db/db.c     │
└──────────────────┘     └─────────────┘
```

## Layout

| Directory | Contents |
|-----------|----------|
| `include/salus/` | `app.h`, `util.h`, module headers |
| `src/devices/xiaomi/` | Transport, auth, Mi Fitness sync orchestration |
| `src/devices/xiaomi/` | Activity file parsers |
| `src/ui/` | GTK4 UI |
| `src/db/` | SQLite (Mi Fitness / GB-style schema) |
| `src/proto/` | `xiaomi.pb-c` generated code |
| `src/core/` | Config (INI) |

CLI binary sharing `src/spp` + `activity` + `db` without GTK is planned later (`miwear-band-cli`).

See [MIFITNESS_SYNC.md](MIFITNESS_SYNC.md) for APK-derived sync semantics.
