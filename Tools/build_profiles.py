# Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
# Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

"""Build smooth lens profiles from a raw Lens File dump (DynamicLens/Tools).

Input : data/raw/<name>.json  (output of dump_lensfiles.py: per lens file, distortion points)
Output: data/profiles/<Series>.json  — a regular grid  focal x focus_cm  -> K1,K2,K3,P1,P2

Run with any Python 3 (no numpy needed):  python build_profiles.py
"""
import json, math, os, re, collections

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "data", "raw", "andy_davis_lensfiles.json")  # Tools/data/raw
OUT = os.path.join(HERE, "data", "profiles")

# focus grid in cm: dense near close focus, sparse far away, last entry = "infinity"
FOCUS_GRID = [20, 25, 30, 35, 40, 45, 50, 60, 70, 80, 90, 100, 120, 150, 200, 250, 300, 400, 500,
              700, 1000, 1500, 2000, 3000, 5000, 8000, 12000, 20000]

SERIES = {
    "ARRI_Master": {"prefix": "ARRIZEISS_Master_", "exclude": ["MACRO"], "label": "ARRI/Zeiss Master Prime (spherical)"},
    "ZEISS_Supreme": {"prefix": "ZEISS_Supreme_", "exclude": [], "label": "Zeiss Supreme Prime (spherical)"},
}


def focal_from_name(name):
    m = re.search(r"_(\d{3})mm", name)
    return float(m.group(1)) if m else None


def lerp_series(xs, ys, x):
    """piecewise-linear with clamped ends. xs ascending."""
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            t = (x - xs[i - 1]) / (xs[i] - xs[i - 1])
            return ys[i - 1] + t * (ys[i] - ys[i - 1])
    return ys[-1]


def build_series(raw, key, spec):
    lenses = []
    for name, d in raw.items():
        if not name.startswith(spec["prefix"]) or any(x in name for x in spec["exclude"]):
            continue
        focal = focal_from_name(name)
        pts = d["distortion"]
        # each file carries a leftover zoom point from Andy's previous focal -> keep the dominant zoom only
        zoom = collections.Counter(round(p["zoom"], 1) for p in pts).most_common(1)[0][0]
        sel = sorted((p for p in pts if round(p["zoom"], 1) == zoom), key=lambda p: p["focus"])
        if len(sel) < 2:
            print(f"  skip {name}: only {len(sel)} focus points")
            continue
        xs = [p["focus"] for p in sel]
        rows = []
        for f in FOCUS_GRID:
            rows.append([lerp_series(xs, [p["params"][i] for p in sel], f) for i in range(5)])
        lenses.append({"name": name, "focal": focal, "n_points": len(sel),
                       "focus_min_cm": xs[0], "focus_max_cm": xs[-1], "sensor_mm": d["sensor_mm"], "grid": rows})
        print(f"  {name:30s} f={focal:6.1f}  {len(sel):2d} pts  focus {xs[0]:.0f}..{xs[-1]:.0f} cm  K1(inf)={rows[-1][0]:+.4f}")
    lenses.sort(key=lambda l: l["focal"])
    return {
        "name": key, "label": spec["label"], "model": "spherical",
        "param_names": ["K1", "K2", "K3", "P1", "P2"],
        "focals": [l["focal"] for l in lenses],
        "focus_cm": FOCUS_GRID,
        "grid": [l["grid"] for l in lenses],
        "sources": [{k: v for k, v in l.items() if k != "grid"} for l in lenses],
    }


def main():
    raw = json.load(open(RAW))
    os.makedirs(OUT, exist_ok=True)
    for key, spec in SERIES.items():
        print(key)
        prof = build_series(raw, key, spec)
        with open(os.path.join(OUT, key + ".json"), "w") as f:
            json.dump(prof, f)
        print(f"  -> {len(prof['focals'])} focals x {len(FOCUS_GRID)} focus steps")


if __name__ == "__main__":
    main()
