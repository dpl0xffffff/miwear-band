# Binary capture fixtures

Drop band payloads here for regression decoding.

Generate captures during sync:

```bash
export SALUS_CAPTURE_DIR="$(pwd)/test/fixtures"
/path/to/build/salus
```

Each capture: `sport_<type>_detail<D>_v<V>_<unix_ts>.bin` plus a small `.json` sidecar.

Decode:

```bash
python3 scripts/schema_binary_dump.py --bin test/fixtures/sport_22_detail1_v6_*.bin --sport 22 --detail 1 --version 6 --summary
```

Use the wire **version byte** from the sidecar when choosing `--version`. If lookup returns no schema path, the band firmware uses legacy parsers (Gadgetbridge layouts), not JSON schemas.
