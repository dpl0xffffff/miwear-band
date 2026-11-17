#!/usr/bin/env python3
"""Turn jadx output into a compilable set: stubbed base classes + report parser constructors.

usage: prepare_sources.py <jadx_out_dir> <stubs_dir> <work_src_dir>
"""
import glob
import os
import re
import shutil
import sys

JADX, STUBS, WORK = sys.argv[1:4]
if os.path.exists(WORK):
    shutil.rmtree(WORK)
shutil.copytree(STUBS, WORK)

# SportReportBaseParser: only the ReportType constants and the two dependency descriptors.
base = open(f"{JADX}/com.xiaomi.fit.fitness.parser.sport.base.SportReportBaseParser.java").read()
report_type = re.search(r"(    public static final class ReportType \{.*?\n    \})\n", base, re.S).group(1)
report_type = report_type.replace("@NotNull", "").replace("@Nullable", "")
os.makedirs(f"{WORK}/com/xiaomi/fit/fitness/parser/sport/base", exist_ok=True)
with open(f"{WORK}/com/xiaomi/fit/fitness/parser/sport/base/SportReportBaseParser.java", "w") as f:
    f.write("""package com.xiaomi.fit.fitness.parser.sport.base;
import com.xiaomi.fit.fitness.parser.base.FitnessDataBaseParser;
public abstract class SportReportBaseParser extends FitnessDataBaseParser {
    private final FitnessDataBaseParser.DependDataType courseIdExistType = new FitnessDataBaseParser.DependDataType(90, new Integer[]{251, 252, 253, 255});
    private final FitnessDataBaseParser.DependDataType lactateTypeExistType = new FitnessDataBaseParser.DependDataType(90, new Integer[]{11});
    public final FitnessDataBaseParser.DependDataType getCourseIdExistType() { return courseIdExistType; }
    public final FitnessDataBaseParser.DependDataType getLactateTypeExistType() { return lactateTypeExistType; }
    public abstract FitnessDataBaseParser.OneDimenDataType[] getReportDataTypeArray();
""" + report_type + "\n}\n")

# Each report parser: keep only its constructor (the field table). Parsers without one have no
# native table (esports is schema-only).
out_dir = f"{WORK}/com/xiaomi/fit/fitness/parser/sport/report"
os.makedirs(out_dir, exist_ok=True)
for path in glob.glob(f"{JADX}/com.xiaomi.fit.fitness.parser.sport.report.*.java"):
    name = path.split(".")[-2]
    s = open(path).read()
    head = f"    public {name}() {{"
    if head not in s:
        continue
    i = s.index(head)
    k, depth = s.index("{", i), 0
    while True:
        depth += {"{": 1, "}": -1}.get(s[k], 0)
        if depth == 0:
            break
        k += 1
    ctor = re.sub(r"([0-9]+) == true \? 1 : 0", r"\1", s[i:k + 1])  # jadx: boolean register reused as int
    with open(f"{out_dir}/{name}.java", "w") as f:
        f.write(f"""package com.xiaomi.fit.fitness.parser.sport.report;
import com.xiaomi.fit.fitness.parser.base.FitnessDataBaseParser;
import com.xiaomi.fit.fitness.parser.sport.base.SportReportBaseParser;
import kotlin.jvm.internal.DefaultConstructorMarker;
public final class {name} extends SportReportBaseParser {{
    private final FitnessDataBaseParser.OneDimenDataType[] reportDataTypeArray;
{ctor}
    public FitnessDataBaseParser.OneDimenDataType[] getReportDataTypeArray() {{ return reportDataTypeArray; }}
}}
""")

# Health / GPS parsers: their OneDimenDataType[] fields (constructor and/or initialisers).
OTHERS = ["fitness.parser.sport.gps.SportGpsParser", "fitness.parser.daily.WeightRecordParser",
          "fitness.parser.daily.AbnormalRecordParser", "fitness.parser.daily.EcgMeasureParser",
          "fitness.parser.daily.TemperatureMeasureParser"]
for cls in OTHERS:
    pkg, name = cls.rsplit(".", 1)
    s = open(f"{JADX}/com.xiaomi.fit.{cls}.java").read()
    decls = re.findall(r"    private final FitnessDataBaseParser\.OneDimenDataType\[\] \w+(?: = \{.*?\})?;", s, re.S)
    ctor = ""
    head = f"    public {name}() {{"
    if head in s:
        i = s.index(head)
        k, depth = s.index("{", i), 0
        while True:
            depth += {"{": 1, "}": -1}.get(s[k], 0)
            if depth == 0:
                break
            k += 1
        ctor = re.sub(r"([0-9]+) == true \? 1 : 0", r"\1", s[i:k + 1])
    d = f"{WORK}/com/xiaomi/fit/" + pkg.replace(".", "/")
    os.makedirs(d, exist_ok=True)
    with open(f"{d}/{name}.java", "w") as f:
        f.write(f"""package com.xiaomi.fit.{pkg};
import com.xiaomi.fit.fitness.parser.base.FitnessDataBaseParser;
import com.xiaomi.fit.fitness.parser.sport.base.SportReportBaseParser;
import kotlin.jvm.internal.DefaultConstructorMarker;
public final class {name} extends FitnessDataBaseParser {{
""" + "\n".join(decls) + "\n" + ctor + "\n}\n")

shutil.copy(f"{JADX}/com.xiaomi.fit.data.common.data.mi.FitnessDataValidity.java",
            f"{WORK}/com/xiaomi/fit/data/common/data/mi/FitnessDataValidity.java")
