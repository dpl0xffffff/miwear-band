#!/bin/bash
# Regenerates third_party/mi-fitness-tables/sport_reports.json from a Mi Fitness APK.
#
# usage: tools/mi-fitness-extract/extract.sh <base.apk> <jadx-all.jar> [workdir]
#
# Decompiling the whole APK needs >16 GB, so only the DEX holding the fitness parsers is loaded.
# The report/record parser constructors and FitnessDataValidity are then compiled against small stubs and
# executed, so the tables come out exactly as Mi Fitness builds them (defaults, versions, signedness).
set -euo pipefail
APK=$1
JAR=$2
WORK=${3:-$(mktemp -d)}
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
mkdir -p "$WORK/dex" "$WORK/jadx"
unzip -qo "$APK" 'classes*.dex' -d "$WORK/dex"
DEX=$(grep -la 'Lcom/xiaomi/fit/fitness/parser/sport/report/SwimmingReportParser;' "$WORK"/dex/classes*.dex | head -1)
java -Xmx6g -cp "$JAR" "$HERE/JadxDump.java" "$DEX" "$WORK/jadx" \
  '^com\.xiaomi\.fit\.(fitness\.parser\.(sport\.(base\.(SportReportBaseParser|SportRecordBaseParser)|report\.|record\.|gps\.SportGpsParser)|daily\.(WeightRecord|AbnormalRecord|EcgMeasure|TemperatureMeasure)Parser)|data\.common\.data\.mi\.FitnessDataValidity)'
python3 "$HERE/prepare_sources.py" "$WORK/jadx" "$HERE/stubs" "$WORK/src"
mkdir -p "$WORK/classes"
javac -nowarn -d "$WORK/classes" $(find "$WORK/src" -name '*.java') "$HERE/DumpTables.java"
java -cp "$WORK/classes" DumpTables > "$REPO/third_party/mi-fitness-tables/sport_reports.json"
python3 "$REPO/scripts/gen_mi_fitness_reports.py"
echo "updated sport_reports.json and src/devices/xiaomi/mi_fitness_report_tables.c"
