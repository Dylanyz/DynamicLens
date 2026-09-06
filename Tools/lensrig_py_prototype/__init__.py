"""DynamicLens — engine-wide, focal/focus-driven lens distortion + bokeh presets for UE 5.8.

In the editor Python console:
    import lensrig
    lensrig.on("Master")        # start ticking + enable (presets: lensrig.presets())
    lensrig.set_preset("Vintage")
    lensrig.off()               # keep ticking, remove effect
    lensrig.stop()              # unregister tick, clean up
    lensrig.status()
    lensrig.reload()            # re-read data/presets.json + profiles after editing
"""
from . import profiles
try:
    import unreal  # noqa: F401
    from .rig import STATE, start, stop, on, off, set_preset, status
except ImportError:  # outside the editor: profiles/evaluate only
    STATE = start = stop = on = off = set_preset = status = None

__all__ = ["STATE", "start", "stop", "on", "off", "set_preset", "status", "presets", "reload", "evaluate"]


def presets():
    return {k: v.get("label", "") for k, v in profiles.presets().items()}


def reload():
    r = profiles.reload()
    from .rig import _rt
    for e in _rt["cams"].values():
        e["last_look"] = None
    return r


def evaluate(preset, focal_mm, focus_cm, fstop=2.8):
    return profiles.eval_preset(preset, focal_mm, focus_cm, fstop)
