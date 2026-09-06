"""DynamicLens rig: per-frame driver that pushes preset lens distortion + bokeh onto the active view.

Targets, in priority order:
  1. PIE / Movie Render Queue world: the player camera manager's view target (a CineCameraActor)
  2. Editor: the piloted / Sequencer-locked CineCameraActor
  3. Editor: the free level viewport (via a hidden transient proxy camera + a transient PostProcessVolume)

How distortion is applied (no Lens File asset, no Lens Component, no dependency on the editor world ticking):
  * one Epic distortion handler per camera component (SphericalLensDistortionModelHandler, transient, outer = camera)
  * one transient in-memory LensFile per camera holding a single distortion point that we overwrite every time the
    inputs change; `LensFile.evaluate_distortion_data` then draws the displacement maps + computes overscan
  * the handler's post-process MID is added to the camera as a blendable, and `CameraComponent.overscan` is set
    from the handler's overscan factor (MRG honours camera overscan since 5.6)
"""
import math, time
import unreal
from . import profiles

TAG = "DynamicLens"
PROXY_LABEL = "DynamicLens_ViewportProxy"
PPV_LABEL = "DynamicLens_ViewportPPV"
SETTINGS_CLASS = "/Game/CinematicTemplate/Lenses/DynamicLens/BP_DynamicLens.BP_DynamicLens_C"

STATE = {
    "enabled": False,
    "preset": "Master",
    "bokeh": True,
    "vignette": True,
    "viewport_focus_cm": 1000.0,        # focus distance assumed for the free viewport
    "viewport_sensor_w": 24.89,         # filmback assumed for the free viewport (Alexa 35 HD width)
    "viewport_fstop": 2.8,
    "free_viewport": True,
    "debug": False,
}

_rt = {
    "tick": None,
    "cams": {},          # actor path -> entry
    "proxy": None,
    "ppv": None,
    "vp": {"base_fov": None, "set_fov": None, "key": None},
    "last_target": None,
    "last_info": {},
    "last_settings_scan": 0.0,
    "settings_actor": None,
    "errors": 0,
}

_MODEL = {"spherical": (unreal.SphericalLensModel, unreal.SphericalLensDistortionModelHandler),
          "anamorphic": (unreal.AnamorphicLensModel, unreal.AnamorphicLensDistortionModelHandler)}


def _log(msg):
    unreal.log(f"[DynamicLens] {msg}")


def _invalidate():
    try:
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_invalidate_viewports()
    except Exception:
        pass


def _valid(obj):
    try:
        return obj is not None and unreal.SystemLibrary.is_valid(obj)
    except Exception:
        return False


# ----------------------------------------------------------------------------- distortion driver

def _ensure_driver(cc, entry, model_name):
    model_cls, handler_cls = _MODEL[model_name]
    if not _valid(entry.get("handler")) or entry.get("model") != model_name:
        entry["handler"] = unreal.new_object(handler_cls, cc)
        entry["mid"] = None
        entry["model"] = model_name
        entry["lensfile"] = None
    if not _valid(entry.get("lensfile")):
        lf = unreal.new_object(unreal.LensFile)
        li = lf.get_editor_property("lens_info")
        li.set_editor_property("lens_model", model_cls)
        lf.set_editor_property("lens_info", li)
        lf.set_editor_property("data_mode", unreal.LensDataMode.PARAMETERS)
        entry["lensfile"] = lf
        entry["sensor"] = None
    return entry["handler"], entry["lensfile"]


def _push_distortion(cc, entry, params, focal, sw, sh):
    h, lf = _ensure_driver(cc, entry, entry.get("want_model", "spherical"))
    if entry.get("sensor") != (sw, sh):
        li = lf.get_editor_property("lens_info")
        li.set_editor_property("sensor_dimensions", unreal.Vector2D(sw, sh))
        lf.set_editor_property("lens_info", li)
        entry["sensor"] = (sw, sh)
    di = unreal.DistortionInfo()
    di.set_editor_property("parameters", [float(x) for x in params])
    fl = unreal.FocalLengthInfo()
    fl.set_editor_property("fx_fy", unreal.Vector2D(focal / sw, focal / sh))
    lf.add_distortion_point(0.0, 0.0, di, fl)
    ok = lf.evaluate_distortion_data(0.0, 0.0, unreal.Vector2D(sw, sh), h)
    over = float(h.get_editor_property("overscan_factor")) if ok else 1.0
    cc.set_editor_property("overscan", max(0.0, min(1.0, over - 1.0)))
    if cc.get_editor_property("crop_overscan"):
        cc.set_editor_property("crop_overscan", False)
    mid = h.get_editor_property("distortion_post_process_mid")
    if mid is not None and entry.get("mid") != mid:
        if _valid(entry.get("mid")):
            cc.remove_blendable(entry["mid"])
        cc.add_or_update_blendable(mid, 1.0)
        entry["mid"] = mid
    return ok, over


# ----------------------------------------------------------------------------- bokeh / vignette

_PPS_KEYS = [
    ("depth_of_field_blade_count", "override_depth_of_field_blade_count"),
    ("depth_of_field_petzval_bokeh", "override_depth_of_field_petzval_bokeh"),
    ("depth_of_field_petzval_bokeh_falloff", "override_depth_of_field_petzval_bokeh_falloff"),
    ("depth_of_field_barrel_radius", "override_depth_of_field_barrel_radius"),
    ("depth_of_field_barrel_length", "override_depth_of_field_barrel_length"),
    ("vignette_intensity", "override_vignette_intensity"),
]


def _capture_pps(cc):
    pps = cc.get_editor_property("post_process_settings")
    return {k: (pps.get_editor_property(k), pps.get_editor_property(o)) for k, o in _PPS_KEYS}


def _restore_pps(cc, orig):
    pps = cc.get_editor_property("post_process_settings")
    for k, o in _PPS_KEYS:
        if k in orig:
            v, ov = orig[k]
            pps.set_editor_property(o, ov)
            pps.set_editor_property(k, v)
    cc.set_editor_property("post_process_settings", pps)


def _apply_look(cc, entry, ev):
    """bokeh + vignette into the camera's post process overrides (only when changed)."""
    want = {}
    bk = ev["bokeh"] if STATE["bokeh"] else {}
    if bk:
        want["depth_of_field_blade_count"] = int(bk.get("blades", 9))
        want["depth_of_field_petzval_bokeh"] = float(bk.get("petzval_eff", 0.0))
        want["depth_of_field_petzval_bokeh_falloff"] = float(bk.get("petzval_falloff", 2.0))
        want["depth_of_field_barrel_radius"] = float(bk.get("barrel_radius_eff", 0.0))
        want["depth_of_field_barrel_length"] = float(bk.get("barrel_length", 0.0))
    if STATE["vignette"]:
        want["vignette_intensity"] = float(ev["vignette"])
    last = entry.get("last_look")
    if last is not None and set(last) == set(want) and all(abs(last[k] - v) < 1e-3 for k, v in want.items()):
        return False
    pps = cc.get_editor_property("post_process_settings")
    for k, o in _PPS_KEYS:
        if k in want:
            pps.set_editor_property(o, True)
            pps.set_editor_property(k, want[k])
        elif k in entry["orig_pps"]:
            v, ov = entry["orig_pps"][k]
            pps.set_editor_property(o, ov)
            pps.set_editor_property(k, v)
    cc.set_editor_property("post_process_settings", pps)
    entry["last_look"] = want
    return True


# ----------------------------------------------------------------------------- apply to a camera

def apply_to_camera(actor, focus_override=None, fstop_override=None):
    cc = actor.get_cine_camera_component()
    key = actor.get_path_name()
    entry = _rt["cams"].get(key)
    if entry is None:
        entry = {"orig_pps": _capture_pps(cc), "last_look": None, "last_key": None}
        _rt["cams"][key] = entry
    focal = float(cc.get_editor_property("current_focal_length"))
    focus = float(focus_override if focus_override is not None else cc.get_editor_property("current_focus_distance"))
    fstop = float(fstop_override if fstop_override is not None else cc.get_editor_property("current_aperture"))
    fb = cc.get_editor_property("filmback")
    sw, sh = float(fb.sensor_width), float(fb.sensor_height)
    focus = max(focus, 1.0)
    ev = profiles.eval_preset(STATE["preset"], focal, focus, fstop)
    entry["want_model"] = profiles.profiles()[ev["profile"]].get("model", "spherical")
    cache_key = (STATE["preset"], round(focal, 3), round(focus, 1), round(fstop, 2), round(sw, 3), round(sh, 3), tuple(round(x, 6) for x in ev["params"]))
    changed = False
    if entry.get("last_key") != cache_key or not _valid(entry.get("mid")):
        ok, over = _push_distortion(cc, entry, ev["params"], focal, sw, sh)
        entry["last_key"] = cache_key
        entry["last_over"] = over
        changed = True
    if _apply_look(cc, entry, ev):
        changed = True
    if changed:
        _invalidate()
    info = {"target": actor.get_actor_label(), "focal": round(focal, 2), "focus_cm": round(focus, 1), "fstop": round(fstop, 2),
            "K1": round(ev["params"][0], 5), "K2": round(ev["params"][1], 5), "overscan": round(entry.get("last_over", 1.0), 4),
            "vignette": round(ev["vignette"], 3), "preset": STATE["preset"]}
    _rt["last_info"] = info
    return entry, info


def release_camera(key):
    entry = _rt["cams"].pop(key, None)
    if not entry:
        return
    try:
        h = entry.get("handler")
        cc = h.get_outer() if _valid(h) else None
        if _valid(cc):
            if _valid(entry.get("mid")):
                cc.remove_blendable(entry["mid"])
            cc.set_editor_property("overscan", 0.0)
            _restore_pps(cc, entry["orig_pps"])
        _invalidate()
    except Exception as e:
        _log(f"release {key}: {e}")


# ----------------------------------------------------------------------------- free viewport

def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _ensure_proxy(world):
    p = _rt["proxy"]
    if _valid(p) and p.get_world() == world:
        return p
    _rt["proxy"] = None
    p = _eas().spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(0, 0, -100000), unreal.Rotator(0, 0, 0), True)
    _rt["proxy"] = p
    p.set_actor_label(PROXY_LABEL)
    p.set_is_temporarily_hidden_in_editor(True)
    p.set_actor_hidden_in_game(True)
    cc = p.get_cine_camera_component()
    fs = cc.get_editor_property("focus_settings")
    fs.set_editor_property("focus_method", unreal.CameraFocusMethod.DISABLE)
    cc.set_editor_property("focus_settings", fs)
    _rt["proxy"] = p
    _log("spawned viewport proxy camera")
    return p


def _ensure_ppv(world):
    v = _rt["ppv"]
    if _valid(v) and v.get_world() == world:
        return v
    _rt["ppv"] = None
    v = _eas().spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, -100000), unreal.Rotator(0, 0, 0), True)
    _rt["ppv"] = v
    v.set_actor_label(PPV_LABEL)
    v.set_editor_property("unbound", True)
    v.set_editor_property("priority", 1000.0)
    v.set_editor_property("blend_weight", 1.0)
    _rt["ppv"] = v
    _log("spawned viewport post process volume")
    return v


def _set_ppv(ppv, mid, look):
    s = ppv.get_editor_property("settings")
    wb = s.get_editor_property("weighted_blendables")
    arr = wb.get_editor_property("array")
    cur = [x.get_editor_property("object") for x in arr]
    new = []
    if mid is not None:
        b = unreal.WeightedBlendable()
        b.set_editor_property("weight", 1.0)
        b.set_editor_property("object", mid)
        new = [b]
    if cur != ([mid] if mid is not None else []):
        wb.set_editor_property("array", new)
        s.set_editor_property("weighted_blendables", wb)
    for k, o in _PPS_KEYS:
        if k in look:
            s.set_editor_property(o, True)
            s.set_editor_property(k, look[k])
        else:
            s.set_editor_property(o, False)
    ppv.set_editor_property("settings", s)


def _free_viewport_tick(les, ues, world):
    vp = _rt["vp"]
    key = les.get_active_viewport_config_key()
    fov_now = float(les.get_level_viewport_fov(key))
    if vp["key"] != str(key) or vp["set_fov"] is None or abs(fov_now - vp["set_fov"]) > 0.05:
        vp["base_fov"] = fov_now          # user-owned FOV (no overscan)
        vp["key"] = str(key)
    base = vp["base_fov"]
    size = ues.get_level_viewport_size()
    aspect = (float(size.x) / float(size.y)) if size.y > 0 else 16 / 9
    proxy = _ensure_proxy(world)
    cc = proxy.get_cine_camera_component()
    sw = STATE["viewport_sensor_w"]
    fb = cc.get_editor_property("filmback")
    if abs(fb.sensor_width - sw) > 1e-3 or abs(fb.sensor_height - sw / aspect) > 1e-3:
        fb.set_editor_property("sensor_width", sw)
        fb.set_editor_property("sensor_height", sw / aspect)
        cc.set_editor_property("filmback", fb)
    focal = (sw / 2.0) / math.tan(math.radians(base) / 2.0)
    if abs(cc.get_editor_property("current_focal_length") - focal) > 1e-3:
        cc.set_editor_property("current_focal_length", focal)
    entry, info = apply_to_camera(proxy, STATE["viewport_focus_cm"], STATE["viewport_fstop"])
    info["target"] = "viewport"
    ppv = _ensure_ppv(world)
    look = entry.get("last_look") or {}
    if entry.get("ppv_key") != (entry.get("mid"), tuple(sorted(look.items()))):
        _set_ppv(ppv, entry.get("mid"), look)
        entry["ppv_key"] = (entry.get("mid"), tuple(sorted(look.items())))
    over = float(entry.get("last_over", 1.0))
    want = math.degrees(2.0 * math.atan(math.tan(math.radians(base) / 2.0) * over))
    if abs(want - fov_now) > 0.01:
        les.set_level_viewport_fov(want, key)
    vp["set_fov"] = want
    info["overscan"] = round(over, 4)
    info["fov"] = round(base, 2)
    _rt["last_info"] = info


def _release_free_viewport(les):
    vp = _rt["vp"]
    try:
        if vp["base_fov"] is not None and vp["key"]:
            key = les.get_active_viewport_config_key()
            if str(key) == vp["key"] and abs(float(les.get_level_viewport_fov(key)) - (vp["set_fov"] or -1)) < 0.05:
                les.set_level_viewport_fov(vp["base_fov"], key)
    except Exception as e:
        _log(f"restore fov: {e}")
    vp["base_fov"] = None
    vp["set_fov"] = None
    if _valid(_rt["ppv"]):
        try:
            _set_ppv(_rt["ppv"], None, {})
        except Exception:
            pass
    if _valid(_rt["proxy"]):
        release_camera(_rt["proxy"].get_path_name())


# ----------------------------------------------------------------------------- target discovery

def _find_target(les, ues):
    """returns (kind, actor, world)"""
    if les.is_in_play_in_editor():
        world = ues.get_game_world()
        if world:
            try:
                pcm = unreal.GameplayStatics.get_player_camera_manager(world, 0)
                vt = pcm.get_view_target() if pcm else None
                if isinstance(vt, unreal.CineCameraActor):
                    return "pie", vt, world
            except Exception:
                pass
        return "none", None, world
    world = ues.get_editor_world()
    pilot = les.get_pilot_level_actor()
    if isinstance(pilot, unreal.CineCameraActor):
        return "pilot", pilot, world
    if STATE["free_viewport"]:
        return "viewport", None, world
    return "none", None, world


def _read_settings_actor():
    """optional BP_DynamicLens actor (tag DynamicLensSettings) in the level overrides STATE (scanned every 2 s)."""
    now = time.time()
    a = _rt["settings_actor"]
    if now - _rt["last_settings_scan"] > 2.0 or not _valid(a):
        _rt["last_settings_scan"] = now
        a = None
        try:
            w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            cls = _rt.get("settings_class")
            if not _valid(cls):
                cls = unreal.load_class(None, SETTINGS_CLASS)
                _rt["settings_class"] = cls
            found = list(unreal.GameplayStatics.get_all_actors_of_class(w, cls)) if cls else []
            if not found:
                found = list(unreal.GameplayStatics.get_all_actors_with_tag(w, unreal.Name("DynamicLensSettings")))
            a = found[0] if found else None
        except Exception:
            a = None
        _rt["settings_actor"] = a
    if not _valid(a):
        return
    for prop, key, conv in [("Enabled", "enabled", bool), ("Preset", "preset", str), ("Bokeh", "bokeh", bool), ("Vignette", "vignette", bool),
                            ("FreeViewport", "free_viewport", bool), ("ViewportFocusCm", "viewport_focus_cm", float)]:
        try:
            v = a.get_editor_property(prop)
            if conv is str:
                v = str(v)
                if v not in profiles.presets():
                    continue
            STATE[key] = conv(v)
        except Exception:
            pass


# ----------------------------------------------------------------------------- tick

def _tick(dt):
    try:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        _read_settings_actor()
        if not STATE["enabled"]:
            if _rt["last_target"] is not None:
                _release_all(les)
            return
        kind, actor, world = _find_target(les, ues)
        target_key = (kind, actor.get_path_name() if actor else None)
        if target_key != _rt["last_target"]:
            keep = actor.get_path_name() if actor else None
            proxy_key = _rt["proxy"].get_path_name() if _valid(_rt["proxy"]) else None
            for k in list(_rt["cams"]):
                if k != keep and k != proxy_key:
                    release_camera(k)
            if kind != "viewport":
                _release_free_viewport(les)
            _rt["last_target"] = target_key
            _log(f"target -> {kind} {actor.get_actor_label() if actor else ''}")
        if kind in ("pie", "pilot"):
            apply_to_camera(actor)
        elif kind == "viewport":
            _free_viewport_tick(les, ues, world)
        _rt["errors"] = 0
    except Exception:
        _rt["errors"] += 1
        if _rt["errors"] < 5 or STATE["debug"]:
            import traceback
            _log("tick error: " + traceback.format_exc())


def _release_all(les):
    for k in list(_rt["cams"]):
        release_camera(k)
    _release_free_viewport(les)
    _rt["last_target"] = None
    _rt["last_info"] = {}


# ----------------------------------------------------------------------------- public API

def start():
    if _rt["tick"] is None:
        profiles.reload()
        _rt["tick"] = unreal.register_slate_post_tick_callback(_tick)
        _log(f"started (preset={STATE['preset']}, enabled={STATE['enabled']})")


def stop():
    if _rt["tick"] is not None:
        unreal.unregister_slate_post_tick_callback(_rt["tick"])
        _rt["tick"] = None
    _release_all(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem))
    for key in ("proxy", "ppv"):
        a = _rt[key]
        if _valid(a):
            try:
                _eas().destroy_actor(a)
            except Exception:
                pass
        _rt[key] = None
    _log("stopped")


def on(preset=None):
    if preset:
        set_preset(preset)
    STATE["enabled"] = True
    start()


def off():
    STATE["enabled"] = False


def set_preset(name):
    if name not in profiles.presets():
        raise ValueError(f"unknown preset {name}; have {list(profiles.presets())}")
    STATE["preset"] = name
    for e in _rt["cams"].values():
        e["last_look"] = None
    _log(f"preset -> {name}")


def status():
    return {"state": dict(STATE), "target": _rt["last_target"], "info": dict(_rt["last_info"]),
            "cams": list(_rt["cams"]), "ticking": _rt["tick"] is not None}
