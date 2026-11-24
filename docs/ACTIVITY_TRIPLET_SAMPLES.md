# Steps / calories / active minutes: samples

Samples compare what the band shows with what the app shows, plus the raw RealTimeStats fields,
so that the right decoding for each field can be worked out from the data.

RealTimeStats (protobuf, `Xiaomi__RealTimeStats`): field 1 `steps`, field 2 `calories`,
field 3 `unknown3`, field 4 `heartRate`, field 5 `unknown5`, then `standingHours`.
The app's log prints them as `Realtime stats steps=… calories=… activity=… rt5=… rt3=…`.

How the app gets each value today (connected, viewing today):

- **Steps**: RealTimeStats field 1. This matched the band in every reading, so from row 4 on, band
  steps aren't recorded. The raw `rt steps` value is still kept, since `rt3` ÷ steps checks the
  distance theory.
- **Calories**: RealTimeStats field 2, used as is (`src/ui/dashboard.c:1252`).
- **Active minutes**: worked out from `rt3` and `rt5` by `salus_activity_time_from_realtime()`
  (`src/store/health_data.c:362`). `rt3` up to 180 counts as minutes; above that it is divided by
  92 and rounded. `rt5` (≤ 600) counts as minutes. The rule for choosing between them was guessed.

| # | Date / time | Band steps | Band kcal | Band active min | App steps | App kcal | App active min | rt steps | rt calories | rt3 | rt5 | Notes |
|---|-------------|-----------:|----------:|----------------:|----------:|---------:|---------------:|---------:|------------:|----:|----:|-------|
| 1 | 2026-11-18 | 4435 | 179 | 37 | 4435 | 213 | 7 | | | | | raw log line not captured |
| 2 | 2026-11-18 | 4489 | 251 | 38 | 4489 | 285 | 7 | 4489 | 285 | 3190 | 7 | hr=80 standing=0 |
| 3 | 2026-11-18 | 4920 | 346 | 39 | 4920 | 381 | 7 | 4920 | 380 | 3453 | 7 | hr=70 standing=0; app was 1 kcal past the log line (a newer packet) |
| 4 | 2026-11-18 | — | 596 | 53 | — | 637 | 7 | 7378 | 637 | 5042 | 7 | hr=84 standing=0; replaces a wrong reading (586 / 628) |
| 5 | 2026-11-19 | — | 263 | 41 | — | 278 | 7 | 4945 | 278 | 3162 | 7 | hr=73 standing=0 |
| 6 | 2026-11-19 | — | 299 | 50 | — | 321 | 7 | 5987 | 321 | 3867 | 7 | no hr in the packet; standing=0 |
| 7 | 2026-11-19 | — | 418 | 76 | — | 452 | 7 | 9073 | 451 | 5860 | 7 | standing=0; app 1 kcal past the log line |
| 8 | 2026-11-19 | — | 422 | 76 | — | 456 | 7 | 9073 | 456 | 5860 | 7 | raw=08f14610c80318e42d28073000 |
| 9 | 2026-11-19 | — | 453 | 76 | — | 486 | 7 | | | | | sync: band offered only today's summary file (ts=1795042800, ver 5), no per-minute file |
| 10 | 2026-11-20 | 1417 | 97 | 10 | 1417 | 104 | 10 | 1417 | 104 | 944 | 7 | see "Row 10" below; band = totalactiveCalories, active = minutes at intensity 3 |

## Findings so far

- **`rt3` is distance in metres, not activity time.** 3190 m / 4489 steps = 0.71 m per step,
  a normal stride length. So dividing it by 92 to get minutes is wrong.
- **`rt5` stays at 7** while the band's active minutes go from 37 to 38, so it isn't active minutes either.
- **The live `calories` field is above the band's figure, and the gap is not fixed.** It was 34 in
  rows 1–3 (213 − 179, 285 − 251, 380 − 346), then 41 in row 4 (637 − 596).
  As a ratio, band ÷ app is 0.84, 0.88, 0.91, 0.94, so it isn't a fixed scale factor either.
  Possibly the gap only goes up now and then, e.g. once an hour or after a workout.
- `rt3` ÷ steps is 0.711 m in row 2 and 0.702 m in row 3, which fits distance in metres.
- **Active minutes are probably counted from the per-minute activity record**, whose `activeStreng` field
  (bits 7–5 of the `activeStrengthAndSportType` byte) gives an intensity level for each minute:
  0 still, 1 very light, 2 light, 3 moderate, 4 high, 5 intense, 7 unknown.
  `src/devices/xiaomi/daily_details.c` decodes it now; it skipped it before.

## Raw live packet (row 8)

`08f146 10c803 18e42d 2807 3000` holds only field 1 = 9073 (steps), field 2 = 456 (calories),
field 3 = 5860 (`rt3`), field 5 = 7 (`rt5`) and field 6 = 0 (standing). The schema doesn't miss any
fields, so **the live packet doesn't carry the band's own kcal or active minutes**.

The next place to look is the synced per-minute activity file, whose fields are now all decoded:
- `activeTypeAndCalories` (1 byte): bits 7–6 activity type (0 unknown, 1 still, 2 walking, 3 running),
  bits 5–0 calories for that minute.
- `activeStrengthAndSportType` (1 byte): bits 7–5 intensity (see above), bits 4–0 workout type.

After each sync, the log prints one `Daily detail … activity:` line per file, with the sums for that
file. The `Daily report (schema)` line now prints both `totalCalories` and `totalactiveCalories`.

## Daily summary file (ACTIVITY_DAILY, detail=1)

The Gadgetbridge-style parser (`parse_daily_summary_for_log`, `src/devices/xiaomi/activity.c`) skipped the 3 bytes
after `totalSteps` as unknown. Mi Fitness's `daily_activity_report` schema says they are
`totalactiveCalories` (u16) and the current heart rate (u8). The field it read as `calories` is
`totalCalories`. Both are logged now, on the `Daily summary … (v5):` line.

## Per-minute files cover only the time since the last sync (2026-11-19)

A sync at about 19:08 got one `Daily detail` file starting at 19:06, with 2 minutes, both intensity 7
(unknown) and type 0. The band sends only the minutes since the last acked file, so one file's sums
never cover the whole day. Each minute's intensity and activity type are now saved to
`XIAOMI_ACTIVITY_SAMPLE.rawIntensity` / `rawKind` (they used to always be -1). After each sync the log
prints a `Day activity totals <day>:` line that adds up every stored minute of that day. Minutes saved
by older builds show up as `n/a`, so 2026-11-20 is the first day that can be complete.

Live packet at that sync: `raw=08f146 108104 18e42d 205c 2807 3000`, i.e. steps 9073, calories 513,
rt3 5860, hr 92, rt5 7, standing 0. Steps were the same as in rows 7–8 while calories kept rising
(456 → 486 → 513).

## Row 10: first full day with the new build (2026-11-20, about 02:08)

| | Band | App | Source in the sync |
|---|---:|---:|---|
| kcal | **97** | 104 | summary file `totalactiveCalories=97`, `totalCalories=104`; Σ per-minute `activityCalories` = `kcal_sum=97`; live field 2 = 104 |
| active min | **10** | 10 | `Day activity totals`: `intensity{3=10}`, every other minute is 7 (unknown) |
| steps | 1417 | 1417 | summary file, Σ per-minute steps, live field 1 |

Check against yesterday's complete file (2026-11-19, read the next day): `totalactiveCalories=692` equals
`Day activity totals 2026-11-19 kcal_sum=692`, the sum of every stored minute, including the ones saved
by older builds. `totalCalories − totalactiveCalories = 733 − 692 = 41`, the same as the row 4 gap.

## Decoding (exact, no fitting)

- **Band kcal = `totalactiveCalories`**: daily summary file (ACTIVITY_DAILY, detail=1, v5), the u16 LE
  right after `totalSteps`. It also equals the sum of the per-minute `activityCalories` (bits 5–0 of
  the `activeTypeAndCalories` byte) over the day.
- **Live RealTimeStats field 2 = `totalCalories`**, the same counter as in the summary file. The gap to
  the band is `totalCalories − totalactiveCalories`, the non-active part of the day's calories. It only
  ever goes up (15 → 22 → 33 → 34 → 41 on 2026-11-19), which explains every gap in rows 1–9.
- **Band active minutes = the number of minutes whose `activeStreng` (bits 7–5 of the
  `activeStrengthAndSportType` byte) is 3.** So far only 3 and 7 appear, so it's not yet known
  whether 4 and 5 also count; the schema calls them "high" and "intense", so they very likely do.
- **The live packet doesn't carry active minutes.** `rt3` is distance in metres; `rt5` is unknown
  (always 7). The app's "activity=10" in row 10 is a coincidence: `rt3`/92 = 944/92 ≈ 10.

## Implemented (2026-11-20)

The dashboard now uses only synced values:
- **Calories ring** = the day's `totalactiveCalories`, stored in the new
  `XIAOMI_DAILY_SUMMARY_SAMPLE.activeCalories` column (added to existing databases automatically), or
  the sum of the per-minute calories if that is larger (both only grow during the day). The live
  `totalCalories` is no longer shown.
- **Active minutes ring** = the number of the day's stored minutes with intensity 3–5
  (`RecordedSample.intensity`, loaded from `XIAOMI_ACTIVITY_SAMPLE.rawIntensity`). The `rt3`/`rt5`
  guess and the step/heart-rate/calorie heuristic are gone.
- The `Realtime stats` log line now prints `totalCalories=` and `distance_m=` instead of
  `calories=`, `activity=` and `rt3=`.
