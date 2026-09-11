# Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
# Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

"""Lens profile + preset evaluation. Pure Python, no unreal import (testable outside the editor)."""
import json, math, os

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE_DIR = os.path.join(HERE, "data", "profiles")
PRESET_FILE = os.path.join(HERE, "data", "presets.json")

_profiles = {}
_presets = {}


def reload():
    global _profiles, _presets
    _profiles = {}
    for fn in os.listdir(PROFILE_DIR):
        if fn.endswith(".json"):
            p = json.load(open(os.path.join(PROFILE_DIR, fn)))
            p["_log_focals"] = [math.log(f) for f in p["focals"]]
            p["_log_focus"] = [math.log(f) for f in p["focus_cm"]]
            _profiles[p["name"]] = p
    _presets = json.load(open(PRESET_FILE))["presets"]
    return list(_profiles), list(_presets)


def profiles():
    if not _profiles:
        reload()
    return _profiles


def presets():
    if not _presets:
        reload()
    return _presets


def _bracket(xs, x):
    """index i and blend t such that value = lerp(xs[i], xs[i+1], t); clamped."""
    if x <= xs[0]:
        return 0, 0.0
    if x >= xs[-1]:
        return len(xs) - 2, 1.0
    lo, hi = 0, len(xs) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if xs[mid] <= x:
            lo = mid
        else:
            hi = mid
    return lo, (x - xs[lo]) / (xs[hi] - xs[lo])


def eval_profile(profile, focal_mm, focus_cm):
    """Bilinear in (log focal, log focus). Returns [K1,K2,K3,P1,P2]."""
    p = profiles()[profile] if isinstance(profile, str) else profile
    fi, ft = _bracket(p["_log_focals"], math.log(max(focal_mm, 1e-3)))
    di, dt = _bracket(p["_log_focus"], math.log(max(focus_cm, 1.0)))
    g = p["grid"]
    a = g[fi][di]; b = g[fi][di + 1]; c = g[fi + 1][di]; d = g[fi + 1][di + 1]
    return [((a[k] * (1 - dt) + b[k] * dt) * (1 - ft) + (c[k] * (1 - dt) + d[k] * dt) * ft) for k in range(5)]


def _smooth01(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def eval_preset(preset, focal_mm, focus_cm, fstop=2.8):
    """Returns dict: params (5 floats), vignette, bokeh dict, meta."""
    pr = presets()[preset] if isinstance(preset, str) else preset
    prof = pr["profile"]
    base = eval_profile(prof, focal_mm, focus_cm)
    inf = eval_profile(prof, focal_mm, 1e6)
    br = pr.get("breathing", 1.0)
    params = [inf[k] + (base[k] - inf[k]) * br for k in range(5)]
    amount = pr.get("amount", 1.0)
    params = [x * amount for x in params]
    wb = pr.get("wide_boost")
    if wb:
        t = _smooth01((wb["below_mm"] - focal_mm) / max(wb["below_mm"] - wb["full_mm"], 1e-3))
        params[0] += wb.get("k1", 0.0) * t
        params[1] += wb.get("k2", 0.0) * t
    vig = pr.get("vignette", {})
    vt = _smooth01((math.log(max(focal_mm, 1)) - math.log(vig.get("long_mm", 100))) / (math.log(vig.get("wide_mm", 14)) - math.log(vig.get("long_mm", 100)))) if vig else 0.0
    ft = _smooth01((fstop - vig.get("fstop_open", 1.4)) / max(vig.get("fstop_closed", 5.6) - vig.get("fstop_open", 1.4), 1e-3)) if vig else 1.0
    vig_int = (vig.get("at_long", 0.0) + (vig.get("at_wide", 0.0) - vig.get("at_long", 0.0)) * vt) * (1.0 - ft * vig.get("stopdown_fade", 0.7)) if vig else 0.0
    bk = dict(pr.get("bokeh", {}))
    if bk:
        # mechanical (cat's eye) vignetting fades as the iris closes
        fade = 1.0 - _smooth01((fstop - bk.get("fstop_open", 1.4)) / max(bk.get("fstop_closed", 5.6) - bk.get("fstop_open", 1.4), 1e-3)) * bk.get("stopdown_fade", 0.8)
        bk["barrel_radius_eff"] = bk.get("barrel_radius", 0.0) * fade
        bk["petzval_eff"] = bk.get("petzval", 0.0) * fade
    return {"params": params, "vignette": vig_int, "bokeh": bk, "profile": prof, "amount": amount}
