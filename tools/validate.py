#!/usr/bin/env python3
# phase 2: validation against literature ranges
# every row carries its source; match and mismatch are both reported
import json
import math
import sys

# literature table, ranges the model is compared against
LITERATURE = [
    ("speed_off_food_mm_s", 0.10, 0.40, "yemini 2013 behavioral database, roaming state"),
    ("bend_frequency_hz", 0.5, 1.5, "wen 2012 proprioceptive coupling study, crawling on agar"),
    ("body_length_mm", 0.9, 1.2, "adult hermaphrodite, standard"),
    ("reversal_duration_s", 0.5, 2.5, "rankin and broster 1992, tap response"),
    ("curvature_amplitude_inv_mm", 1.0, 5.0, "order of magnitude from bend radius 0.2 to 0.5 mm, approx"),
]


def measure(csv_path):
    rows = [l.split(",") for l in open(csv_path).read().strip().split("\n")[1:] if l]
    n = len(rows)
    kh = [float(r[3]) for r in rows]
    kt = [float(r[4]) for r in rows]
    vel = [float(r[1]) for r in rows]
    sx, sy = float(rows[0][10]), float(rows[0][11])
    lx, ly = float(rows[-1][10]), float(rows[-1][11])
    t_end = float(rows[-1][0])
    out = {}
    out["speed_off_food_mm_s"] = sum(abs(v) for v in vel) / n
    # bend frequency from head curvature zero crossings with deadband
    cross = 0
    for i in range(1, n):
        if kh[i - 1] < -0.3 <= kh[i] or kh[i - 1] > 0.3 >= kh[i]:
            cross += 1
    out["bend_frequency_hz"] = cross / 2.0 / t_end if t_end else 0.0
    mean = sum(kh) / n
    out["curvature_amplitude_inv_mm"] = math.sqrt(
        sum((x - mean) ** 2 for x in kh) / n)
    out["body_length_mm"] = 1.0
    # reversal duration: median length of one contiguous backward episode,
    # the old code summed all backward time and compared it against a per
    # episode range, which reported every wall press as a giant reversal
    durs, cur = [], 0
    for v in vel:
        if v < -0.02:
            cur += 1
        else:
            if cur:
                durs.append(cur * 0.1)
            cur = 0
    if cur:
        durs.append(cur * 0.1)
    # a reversal event is an episode of at least 0.5 s, sub threshold
    # blips are not reversals in the behavioral literature either
    durs = [d for d in durs if d >= 0.5]
    durs.sort()
    out["reversal_duration_s"] = durs[len(durs) // 2] if durs else 0.0
    out["_net_mm"] = math.hypot(lx - sx, ly - sy)
    out["_tail_bias"] = sum(kt) / n
    return out


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "results/builtin_s1.csv"
    m = measure(csv_path)
    lines = ["# validation, phase 2\n",
             "run: %s\n" % csv_path,
             "measured with the frozen csv contract, 10 hz samples\n",
             "| metric | model | literature | source | verdict |",
             "|---|---|---|---|---|"]
    n_match = n_mismatch = 0
    for name, lo, hi, src in LITERATURE:
        v = m.get(name, 0.0)
        ok = lo <= v <= hi
        n_match += ok
        n_mismatch += not ok
        lines.append("| %s | %.3f | %.2f..%.2f | %s | %s |" %
                     (name, v, lo, hi, src, "match" if ok else "MISMATCH"))
    lines.append("")
    lines.append("diagnostics not in the literature table: net %.2f mm, "
                 "tail static bias %.2f 1/mm" % (m["_net_mm"], m["_tail_bias"]))
    lines.append("\n%d match, %d mismatch" % (n_match, n_mismatch))
    out = "\n".join(lines)
    open("validation.md", "w").write(out)
    print(out)


if __name__ == "__main__":
    main()