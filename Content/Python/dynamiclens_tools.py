"""DynamicLens editor tools (auto on sys.path because this is the plugin's Content/Python folder).

    import dynamiclens_tools as dl
    dl.import_profiles()          # Tools/data/profiles/*.json  -> /DynamicLens/Profiles/DLP_<name>
    dl.import_presets()           # Tools/data/presets.json     -> /DynamicLens/Presets/DL_<name>
    dl.add_to_all_cameras("DL_Master")   # add a Dynamic Lens component to every CineCameraActor in the level
    dl.remove_from_all_cameras()
    dl.status()                   # what every Dynamic Lens component in the level is doing right now
"""
import json, os, re
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
            _apply_specs(asset, specs)
        unreal.DynamicLensLibrary.refresh_profile(asset)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{PROFILE_PKG}/{name}")
        _log(f"profile {name}: {len(j['focals'])} focals x {len(j['focus_cm'])} focus steps")
    return created


def _apply_specs(asset, specs):
    """physical / native-format fields shared by every profile type (see presets.json profile_specs)."""
    for k, conv in [("front_diameter_mm", float), ("iris_blades", int), ("max_aperture", float), ("pupil_visible_at_image_circle", float),
                    ("image_circle_mm", float), ("squeeze", float)]:
        if k in specs:
            asset.set_editor_property(k, conv(specs[k]))
    if "native_sensor_mm" in specs:
        asset.set_editor_property("native_sensor_mm", unreal.Vector2D(*specs["native_sensor_mm"]))
    if "source" in specs:
        asset.set_editor_property("source", specs["source"])
    if "label" in specs:
        asset.set_editor_property("label", specs["label"])


def import_projection_profiles(preset_file=None, save=True):
    """presets.json projection_profiles -> UDynamicLensProfile assets of type Projection (ideal fisheye maths)."""
    preset_file = preset_file or os.path.join(DATA_DIR, "presets.json")
    profs = json.load(open(preset_file)).get("projection_profiles", {})
    created = []
    for name, spec in profs.items():
        asset = _create_data_asset("DLP_" + name, PROFILE_PKG, unreal.DynamicLensProfile)
        asset.set_editor_property("type", unreal.DynamicLensProfileType.PROJECTION)
        asset.set_editor_property("projection", getattr(unreal.DynamicLensProjection, spec.get("projection", "Equidistant").upper()))
        asset.set_editor_property("max_field_angle_deg", float(spec.get("max_field_angle_deg", 90.0)))
        _apply_specs(asset, spec)
        unreal.DynamicLensLibrary.refresh_profile(asset)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{PROFILE_PKG}/DLP_{name}")
        _log(f"projection profile DLP_{name}: {asset.get_editor_property('coverage')}")
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
        asset.set_editor_property("image_circle", bool(p.get("image_circle", True)))
        asset.set_editor_property("image_circle_softness", float(p.get("image_circle_softness", 0.05)))
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


# --------------------------------------------------------------------------------------- tiedtke ST maps

TIEDTKE_ROOT = "/Game/CinematicTemplate/Lenses"
TIEDTKE_PKG = PROFILE_PKG + "/Tiedtke"


def import_tiedtke(root=TIEDTKE_ROOT, save=True, series_filter=None):
    """tiedtke's Lens Files (ST maps, one per prime) -> one ST-map profile per lens series + a preset each.

    Textures are duplicated into the plugin so the profiles are self-contained; the original files stay untouched.
    Squeeze comes from the folder name (1_5x / 1_8x / 2x).
    """
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    files, textures = {}, {}
    for ad in ar.get_assets_by_path(root, recursive=True):
        pkg = str(ad.package_name); cls = str(ad.asset_class_path.asset_name)
        parts = pkg[len(root) + 1:].split("/")
        if len(parts) < 3:
            continue
        sq_folder, series = parts[0], parts[1]
        m = re.match(r"(\d+)_?(\d*)x", sq_folder)
        if not m:
            continue
        squeeze = float(m.group(1) + ("." + m.group(2) if m.group(2) else ""))
        if cls == "LensFile":
            fm = re.search(r"_(\d+)mm", parts[-1]) or re.search(r"(\d+)mm", parts[-1])
            if fm:
                files.setdefault((series, squeeze), []).append((float(fm.group(1)), pkg))
        elif cls == "Texture2D" and "Textures" in parts:
            fm = re.search(r"_(\d+)mm", parts[-1])
            if fm:
                textures.setdefault(series, {})[float(fm.group(1))] = pkg
    created = []
    for (series, squeeze), lenses in sorted(files.items()):
        if series_filter and series not in series_filter:
            continue
        name = re.sub(r"_?\d+(_\d+)?x$", "", series)
        prof_name = "DLP_T_" + name
        prof = _create_data_asset(prof_name, TIEDTKE_PKG, unreal.DynamicLensProfile)
        prof.set_editor_property("st_maps", [])
        n = 0
        for focal, pkg in sorted(lenses):
            lf = unreal.load_asset(pkg)
            tex_pkg = textures.get(series, {}).get(focal)
            if lf is None or tex_pkg is None:
                _log(f"  skip {pkg}: lens file or texture missing"); continue
            dst = f"{TIEDTKE_PKG}/Textures/{name}_{int(focal)}mm"
            if not unreal.EditorAssetLibrary.does_asset_exist(dst):
                unreal.EditorAssetLibrary.duplicate_asset(tex_pkg, dst)
            tex = unreal.load_asset(dst)
            if unreal.DynamicLensLibrary.add_st_map_from_lens_file(prof, lf, focal, tex, squeeze):
                n += 1
        prof.set_editor_property("label", f"{name.replace('_', ' ')} {squeeze:g}x anamorphic (tiedtke ST maps)")
        prof.set_editor_property("source", "Real lens grids shot on an ARRI Mini, converted to ST maps in Nuke by tiedtke (https://tiedtke.gumroad.com/l/realcinemalenses, v002). "
                                           "Native frame 46 x 18.66 mm desqueezed (2.39:1). One map per prime, single focus. Physical specs are placeholders.")
        prof.set_editor_property("iris_blades", 11)
        prof.set_editor_property("front_diameter_mm", 110.0)
        prof.set_editor_property("max_aperture", 2.8)
        prof.set_editor_property("pupil_visible_at_image_circle", 0.85)
        unreal.DynamicLensLibrary.refresh_profile(prof)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(prof)
        preset = _create_data_asset("DL_T_" + name, PRESET_PKG + "/Tiedtke", unreal.DynamicLensPreset)
        preset.set_editor_property("profile", prof)
        preset.set_editor_property("description", f"tiedtke {name.replace('_', ' ')} {squeeze:g}x anamorphic ST maps, exact at the measured focal lengths (nearest is used). Use Match Camera To Profile for the native 2.39 frame.")
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(preset)
        created.append((prof_name, n))
        _log(f"tiedtke {prof_name}: {n} maps, squeeze {squeeze:g}")
    return created


# --------------------------------------------------------------------------------------- image circle material

MATERIAL_PKG = "/DynamicLens/Materials"


def build_image_circle_material(save=True, force=False):
    """Creates /DynamicLens/Materials/M_DL_ImageCircle: a post-process mask (after tonemapping) that blacks out the frame beyond a
    centred circle. Parameters: Radius (1 = half the frame width), Softness (fraction of the radius), Aspect (W/H)."""
    path = f"{MATERIAL_PKG}/M_DL_ImageCircle"
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not force:
        return unreal.load_asset(path)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_DL_ImageCircle", MATERIAL_PKG, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    mat.set_editor_property("blendable_priority", 10)
    mel = unreal.MaterialEditingLibrary
    scene = mel.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, -700, -200)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    uv = mel.create_material_expression(mat, unreal.MaterialExpressionScreenPosition, -700, 0)
    params = {}
    for i, (nm, default) in enumerate([("Radius", 0.9), ("Softness", 0.05), ("Aspect", 1.7778)]):
        pnode = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 150 + 100 * i)
        pnode.set_editor_property("parameter_name", nm)
        pnode.set_editor_property("default_value", default)
        params[nm] = pnode
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property("code", "float2 p = float2((UV.x - 0.5) * 2.0, (UV.y - 0.5) * 2.0 / max(Aspect, 0.01));\n"
                                       "float r = length(p);\n"
                                       "float inner = Radius * (1.0 - saturate(Softness));\n"
                                       "float m = 1.0 - smoothstep(inner, max(Radius, inner + 1e-4), r);\n"
                                       "return Scene * m;")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "ImageCircle")
    inputs = []
    for nm in ("Scene", "UV", "Radius", "Softness", "Aspect"):
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", nm); inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    mel.connect_material_expressions(scene, "Color", custom, "Scene")
    mel.connect_material_expressions(uv, "", custom, "UV")
    for nm in ("Radius", "Softness", "Aspect"):
        mel.connect_material_expressions(params[nm], "", custom, nm)
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    if save:
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
    _log("built " + path)
    return mat


def import_all(tiedtke=True):
    build_image_circle_material()
    import_profiles()
    import_projection_profiles()
    import_presets()
    if tiedtke:
        import_tiedtke()
