#!/usr/bin/env python3
"""Regenerate outdoor_step_report_fields.c from JADX-exported OutdoorStepReportParser <init>."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INIT = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/outdoor_step_init.java")


def main() -> None:
    text = INIT.read_text()
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

    arr = re.search(r"new FitnessDataBaseParser\.OneDimenDataType\[\]\{(.*)\};", text, re.S)
    if not arr:
        raise SystemExit("reportDataTypeArray not found")
    body = arr.group(1)
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
            raise SystemExit(f"unexpected token at {i}: {body[i:i+40]!r}")

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

    def resolve_item(expr: str) -> dict:
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
        depend = "getCourseIdExistType" in inner or "getLactateTypeExistType" in inner or len(parts) <= 4
        is_float = is_unsigned = False
        cnt = None
        if len(parts) >= 4 and parts[3] in ("true", "false"):
            is_float = parts[3] == "true"
            is_unsigned = len(parts) > 5 and parts[5] == "true"
            if len(parts) > 6 and "valueOf" in parts[6]:
                cnt = int(re.search(r"(\d+)", parts[6]).group(1))
        return {
            "type": int(typ),
            "byte_count": int(bc),
            "support_version": int(sv),
            "is_float": is_float,
            "is_unsigned": is_unsigned,
            "counts_type": cnt,
            "has_depend": 1 if depend or int(typ) < 0 else 0,
        }

    rows = [resolve_item(it) for it in items]
    c_path = ROOT / "src/activity/outdoor_step_report_fields.c"
    lines = [
        "/* Generated from com.xiaomi.fit.fitness.parser.sport.report.OutdoorStepReportParser */",
        '#include "miwear/activity/mi_fitness_oneden.h"',
        "const MiFitnessOneDimenField mi_fitness_outdoor_step_report_fields[] = {",
    ]
    for r in rows:
        ct = -1 if r["counts_type"] is None else r["counts_type"]
        lines.append(
            f"  {{ {r['type']}, {r['byte_count']}, {r['support_version']}, "
            f"{1 if r['is_float'] else 0}, {1 if r['is_unsigned'] else 0}, {ct}, {r['has_depend']}, "
            f"{1 if 'getCourseIdExistType' in inner else (2 if 'getLactateTypeExistType' in inner else 0)} }},"
        )
    lines.append("};")
    lines.append(f"const size_t mi_fitness_outdoor_step_report_field_count = {len(rows)};")
    c_path.write_text("\n".join(lines) + "\n")
    json_path = ROOT / "third_party/mi-fitness-tables/outdoor_step_report.json"
    json_path.parent.mkdir(exist_ok=True)
    json_path.write_text(json.dumps(rows, indent=2) + "\n")
    print(f"wrote {len(rows)} fields -> {c_path}")


if __name__ == "__main__":
    main()
