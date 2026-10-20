#!/usr/bin/env bash
# Build TensorFlow Lite (static) for miwear-band sleep ML. Requires git, cmake, gcc/g++.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TF_SRC="${MIWEAR_TENSORFLOW_SRC:-/tmp/tflite-build/tensorflow}"
BUILD="${MIWEAR_TFLITE_BUILD_DIR:-/tmp/tflite-cmake}"
if [[ ! -f "$TF_SRC/tensorflow/lite/CMakeLists.txt" ]]; then
  echo "Clone TensorFlow first, e.g.: git clone --depth 1 --branch v2.16.1 https://github.com/tensorflow/tensorflow.git $TF_SRC"
  exit 1
fi
cmake -S "$TF_SRC/tensorflow/lite" -B "$BUILD" -DTFLITE_ENABLE_XNNPACK=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$BUILD" -j"$(nproc)" --target tensorflow-lite
echo "Built: $BUILD/libtensorflow-lite.a"
echo "Configure miwear-band with: cmake -B build -DMIWEAR_ENABLE_SLEEP_ML=ON -DMIWEAR_TFLITE_BUILD_DIR=$BUILD -DTENSORFLOW_SOURCE_DIR=$TF_SRC"
