"""DynamicLens editor tools (auto on sys.path because this is the plugin's Content/Python folder).

    import dynamiclens_tools as dl
    dl.import_profiles()          # Tools/data/profiles/*.json  -> /DynamicLens/Profiles/DLP_<name>
    dl.import_presets()           # Tools/data/presets.json     -> /DynamicLens/Presets/DL_<name>
    dl.add_to_all_cameras("DL_Master")   # add a Dynamic Lens component to every CineCameraActor in the level
    dl.remove_from_all_cameras()
    dl.status()                   # what every Dynamic Lens component in the level is doing right now
"""
import json, os
import unreal

PLUGIN_DIR = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DATA_DIR = os.path.join(PLUGIN_DIR, "Tools", "data")
PROFILE_PKG = "/DynamicLens/Profiles"
PRESET_PKG = "/DynamicLens/Presets"


def _log(msg):
    unreal.log(f"[DynamicLens] {msg}")


def _create_data_asset(name, package_path, cls):
    path = f"{package_path}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, package_path, cls, factory)
    if asset is None:
        raise RuntimeError(f"could not create {path}")
    return asset


def import_profiles(profile_dir=None, save=True):
    """JSON grids (from Tools/build_profiles.py) -> UDynamicLensProfile assets."""
    profile_dir = profile_dir or os.path.join(DATA_DIR, "profiles")
    created = []
    for fn in sorted(os.listdir(profile_dir)):
        if not fn.endswith(".json"):
            continue
        j = json.load(open(os.path.join(profile_dir, fn)))
        name = "DLP_" + j["name"]
        asset = _create_data_asset(name, PROFILE_PKG, unreal.DynamicLensProfile)
        flat = [float(x) for row in j["grid"] for cell in row for x in cell]
        ok = unreal.DynamicLensLibrary.fill_profile(asset, j.get("label", j["name"]),
                                                    "Fitted from measured Lens Files (Andy Davis / Imagery for Media), see Tools/data/raw",
                                                    [float(x) for x in j["focus_cm"]], [float(x) for x in j["focals"]], flat)
        if not ok:
            raise RuntimeError(f"fill_profile failed for {fn}")
        specs = json.load(open(os.path.join(DATA_DIR, "presets.json"))).get("profile_specs", {}).get(j["name"])
        if specs:
            asset.set_editor_property("front_diameter_mm", float(specs["front_diameter_mm"]))
            asset.set_editor_property("barrel_length_mm", float(specs["barrel_length_mm"]))
            asset.set_editor_property("iris_blades", int(specs["iris_blades"]))
            asset.set_editor_property("max_aperture", float(specs["max_aperture"]))
            asset.set_editor_property("source", specs.get("source", asset.get_editor_property("source")))
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{PROFILE_PKG}/{name}")
        _log(f"profile {name}: {len(j['focals'])} focals x {len(j['focus_cm'])} focus steps")
    return created


def _set_struct(obj, prop, values):
    s = obj.get_editor_property(prop)
    for k, v in values.items():
        s.set_editor_property(k, v)
    obj.set_editor_property(prop, s)


def import_presets(preset_file=None, save=True):
    """Tools/data/presets.json -> UDynamicLensPreset assets referencing the profile assets."""
    preset_file = preset_file or os.path.join(DATA_DIR, "presets.json")
    presets = json.load(open(preset_file))["presets"]
    created = []
    for name, p in presets.items():
        asset = _create_data_asset("DL_" + name, PRESET_PKG, unreal.DynamicLensPreset)
        prof = unreal.load_asset(f"{PROFILE_PKG}/DLP_{p['profile']}")
        if prof is None:
            raise RuntimeError(f"profile DLP_{p['profile']} missing; run import_profiles() first")
        asset.set_editor_property("description", p.get("label", name))
        asset.set_editor_property("profile", prof)
        asset.set_editor_property("amount", float(p.get("amount", 1.0)))
        asset.set_editor_property("breathing", float(p.get("breathing", 1.0)))
        asset.set_editor_property("out_of_range", getattr(unreal.DynamicLensRangeMode, p.get("out_of_range", "Clamp").upper()))
        wb = p.get("wide_boost")
        _set_struct(asset, "wide_boost", {"enabled": wb is not None, **({"below_mm": float(wb["below_mm"]), "full_mm": float(wb["full_mm"]), "k1": float(wb.get("k1", 0)), "k2": float(wb.get("k2", 0))} if wb else {})})
        v = p.get("vignette")
        vv = {"enabled": v is not None}
        if v:
            vv["mode"] = getattr(unreal.DynamicLensLayerMode, v.get("mode", "Physical").upper())
            for src, dst in [("natural_falloff", "natural_falloff"), ("mechanical_strength", "mechanical_strength"), ("at_wide", "at_wide"), ("at_long", "at_long"),
                             ("wide_mm", "wide_mm"), ("long_mm", "long_mm"), ("fstop_open", "f_stop_open"), ("fstop_closed", "f_stop_closed"), ("stopdown_fade", "stop_down_fade")]:
                if src in v:
                    vv[dst] = float(v[src])
        _set_struct(asset, "vignette", vv)
        b = p.get("bokeh")
        bb = {"enabled": b is not None}
        if b:
            bb["mode"] = getattr(unreal.DynamicLensLayerMode, b.get("mode", "Physical").upper())
            for src, dst, conv in [("cats_eye_strength", "cats_eye_strength", float), ("blades", "blades", int), ("barrel_radius", "barrel_radius_mm", float), ("barrel_length", "barrel_length_mm", float),
                                   ("petzval", "petzval", float), ("petzval_falloff", "petzval_falloff", float), ("swirl_fades_by_fstop", "swirl_fades_by_f_stop", float), ("swirl_exclusion_radius", "swirl_exclusion_radius", float)]:
                if src in b:
                    bb[dst] = conv(b[src])
            if "swirl_exclusion_box" in b:
                bb["swirl_exclusion_box"] = unreal.Vector2D(*b["swirl_exclusion_box"])
        _set_struct(asset, "bokeh", bb)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{PRESET_PKG}/DL_{name}")
        _log(f"preset DL_{name} ({p.get('label','')})")
    return created


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def add_to_all_cameras(preset="DL_Master", overwrite=False):
    asset = unreal.load_asset(f"{PRESET_PKG}/{preset}") if "/" not in preset else unreal.load_asset(preset)
    if asset is None:
        raise RuntimeError(f"preset {preset} not found")
    n = unreal.DynamicLensLibrary.add_to_all_cine_cameras(_world(), asset, overwrite)
    _log(f"added Dynamic Lens ({preset}) to {n} camera(s)")
    return n


def remove_from_all_cameras():
    n = unreal.DynamicLensLibrary.remove_from_all_cine_cameras(_world())
    _log(f"removed Dynamic Lens from {n} camera(s)")
    return n


def status():
    out = []
    for a in unreal.GameplayStatics.get_all_actors_of_class(_world(), unreal.CineCameraActor):
        for c in a.get_components_by_class(unreal.DynamicLensComponent):
            out.append({"camera": a.get_actor_label(), "enabled": c.get_editor_property("enabled"),
                        "preset": c.get_editor_property("preset").get_name() if c.get_editor_property("preset") else None,
                        "focal": c.get_editor_property("last_focal_mm"), "focus_cm": c.get_editor_property("last_focus_cm"),
                        "fstop": c.get_editor_property("last_f_stop"), "overscan": c.get_editor_property("last_overscan_factor"),
                        "K1": c.get_editor_property("last_params").k1, "vignette": c.get_editor_property("last_vignette")})
    return out
