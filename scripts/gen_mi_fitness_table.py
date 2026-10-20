#!/usr/bin/env python3
"""Generate MiFitnessOneDimenField tables from JADX-exported parser <init> bodies."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

ARRAY_NAMES = (
    "reportDataTypeArray",
    "recordDataTypeArray",
    "dataTypeArray",
    "initDataTypeArray",
)


def parse_inner(inner: str) -> list[str]:
    parts: list[str] = []
    cur = ""
    depth = 0
    for ch in inner:
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def depend_profile_from_inner(inner: str) -> int:
    if "getCourseIdExistType" in inner:
        return 1
    if "getLactateTypeExistType" in inner:
        return 2
    return 0


def collect_assignments(text: str) -> dict[str, str]:
    assign: dict[str, str] = {}
    for line in text.split("\n"):
        m = re.match(
            r"\s*FitnessDataBaseParser\.OneDimenDataType (oneDimenDataType\d*) = "
            r"new FitnessDataBaseParser\.OneDimenDataType\((.*)\);",
            line,
        )
        if m:
            assign[m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s*int (i\d*) = (\d+);", line)
        if m:
            assign[m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s*int (i) = (\d+);", line)
        if m:
            assign[m.group(1)] = m.group(2)
            continue
        m = re.match(r"\s*char (c\d*) = .*;", line)
        if m:
            assign[m.group(1)] = "1"
    return assign


def find_array_body(text: str, array_name: str) -> str:
    marker = (
        f"this.{array_name} = new FitnessDataBaseParser.OneDimenDataType[]{{"
    )
    idx = text.find(marker)
    if idx < 0:
        raise SystemExit(f"{array_name} assignment not found in init body")
    i = idx + len(marker) - 1
    depth = 0
    j = i
    while j < len(text):
        ch = text[j]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1 : j]
        j += 1
    raise SystemExit(f"unterminated {array_name} initializer")


def split_array_items(body: str) -> list[str]:
    items: list[str] = []
    i = 0
    while i < len(body):
        while i < len(body) and body[i] in " \n\r\t,":
            i += 1
        if i >= len(body):
            break
        if body.startswith("oneDimenDataType", i):
            j = i
            while j < len(body) and body[j] not in ",}":
                j += 1
            items.append(body[i:j].strip())
            i = j
        elif body.startswith("new FitnessDataBaseParser.OneDimenDataType(", i):
            j = i + len("new FitnessDataBaseParser.OneDimenDataType(")
            depth = 1
            while j < len(body) and depth:
                if body[j] == "(":
                    depth += 1
                elif body[j] == ")":
                    depth -= 1
                j += 1
            items.append(body[i:j].strip())
            i = j
        else:
            raise SystemExit(f"unexpected token at {i}: {body[i : i + 40]!r}")
    return items


def resolve_item(expr: str, assign: dict[str, str]) -> dict:
    if expr.startswith("new FitnessDataBaseParser.OneDimenDataType("):
        inner = expr[len("new FitnessDataBaseParser.OneDimenDataType(") : -1]
    elif expr in assign:
        inner = assign[expr]
    else:
        raise ValueError(expr)
    inner = re.sub(r"SportReportBaseParser\.ReportType\.(\w+)", "205", inner)
    parts = parse_inner(inner)
    typ, bc, sv = parts[0], parts[1], parts[2]
    if typ in assign:
        typ = assign[typ]
    if bc in assign:
        bc = assign[bc]
    if sv in assign:
        sv = assign[sv]
    is_float = is_unsigned = False
    cnt = None
    if len(parts) >= 4 and parts[3] in ("true", "false"):
        is_float = parts[3] == "true"
        is_unsigned = len(parts) > 5 and parts[5] == "true"
        if len(parts) > 6 and parts[6] != "null" and "valueOf" in parts[6]:
            cnt = int(re.search(r"(\d+)", parts[6]).group(1))
    typ_i = int(typ)
    has_depend = (
        "getCourseIdExistType" in inner
        or "getLactateTypeExistType" in inner
        or len(parts) <= 4
        or typ_i < 0
    )
    return {
        "type": typ_i,
        "byte_count": int(bc),
        "support_version": int(sv),
        "is_float": is_float,
        "is_unsigned": is_unsigned,
        "counts_type": cnt,
        "has_depend": 1 if has_depend else 0,
        "depend_profile": depend_profile_from_inner(inner),
    }


def field_count_symbol(fields_symbol: str) -> str:
    if fields_symbol.endswith("_fields"):
        return fields_symbol[: -len("_fields")] + "_field_count"
    return fields_symbol + "_count"


def write_c(rows: list[dict], fq_class: str, symbol: str, out_path: Path) -> None:
    count_sym = field_count_symbol(symbol)
    lines = [
        f"/* Generated from {fq_class} */",
        '#include "miwear/activity/mi_fitness_oneden.h"',
        f"const MiFitnessOneDimenField {symbol}[] = {{",
    ]
    for r in rows:
        ct = -1 if r["counts_type"] is None else r["counts_type"]
        lines.append(
            f"  {{ {r['type']}, {r['byte_count']}, {r['support_version']}, "
            f"{1 if r['is_float'] else 0}, {1 if r['is_unsigned'] else 0}, {ct}, "
            f"{r['has_depend']}, {r['depend_profile']} }},"
        )
    lines.append("};")
    lines.append(f"const size_t {count_sym} = {len(rows)};")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n")


def generate(
    init_path: Path,
    array_name: str,
    fq_class: str,
    symbol: str,
    out_c: Path,
    json_out: Path | None = None,
) -> int:
    text = init_path.read_text()
    assign = collect_assignments(text)
    body = find_array_body(text, array_name)
    items = split_array_items(body)
    rows = [resolve_item(it, assign) for it in items]
    write_c(rows, fq_class, symbol, out_c)
    if json_out is None:
        json_out = ROOT / "third_party/mi-fitness-tables" / (out_c.stem + ".json")
    json_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(json.dumps(rows, indent=2) + "\n")
    print(f"wrote {len(rows)} fields -> {out_c}")
    return len(rows)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--init", type=Path, required=True, help="Path to .init.java export")
    p.add_argument(
        "--array",
        required=True,
        choices=ARRAY_NAMES,
        help="OneDimenDataType[] field assigned in <init>",
    )
    p.add_argument("--class", dest="fq_class", required=True, help="Fully qualified parser class")
    p.add_argument("--symbol", required=True, help="C array symbol (without _count suffix)")
    p.add_argument("--out", type=Path, required=True, help="Output .c path relative to repo root or absolute")
    p.add_argument("--json", type=Path, default=None, help="Optional JSON dump path")
    args = p.parse_args()

    init_path = args.init if args.init.is_absolute() else ROOT / args.init
    out_c = args.out if args.out.is_absolute() else ROOT / args.out
    json_out = None
    if args.json:
        json_out = args.json if args.json.is_absolute() else ROOT / args.json

    count = generate(init_path, args.array, args.fq_class, args.symbol, out_c, json_out)
    if count == 0:
        sys.exit("empty field table")


if __name__ == "__main__":
    main()
