# Sleep stages (Mi Fitness parity)

In-process pipeline under `src/sleep/`:

| File | Role |
|------|------|
| `mi_sleep_packets.c` | Gadgetbridge-style sleep packet scan (`FB FA FC FF` …) |
| `mi_sleep_pipeline.c` | Orchestration: embedded stages or HR epochs + TFLite |
| `mi_sleep_inference.cpp` | `mi_stage_model_0_1_1.tflite` via TensorFlow Lite C API |
| `mi_sleep_duration.c` | Normalize stage durations (minutes vs seconds) |
| `mi_sleep_external.c` | Optional `MIWEAR_MI_SLEEP_ALGO_CMD` JSON helper override |

Full-day sleep binary layout (v5/v6) lives in `src/activity/mi_fitness_full_day_sleep.c`.

Build with ML:

```bash
./scripts/build-sleep-tflite.sh
cmake -B build -DMIWEAR_ENABLE_SLEEP_ML=ON \
  -DTENSORFLOW_SOURCE_DIR=/tmp/tflite-build/tensorflow \
  -DMIWEAR_TFLITE_BUILD_DIR=/tmp/tflite-cmake
cmake --build build
```

Model path: `third_party/mi-fitness-sleep/mi_stage_model_0_1_1.tflite` (override with `MIWEAR_SLEEP_MODEL`).
