#!/usr/bin/env python3
"""Resolve Mi Fitness binary file IDs to schema JSON paths and Java parser classes.

Mirrors:
  - FitnessDataId(byte[]) — com.xiaomi.fit.data.common.data.mi.FitnessDataId
  - SchemaFilesUtilsKt.getSchemaInfo
  - SchemaIndex.getSchemaPath(activity, fileType, version) via index.json
  - FitnessDataParser.getSportReportParserInstance / getSportRecordParserInstance
  - FitnessDataParser.getDailyDataParserInstance

Usage:
  python3 scripts/mi_schema_lookup.py --hex d9...   # 7-byte file id (14 hex chars)
  python3 scripts/mi_schema_lookup.py --sport 22 --detail 1 --version 6
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INDEX_PATH = ROOT / "third_party/mi-fitness-schemas/index.json"
ZIP_PATH = ROOT / "third_party/mi-fitness-schemas/schemas.zip"

FILE_TYPE_SPORT = {0: "record", 1: "report", 2: "gps", 3: "hrr"}
FILE_TYPE_HEALTH = {0: "record", 1: "report"}

SPORT_REPORT_PARSER = {
    (1, 2, 4, 5): "OutdoorSportReportParser",
    3: "IndoorRunReportParser",
    6: "OutdoorBikingReportParser",
    7: "IndoorBikingReportParser",
    8: "FreeTrainingReportParser",
    (9, 10): "SwimmingReportParser",
    11: "EllipticalMachineReportParser",
    12: "YogaReportParser",
    13: "RowingMachineReportParser",
    14: "RopeSkippingReportParser",
    15: "HikingReportParser",
    16: "HITrainingReportParser",
    17: "TriathlonReportParser",
    18: "OrdinaryBallReportParser",
    19: "BasketballReportParser",
    20: "GolfReportParser",
    21: "SkiReportParser",
    22: "OutdoorStepReportParser",
    23: "OutdoorNoStepReportParser",
    24: "RockClimbingReportParser",
    25: "DivingReportParser",
    26: "GameModeReportRarser",
}

SPORT_RECORD_PARSER = {
    (1, 2, 4, 5, 15): "OutdoorSportRecordParser",
    3: "IndoorRunRecordParser",
    6: "OutdoorBikingRecordParser",
    7: "IndoorBikingRecordParser",
    8: "FreeTrainingRecordParser",
    (9, 10): "SwimmingRecordParser",
    11: "EllipticalMachineRecordParser",
    12: "YogaRecordParser",
    13: "RowingMachineRecordParser",
    14: "RopeSkippingRecordParser",
    16: "HITrainingRecordParser",
    17: "TriathlonRecordParser",
    18: "OrdinaryBallRecordParser",
    19: "BasketballRecordParser",
    20: "GolfRecordParser",
    21: "SkiRecordParser",
    22: "OutdoorStepRecordParser",
    23: "OutdoorNoStepRecordParser",
    24: "RockClimbingRecordParser",
    25: "DivingRecordParser",
    26: "GameModeRecordRarser",
}

DAILY_PARSER = {
    (0, 0): "DailyRecordParser",
    (0, 1): "DailyReportParser",
    (2, None): "DaytimeSleepParser",
    (3, None): "NightSleepParser",
    (5, None): "UserProfileParser",
    (6, None): "ManualMeasureParser",
    (8, None): "AllDaySleepParser",
    (9, None): "AbnormalRecordParser",
    (10, None): "WeightRecordParser",
    (11, None): "EcgMeasureParser",
    (12, None): "TemperatureMeasureParser",
}


def _lookup_switch(table: dict, key: int, default: str) -> str:
    for k, v in table.items():
        if isinstance(k, tuple) and key in k:
            return v
        if k == key:
            return v
    return default


def parse_data_id(raw: bytes) -> dict:
    if len(raw) < 7:
        raise ValueError("file id must be at least 7 bytes")
    ts, tz, ver, b2 = struct.unpack("<IBBB", raw[:7])
    data_type = (b2 & 0x80) >> 7
    subtype = (b2 & 0x7F) >> 2
    file_type = b2 & 3
    out = {
        "timestamp": ts,
        "tz_in_15min": tz,
        "version": ver,
        "data_type": data_type,
        "data_type_name": "sport" if data_type == 1 else "health",
        "file_type": file_type,
        "file_type_name": FILE_TYPE_SPORT.get(file_type, str(file_type))
        if data_type == 1
        else FILE_TYPE_HEALTH.get(file_type, str(file_type)),
    }
    if data_type == 1:
        out["sport_type"] = subtype
    else:
        out["daily_type"] = subtype
    return out


def get_schema_activity_name(data: dict) -> str:
    """SchemaFilesUtilsKt.getSchemaInfo — activity folder name (first triple component)."""
    if data["data_type"] == 1:
        sport_map = {
            3: "indoor_running",
            7: "indoor_cycling",
            8: "free_training",
            9: "swimming",
            10: "swimming",
            11: "elliptical_training",
            12: "yoga",
            13: "rowing_workout",
            14: "rope_skipping",
            16: "hiit",
            17: "triathlon",
            18: "ball_games",
            19: "basketball",
            20: "golf",
            21: "skiing",
            22: "outdoor_foot",
            23: "outdoor_non_foot",
            24: "climbing",
            25: "diving",
            26: "esports",
        }
        return sport_map.get(data["sport_type"], "")
    daily = data["daily_type"]
    if daily == 0:
        return "daily_activity"
    if daily == 6:
        return "manual_measurement"
    daily_map = {
        8: "full_day_sleep",
        9: "abnormal",
        10: "weight_fat",
        11: "ecg",
        12: "temperature",
    }
    return daily_map.get(daily, "")


def find_schema_path(index: dict, activity: str, file_type: str, version: int) -> str | None:
    ver_key = f"v{version}"
    node = index.get(activity)
    if not isinstance(node, dict):
        return None
    ft_node = node.get(file_type)
    if not isinstance(ft_node, dict):
        return None
    return ft_node.get(ver_key)


def java_parser(data: dict) -> str:
    if data["data_type"] == 1:
        ft = data["file_type"]
        st = data["sport_type"]
        if ft == 1:
            return _lookup_switch(SPORT_REPORT_PARSER, st, "SportReportSchemaBaseParser")
        if ft == 0:
            return _lookup_switch(SPORT_RECORD_PARSER, st, "SportRecordSchemaBaseParser")
        return "(recordFDSData — GPS/HRR blob, no summary parser)"
    dt = data["daily_type"]
    ft = data["file_type"]
    if dt == 0:
        return DAILY_PARSER.get((0, ft), "DailyReportParser")
    return DAILY_PARSER.get((dt, None), "DailyReportParser")


def resolve(data: dict, index: dict) -> dict:
    activity = get_schema_activity_name(data)
    ft_name = data["file_type_name"]
    ver = data["version"]
    schema_rel = find_schema_path(index, activity, ft_name, ver) if activity else None
    parse_mode = "schemaParse (JSON + native schema engine)" if schema_rel else "nativeParse (legacy Java/C++ layout)"
    return {
        **data,
        "schema_activity": activity or None,
        "schema_path": schema_rel,
        "java_parser_class": java_parser(data),
        "mi_fitness_parse_mode": parse_mode,
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hex", help="7-byte file id as hex (14 chars, optional spaces)")
    ap.add_argument("--sport", type=int, help="sport_type / wire subtype (dataType=1)")
    ap.add_argument("--daily", type=int, help="daily_type (dataType=0)")
    ap.add_argument("--detail", type=int, choices=(0, 1, 2, 3), help="file_type / detail")
    ap.add_argument("--version", type=int, default=0, help="schema wire version byte")
    ap.add_argument("--list-sports", action="store_true", help="print sport subtype → schema folder")
    args = ap.parse_args()

    index = json.loads(INDEX_PATH.read_text())

    if args.list_sports:
        types = index["$types"]["activityType"]["sport"]
        for num in sorted(int(k) for k in types):
            name = types[str(num)]
            brief = index.get(name, {})
            print(f"{num:3d}  {name:22s}  {brief}")
        return

    if args.hex:
        raw = bytes.fromhex(args.hex.replace(" ", ""))
        data = parse_data_id(raw)
    elif args.detail is not None and (args.sport is not None or args.daily is not None):
        data = {
            "version": args.version,
            "file_type": args.detail,
            "file_type_name": FILE_TYPE_SPORT.get(args.detail, str(args.detail)),
        }
        if args.sport is not None:
            data.update(
                data_type=1,
                data_type_name="sport",
                sport_type=args.sport,
            )
        else:
            data.update(
                data_type=0,
                data_type_name="health",
                daily_type=args.daily,
                file_type_name=FILE_TYPE_HEALTH.get(args.detail, str(args.detail)),
            )
    else:
        ap.print_help()
        sys.exit(1)

    info = resolve(data, index)
    print(json.dumps(info, indent=2))
    if info.get("schema_path"):
        print(f"\nSchema JSON in zip: {info['schema_path']}")
    else:
        print(
            "\nNo exact schema for this version — Mi Fitness uses legacy parser:",
            info["java_parser_class"],
        )


if __name__ == "__main__":
    main()
