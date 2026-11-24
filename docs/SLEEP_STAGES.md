# Sleep stages (Mi Fitness parity)

In-process pipeline under `src/devices/xiaomi/`:

| File | Role |
|------|------|
| `mi_sleep_packets.c` | Gadgetbridge-style sleep packet scan (`FB FA FC FF` …) |
| `mi_sleep_pipeline.c` | Orchestration: stages from the band's embedded stage packets |
| `mi_sleep_duration.c` | Normalize stage durations (minutes vs seconds) |
| `mi_sleep_external.c` | Optional `SALUS_MI_SLEEP_ALGO_CMD` JSON helper override |

Full-day sleep binary layout (v5/v6) lives in `src/devices/xiaomi/mi_fitness_full_day_sleep.c`
(`salus_full_day_sleep_layout`), following Mi Fitness's `full_day_sleep_report_v5/v6` schemas:

| | v5 | v6 |
|---|---|---|
| Validity bitmap | 2 bytes, MSB first | 3 bytes, MSB first |
| Bits 0–7 | start, end, quality, efficiency, asleep duration, in-bed duration, go-to-bed, leave-bed | same |
| HRV summary (avg, SD, median, percentiles, timestamp, max, min, baseline) | — | bits 8–18 |
| HR / SpO₂ series | bits 8 / 9 | bits 19 / 20 |
| HRV series (u16 points) | — | bit 21 |
| Snore series (u32 points) | bit 10 | bit 22 |

Each series is `interval u16, count u16, firstTime u32 (only when count > 0), values`. The
`featureData` tail after the last series holds the `FF FC FA FB` stage packets (type 17 timeline,
type 16 totals). Reading a v6 file with the v5 layout misses the tail entirely, which is how nights
from 2026-11-19 on lost their stages.

How a night is shown:

- Stage markers (type 17) → the hypnogram and the breakdown are counted from them.
- Only stage totals (type 16) → the breakdown uses the totals; the hypnogram shows the night as
  "asleep, stage unknown" (never a timeline made up from the totals).
- Neither → total sleep only, drawn as "stage unknown", with a note on the Sleep page.

Nights whose feature blob has no embedded stage packets keep their total sleep duration but get no
stage timeline, unless `SALUS_MI_SLEEP_ALGO_CMD` points at an external helper.

An in-process TensorFlow Lite attempt (`mi_stage_model_0_1_1.tflite`) was removed: the model returns
one `[1,1,4]` prediction per call, not one per epoch, and expects Xiaomi's HRV/accelerometer feature
set (computed in `libstage-lib.so`), which was never reproduced. See git history if revisiting.
