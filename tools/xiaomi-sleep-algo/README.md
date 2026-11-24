# Legacy external sleep helper (removed)

Sleep stages are handled **in-process** by `src/devices/xiaomi/` (see [docs/SLEEP_STAGES.md](../../docs/SLEEP_STAGES.md)).

To plug in a custom JSON helper, set:

```bash
export SALUS_MI_SLEEP_ALGO_CMD='/path/to/helper %s'
```

The helper reads `$XDG_RUNTIME_DIR/miwear-sleep-src.bin` and prints Mi Fitness–style JSON on stdout.
