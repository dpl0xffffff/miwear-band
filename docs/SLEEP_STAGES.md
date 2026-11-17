# Sleep stages (Mi Fitness parity)

In-process pipeline under `src/sleep/`:

| File | Role |
|------|------|
| `mi_sleep_packets.c` | Gadgetbridge-style sleep packet scan (`FB FA FC FF` …) |
| `mi_sleep_pipeline.c` | Orchestration: stages from the band's embedded stage packets |
| `mi_sleep_duration.c` | Normalize stage durations (minutes vs seconds) |
| `mi_sleep_external.c` | Optional `MIWEAR_MI_SLEEP_ALGO_CMD` JSON helper override |

Full-day sleep binary layout (v5/v6) lives in `src/activity/mi_fitness_full_day_sleep.c`.

Nights whose feature blob has no embedded stage packets keep their total sleep duration but get no
stage timeline, unless `MIWEAR_MI_SLEEP_ALGO_CMD` points at an external helper.

An in-process TensorFlow Lite attempt (`mi_stage_model_0_1_1.tflite`) was removed: the model returns
one `[1,1,4]` prediction per call, not one per epoch, and expects Xiaomi's HRV/accelerometer feature
set (computed in `libstage-lib.so`), which was never reproduced. See git history if revisiting.
