#!/usr/bin/env python3
"""Decode Mi Fitness wire payloads using assets/schemas.zip JSON (sequential layout).

Mirrors constructSchemaBinaryData + native schemaParse layout:
  buffer = file_id[0:7] + b'\\x00' + payload[8:]   (sport files)

Usage:
  python3 scripts/schema_binary_dump.py --sport 22 --detail 1 --version 8 --bin capture.bin
  python3 scripts/schema_binary_dump.py --schema outdoor_foot/report/outdoor_foot_report_v8.json --bin capture.bin
  python3 scripts/schema_binary_dump.py --sport 22 --detail 1 --version 8 --bin capture.bin --summary
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zipfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
INDEX_PATH = ROOT / "third_party/mi-fitness-schemas/index.json"
ZIP_PATH = ROOT / "third_party/mi-fitness-schemas/schemas.zip"

SPORT_ACTIVITY = {
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

FILE_TYPE = {0: "record", 1: "report", 2: "gps", 3: "hrr"}


class DecodeError(Exception):
    pass


class BinReader:
    def __init__(self, data: bytes):
        self.data = data
        self.off = 0

    def remaining(self) -> int:
        return len(self.data) - self.off

    def need(self, n: int) -> None:
        if self.off + n > len(self.data):
            raise DecodeError(f"need {n} bytes at {self.off}, have {self.remaining()}")

    def read_raw(self, n: int) -> bytes:
        self.need(n)
        chunk = self.data[self.off : self.off + n]
        self.off += n
        return chunk

    def u8(self) -> int:
        return self.read_raw(1)[0]

    def i8(self) -> int:
        v = self.u8()
        return v - 256 if v >= 128 else v

    def u16(self) -> int:
        return struct.unpack_from("<H", self.read_raw(2))[0]

    def u32(self) -> int:
        return struct.unpack_from("<I", self.read_raw(4))[0]

    def i32(self) -> int:
        return struct.unpack_from("<i", self.read_raw(4))[0]

    def f32(self) -> float:
        return struct.unpack_from("<f", self.read_raw(4))[0]


def construct_schema_binary_sport(payload: bytes) -> bytes:
    if len(payload) < 8:
        raise DecodeError("sport payload too short")
    return payload[:7] + b"\x00" + payload[8:]


def construct_schema_binary_health(payload: bytes) -> bytes:
    if len(payload) < 7:
        raise DecodeError("health payload too short")
    return payload[:6] + b"\x00" + payload[7:]


def read_bitfield(reader: BinReader, spec: dict) -> dict[str, int]:
    size = int(spec.get("byteSize", 1))
    raw = int.from_bytes(reader.read_raw(size), "little")
    out: dict[str, int] = {}
    props = spec.get("properties") or {}
    order = spec.get("fieldOrder") or list(props.keys())
    for name in order:
        field = props[name]
        start = int(field["startBit"])
        end = int(field["endBit"])
        width = end - start + 1
        mask = (1 << width) - 1
        out[name] = (raw >> start) & mask
    return out


def resolve_ref(root: dict, node: dict) -> dict:
    ref = node.get("$ref")
    if not ref:
        return node
    if not ref.startswith("#/$defs/"):
        raise DecodeError(f"unsupported ref {ref}")
    name = ref.split("/")[-1]
    defs = root.get("$defs") or {}
    if name not in defs:
        raise DecodeError(f"missing def {name}")
    return defs[name]


def decode_scalar(reader: BinReader, typ: str) -> Any:
    if typ == "uint8":
        return reader.u8()
    if typ == "int8":
        return reader.i8()
    if typ == "uint16":
        return reader.u16()
    if typ == "int16":
        v = reader.u16()
        return v - 65536 if v >= 32768 else v
    if typ == "uint32":
        return reader.u32()
    if typ == "int32":
        return reader.i32()
    if typ in ("float", "float32"):
        v = reader.f32()
        if math.isnan(v) or math.isinf(v):
            return None
        return v
    raise DecodeError(f"unsupported scalar {typ}")


def decode_field(reader: BinReader, root: dict, spec: dict, ctx: dict) -> Any:
    spec = resolve_ref(root, spec)
    typ = spec.get("type")
    if typ == "bitField":
        return read_bitfield(reader, spec)
    if typ == "object":
        return decode_object(reader, root, spec, ctx)
    if typ == "array":
        return decode_array(reader, root, spec, ctx)
    if typ in ("uint8", "int8", "uint16", "int16", "uint32", "int32", "float", "float32"):
        bs = spec.get("byteSize")
        if bs is not None and int(bs) == 0:
            return None
        return decode_scalar(reader, typ)
    raise DecodeError(f"unsupported field type {typ}")


def array_count(spec: dict, ctx: dict, reader: BinReader) -> int | None:
    ic = spec.get("itemCount")
    if ic is None:
        return 1
    if ic == "EOF":
        return None
    if isinstance(ic, int):
        return ic
    if isinstance(ic, dict) and "$data" in ic:
        expr = ic["$data"]
        if expr.startswith("1/"):
            key = expr[2:]
            if key not in ctx:
                raise DecodeError(f"array count context missing {key}")
            return int(ctx[key])
    raise DecodeError(f"unsupported itemCount {ic!r}")


def decode_array(reader: BinReader, root: dict, spec: dict, ctx: dict) -> list:
    count = array_count(spec, ctx, reader)
    item_spec = spec.get("items") or {}
    item_spec = resolve_ref(root, item_spec)
    out: list = []
    if count is None:
        while reader.remaining() > 0:
            out.append(decode_field(reader, root, item_spec, ctx))
        return out
    for _ in range(count):
        if reader.remaining() == 0:
            break
        out.append(decode_field(reader, root, item_spec, ctx))
    return out


def decode_object(reader: BinReader, root: dict, spec: dict, ctx: dict) -> dict:
    props = spec.get("properties") or {}
    order = spec.get("fieldOrder") or list(props.keys())
    out: dict = {}
    local_ctx = dict(ctx)
    for name in order:
        if name not in props:
            continue
        field = props[name]
        try:
            val = decode_field(reader, root, field, local_ctx)
        except DecodeError:
            raise
        out[name] = val
        local_ctx[name] = val
    return out


def decode_schema(root: dict, wire: bytes, *, sport: bool = True) -> dict:
    buf = construct_schema_binary_sport(wire) if sport else construct_schema_binary_health(wire)
    reader = BinReader(buf)
    structure = root.get("structure")
    if not structure:
        raise DecodeError("schema missing structure")
    return decode_field(reader, root, structure, {})


def schema_path_for_sport(sport: int, detail: int, version: int, index: dict) -> str | None:
    activity = SPORT_ACTIVITY.get(sport)
    if not activity:
        return None
    ft = FILE_TYPE.get(detail)
    if not ft:
        return None
    node = index.get(activity, {}).get(ft, {})
    return node.get(f"v{version}")


def load_schema(rel_path: str) -> dict:
    with zipfile.ZipFile(ZIP_PATH) as zf:
        return json.loads(zf.read(rel_path))


def workout_summary_view(decoded: dict) -> dict:
    body = decoded.get("body") or {}
    if not isinstance(body, dict):
        return {"error": "no body object (array record?)"}

    def g(*names):
        for n in names:
            if n in body and body[n] is not None:
                return body[n]
        return None

    return {
        "start_sec": g("startTime"),
        "end_sec": g("endTime"),
        "duration_sec": g("totalDuration", "effectiveDuration"),
        "distance_m": g("totalDistance"),
        "calories": g("totalCalories", "activeCalories"),
        "hr_avg": g("avgHeartRate"),
        "hr_max": g("maxHeartRate"),
        "hr_min": g("minHeartRate"),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", required=True, type=Path, help="raw wire payload (.bin)")
    ap.add_argument("--schema", help="path inside schemas.zip")
    ap.add_argument("--sport", type=int)
    ap.add_argument("--detail", type=int, choices=(0, 1, 2, 3))
    ap.add_argument("--version", type=int)
    ap.add_argument("--summary", action="store_true", help="print workout summary field mapping")
    ap.add_argument("--health", action="store_true", help="health/daily wire (6-byte id prefix)")
    args = ap.parse_args()

    payload = args.bin.read_bytes()
    index = json.loads(INDEX_PATH.read_text())

    rel = args.schema
    if not rel:
        if args.sport is None or args.detail is None or args.version is None:
            ap.error("provide --schema or (--sport --detail --version)")
        rel = schema_path_for_sport(args.sport, args.detail, args.version, index)
        if not rel:
            print("No schema index entry for this id (legacy nativeParse only).", file=sys.stderr)
            sys.exit(2)

    schema = load_schema(rel)
    sport = not args.health
    decoded = decode_schema(schema, payload, sport=sport)
    out = {"schema": rel, "bytes_consumed": len(payload), "decoded": decoded}
    if args.summary:
        out["workout_summary"] = workout_summary_view(decoded)
    print(json.dumps(out, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
