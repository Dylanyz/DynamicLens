# Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
# Third-party lens data under Content/Profiles/Tiedtke and Tools/data/raw is NOT covered; see NOTICE.

"""DynamicLens editor tools (auto on sys.path because this is the plugin's Content/Python folder).

    import dynamiclens_tools as dl
    dl.import_profiles()          # Tools/data/profiles/*.json  -> /DynamicLens/Profiles/DLP_<name>
    dl.import_presets()           # Tools/data/presets.json     -> /DynamicLens/Presets/DL_<name>
    dl.import_andy_stmaps()       # Andy Davis spherical ST maps -> /DynamicLens/Profiles/AndyDavis
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
    for k, conv in [("front_diameter_mm", float), ("iris_blades", int), ("blade_curvature", float), ("max_aperture", float), ("pupil_visible_at_image_circle", float),
                    ("image_circle_mm", float), ("squeeze", float), ("nominal_focal_mm", float)]:
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


def import_presets(preset_file=None, save=True, only=None):
    """Tools/data/presets.json -> UDynamicLensPreset assets referencing the profile assets."""
    preset_file = preset_file or os.path.join(DATA_DIR, "presets.json")
    presets = json.load(open(preset_file))["presets"]
    created = []
    for name, p in presets.items():
        if only and name not in only:
            continue
        asset = _create_data_asset("DL_" + name, PRESET_PKG, unreal.DynamicLensPreset)
        prof = unreal.load_asset(f"{PROFILE_PKG}/DLP_{p['profile']}")
        if prof is None:
            raise RuntimeError(f"profile DLP_{p['profile']} missing; run import_profiles() first")
        asset.set_editor_property("description", p.get("label", name))
        wb = p.get("wide_boost")
        dd = {"profile": prof, "amount": float(p.get("amount", 1.0)), "breathing": float(p.get("breathing", 1.0)),
              "out_of_range": getattr(unreal.DynamicLensRangeMode, {"Clamp": "CLAMP", "Extrapolate": "EXTRAPOLATE", "ClampRaw": "CLAMP_RAW"}[p.get("out_of_range", "Clamp")]),
              "lock_focal_length": bool(p.get("lock_focal", False))}
        _set_struct(asset, "distortion", dd)
        d_s = asset.get_editor_property("distortion")
        w = d_s.get_editor_property("wide_boost")
        for k, v in {"enabled": wb is not None, **({"below_mm": float(wb["below_mm"]), "full_mm": float(wb["full_mm"]), "k1": float(wb.get("k1", 0)), "k2": float(wb.get("k2", 0))} if wb else {})}.items():
            w.set_editor_property(k, v)
        d_s.set_editor_property("wide_boost", w); asset.set_editor_property("distortion", d_s)
        e = p.get("image_circle_edge") or {}
        ee = {}
        for src, dst in [("falloff_power", "falloff_power"), ("opacity", "opacity"), ("ellipticity", "ellipticity"), ("wobble", "wobble"),
                         ("wobble_seed", "wobble_seed"), ("edge_noise", "edge_noise"), ("noise_scale", "noise_scale"),
                         ("chromatic_red", "chromatic_red"), ("chromatic_green", "chromatic_green"), ("chromatic_blue", "chromatic_blue"),
                         ("falloff_wobble", "falloff_wobble"), ("falloff_wobble_seed", "falloff_wobble_seed"),
                         ("noise_depth", "noise_depth"), ("noise_blur", "noise_blur"), ("noise_detail", "noise_detail"),
                         ("scatter", "scatter"), ("mask_strength", "mask_strength"),
                         ("fade_reach", "fade_reach"), ("fade_amount", "fade_amount"), ("fade_curve", "fade_curve"),
                         ("noise_stretch", "noise_stretch"), ("noise_seed", "noise_seed"), ("noise_contrast", "noise_contrast")]:
            if src in e:
                ee[dst] = float(e[src])
        if "chromatic_aberration" in e:   # legacy scalar: red in, blue out
            ee["chromatic_red"] = -float(e["chromatic_aberration"]); ee["chromatic_blue"] = float(e["chromatic_aberration"])
        if "chromatic_amount" in e:
            ee["chromatic_amount"] = float(e["chromatic_amount"])
        elif any(k in ee for k in ("chromatic_red", "chromatic_green", "chromatic_blue")):
            ee["chromatic_amount"] = 1.0   # explicit channel offsets: use them as they are
        for src in ("wobble_lobes", "falloff_wobble_lobes"):
            if src in e:
                ee[src] = int(e[src])
        if "center_offset" in e:
            ee["center_offset"] = unreal.Vector2D(*e["center_offset"])
        ic = asset.get_editor_property("image_circle")
        ic.set_editor_property("enabled", bool(p.get("image_circle", True)))
        ic.set_editor_property("softness", float(p.get("image_circle_softness", 0.05)))
        ic.set_editor_property("scale", float(p.get("image_circle_scale", 1.0)))
        edge = ic.get_editor_property("edge")
        for k, v in ee.items():
            edge.set_editor_property(k, v)
        ic.set_editor_property("edge", edge); asset.set_editor_property("image_circle", ic)
        o = p.get("overscan") or {}
        oo = {"mode": getattr(unreal.DynamicLensOverscanMode, o.get("mode", "Dynamic").upper())}
        if "max" in o: oo["max_overscan"] = float(o["max"])
        if "fixed" in o: oo["fixed_overscan"] = float(o["fixed"])
        if "scale_resolution" in o: oo["scale_resolution_with_overscan"] = bool(o["scale_resolution"])
        if "step" in o: oo["dynamic_step"] = float(o["step"])
        _set_struct(asset, "overscan", oo)
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
            for src, dst, conv in [("blade_rotation_deg", "blade_rotation_deg", float), ("spherical_aberration", "spherical_aberration", float), ("coma", "coma", float),
                                   ("squeeze", "squeeze", float), ("drive_accumulation_dof", "drive_accumulation_dof", bool)]:
                if src in b:
                    bb[dst] = conv(b[src])
            for src, dst in [("blade_source", "blade_source"), ("squeeze_source", "squeeze_source")]:
                if src in b:
                    bb[dst] = getattr(unreal.DynamicLensValueSource, b[src].upper())
            if "blade_curvature" in b:
                bb["override_blade_curvature"] = True
                bb["blade_curvature"] = float(b["blade_curvature"])
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

# tiedtke's Lens Files ship with the plugin (he gave permission), so the import works from a
# clean clone. Only the LensFile assets are shipped, not his 200 MB of source textures: the
# textures we actually use are already imported under Textures/, and _tiedtke_texture() falls
# back to those. Point TIEDTKE_ROOT at his pack in a project to re-import from the originals.
TIEDTKE_ROOT = "/DynamicLens/Profiles/Tiedtke/Source"
TIEDTKE_ROOT_PACK = "/Game/CinematicTemplate/Lenses"
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
            # A zoom is named for its RANGE ("AngenieuxOptimo_44-440mm"), so a plain focal search
            # picks up 440 and then never matches its map, which is shot at 50 mm. Strip the range
            # first and let the pairing below supply the real focal from the texture.
            bare = re.sub(r"\d+-\d+\s*mm", "", parts[-1])
            fm = re.search(r"_(\d+)mm", bare) or re.search(r"(\d+)mm", bare)
            files.setdefault((series, squeeze), []).append((float(fm.group(1)) if fm else None, pkg))
        elif cls == "Texture2D" and "Textures" in parts:
            fm = re.search(r"_(\d+)mm", parts[-1])
            if fm:
                textures.setdefault(series, {})[float(fm.group(1))] = pkg
    created = []
    for (series, squeeze), lenses in sorted(files.items(), key=lambda kv: kv[0]):
        if series_filter and series not in series_filter:
            continue
        name = re.sub(r"_?\d+(_\d+)?x$", "", series)
        zoom = re.search(r"(\d+)-(\d+)\s*mm", series)
        tex_by_focal = textures.get(series, {})
        # One Lens File whose focal we could not read, and exactly one texture: that is a zoom,
        # and the texture name carries the focal the single map was actually shot at.
        if len(lenses) == 1 and lenses[0][0] is None and len(tex_by_focal) == 1:
            lenses = [(next(iter(tex_by_focal)), lenses[0][1])]
        prof_name = "DLP_T_" + name
        prof = _create_data_asset(prof_name, TIEDTKE_PKG, unreal.DynamicLensProfile)
        # keep what is already on the profile: it is the fallback when only the Lens Files ship
        prior_maps = {e.get_editor_property("focal_mm"): e.get_editor_property("map")
                      for e in prof.get_editor_property("st_maps")}
        # ...including for a zoom whose focal we could not read and whose source texture is absent:
        # the map already imported is the one tiedtke shipped, so take its focal
        if len(lenses) == 1 and lenses[0][0] is None and len(prior_maps) == 1:
            lenses = [(next(iter(prior_maps)), lenses[0][1])]
        lenses = [(f, pkg) for f, pkg in lenses if f is not None]
        if not lenses:
            _log(f"  skip {series}: could not pair any Lens File with a map"); continue
        prof.set_editor_property("st_maps", [])
        n = 0
        for focal, pkg in sorted(lenses):
            lf = unreal.load_asset(pkg)
            tex_pkg = textures.get(series, {}).get(focal)
            if tex_pkg is None:
                # Shipping only the Lens Files means there is no source texture to duplicate. The
                # texture already on the existing profile IS the right map, so reuse it by identity
                # rather than by guessing a name - the zooms (Angenieux Optimo, PS-Technik) do not
                # follow the <series>_<focal>mm convention and a name guess silently empties them.
                tex = prior_maps.get(focal)
                if tex is not None:
                    if unreal.DynamicLensLibrary.add_st_map_from_lens_file(prof, lf, focal, tex, squeeze):
                        n += 1
                    continue
                _log(f"  skip {pkg}: no source texture and none already imported"); continue
            if lf is None or tex_pkg is None:
                _log(f"  skip {pkg}: lens file or texture missing"); continue
            dst = f"{TIEDTKE_PKG}/Textures/{name}_{int(focal)}mm"
            if not unreal.EditorAssetLibrary.does_asset_exist(dst):
                unreal.EditorAssetLibrary.duplicate_asset(tex_pkg, dst)
                if save:
                    unreal.EditorAssetLibrary.save_asset(dst)   # a duplicate only lives in memory until saved
            tex = unreal.load_asset(dst)
            if unreal.DynamicLensLibrary.add_st_map_from_lens_file(prof, lf, focal, tex, squeeze):
                n += 1
        prof.set_editor_property("label", f"{name.replace('_', ' ')} {squeeze:g}x anamorphic (tiedtke ST maps)")
        src = ("Real lens grids shot on an ARRI Mini, converted to ST maps in Nuke by tiedtke "
               "(https://tiedtke.gumroad.com/l/realcinemalenses, v002). Native frame 46 x 18.66 mm "
               "desqueezed (2.39:1). Physical specs are placeholders.")
        if zoom:
            lo, hi = int(zoom.group(1)), int(zoom.group(2))
            # A zoom, and tiedtke shipped one map for the whole range. Leave NominalFocalMm at 0 so
            # the camera is free to zoom; the single map's distortion is applied across the range.
            prof.set_editor_property("nominal_focal_mm", 0.0)
            shot = ", ".join(str(int(f)) for f, _ in lenses)
            src += (f" ZOOM {lo}-{hi} mm: the focal length is NOT locked, so the camera zooms "
                    f"freely across the range. Only {shot} mm is measured, and that one map's "
                    f"distortion is applied at every focal length - a real zoom's distortion "
                    f"changes across its range, so treat the ends as approximate.")
        else:
            src += " One map per prime, single focus."
        prof.set_editor_property("source", src)
        prof.set_editor_property("iris_blades", 11)
        prof.set_editor_property("front_diameter_mm", 110.0)
        prof.set_editor_property("max_aperture", 2.8)
        prof.set_editor_property("pupil_visible_at_image_circle", 0.85)
        unreal.DynamicLensLibrary.refresh_profile(prof)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(prof)
        preset = _create_data_asset("DL_T_" + name, PRESET_PKG + "/Tiedtke", unreal.DynamicLensPreset)
        # primes lock to their one focal length; zooms must stay free to zoom their range
        _set_struct(preset, "distortion", {"profile": prof, "lock_focal_length": not zoom})
        preset.set_editor_property("description", f"tiedtke {name.replace('_', ' ')} {squeeze:g}x anamorphic ST maps, exact at the measured focal lengths (nearest is used). Use Match Camera To Profile for the native 2.39 frame.")
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(preset)
        created.append((prof_name, n))
        _log(f"tiedtke {prof_name}: {n} maps, squeeze {squeeze:g}")
    return created



# --------------------------------------------------------------------------------------- Andy Davis creative lens maps

# the maps ship with the plugin, so the import works from a clean clone
ANDY_STMAP_DIR = os.path.join(DATA_DIR, "stmaps", "andy_spherical")
ANDY_PKG = PROFILE_PKG + "/AndyDavis"
ANDY_PRESET_PKG = PRESET_PKG + "/AndyDavis"


def _andy_overscan(tex, n=64):
    """Overscan the map needs, using the engine's own sampler.

    This mirrors AddSTMapFromLensFile exactly and must stay that way: computing it offline from the
    source EXR instead gives subtly different numbers (measured 1.1159 vs the correct 1.1250 on
    Atlas Orion 50 mm), because the engine samples the texture source on its own grid and does not
    flip V.
    """
    uv = unreal.DynamicLensLibrary.read_st_map_samples(tex, n, n)
    if not uv or len(uv) < n * n * 2:
        return 1.0
    over = 1.0
    for j in range(n):
        for i in range(n):
            if i not in (0, n - 1) and j not in (0, n - 1):
                continue
            u = (i + 0.5) / n
            v = (j + 0.5) / n
            su = uv[2 * (j * n + i)]
            sv = uv[2 * (j * n + i) + 1]
            if abs(u - 0.5) > 0.01:
                over = max(over, abs(su - 0.5) / abs(u - 0.5))
            if abs(v - 0.5) > 0.01:
                over = max(over, abs(sv - 0.5) / abs(v - 0.5))
    return min(max(over, 1.0), 2.0)


def _import_texture(exr_path, dst_pkg, name, save=True):
    """EXR -> Texture2D with the settings the ST-map path needs (matches the tiedtke textures)."""
    dst = f"{dst_pkg}/{name}"
    if not unreal.EditorAssetLibrary.does_asset_exist(dst):
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", exr_path)
        task.set_editor_property("destination_path", dst_pkg)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(dst)
    if tex is None:
        return None
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    if save:
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def import_andy_stmaps(root=None, save=True, series_filter=None):
    """Andy Davis's creative lens maps (spherical) -> one ST-map profile + preset per lens series.

    Defaults to the maps shipped in Tools/data/stmaps/andy_spherical, so a clean clone can rebuild
    every profile with no extra downloads. Andy Davis gave permission to redistribute them; they
    remain his, under his terms, not Apache-2.0 - see NOTICE and SOURCES.md. Pass `root` (or set
    $DYNAMICLENS_ANDY_DIR) to import from a different set, e.g. maps you re-prepared at full
    resolution with Tools/prep_andy_stmaps.py.

    Geometry (focal lengths, sensor, overscan) is measured and comes from the manifest and the
    textures. The editorial and physical layer comes from presets.json `andy_stmap_sets`.
    """
    root = root or os.environ.get("DYNAMICLENS_ANDY_DIR") or ANDY_STMAP_DIR
    if not os.path.isfile(os.path.join(root, "manifest.json")):
        raise RuntimeError(f"no manifest.json under {root}; pass root=<folder with manifest.json>, "
                           "set DYNAMICLENS_ANDY_DIR, or regenerate with Tools/prep_andy_stmaps.py")
    manifest = json.load(open(os.path.join(root, "manifest.json")))
    cfg_all = json.load(open(os.path.join(DATA_DIR, "presets.json"))).get("andy_stmap_sets", {}).get("sets", {})
    created = []
    for series, data in sorted(manifest["sets"].items()):
        if series_filter and series not in series_filter:
            continue
        cfg = cfg_all.get(series)
        if not cfg:
            _log(f"  skip {series}: no entry in presets.json andy_stmap_sets")
            continue
        name = cfg["name"]
        sensor = data["sensor_mm"]
        # A profile carries ONE NativeSensorMm, so every map in it must come off the same gate.
        # ARRI Signature is the one mixed set (29 mm is a 3840x2160 crop, the rest are 4448x3096);
        # an ST map shot on a crop cannot be restretched to the bigger sensor, because outside the
        # crop there is simply no data. Keep the majority gate and say plainly what was dropped.
        gate = tuple(data.get("gate_px", []))
        lenses_in = [l for l in data["lenses"] if tuple(l["src_dims"]) == gate]
        for l in data["lenses"]:
            if tuple(l["src_dims"]) != gate:
                _log(f"  {series} {l['focal_mm']}mm: skipped, gate {l['src_dims']} != series gate {list(gate)}")
        prof = _create_data_asset("DLP_AD_" + name, ANDY_PKG, unreal.DynamicLensProfile)
        entries = []
        for lens in lenses_in:
            exr = os.path.join(root, lens["file"])
            if not os.path.isfile(exr):
                _log(f"  skip {lens['file']}: missing")
                continue
            tex_name = f"{name}_{lens['focal_mm']}mm" + (f"_{lens['variant']}" if lens.get("variant") else "")
            tex = _import_texture(exr, ANDY_PKG + "/Textures", tex_name, save=save)
            if tex is None:
                _log(f"  skip {tex_name}: texture import failed")
                continue
            e = unreal.DynamicLensSTMapEntry()
            e.set_editor_property("focal_mm", float(lens["focal_mm"]))
            e.set_editor_property("focus_cm", 0.0)
            e.set_editor_property("map", tex)
            # MapFormat must be set field by field. Python prints the struct as "{}" because its
            # fields are not Blueprint-visible, so the defaults look harmless. They are not, and
            # BOTH of these render a broken image:
            #   PixelOrigin defaults to TopLeft; DriveSTMap passes it to BuildExtendedSTMap, which
            #     then flips V -> black / wildly zoomed frame.
            #   DistortionChannels defaults to BA; these maps carry the field in RG with B=0 and no
            #     alpha, so the distortion pass reads zeros -> washed-out, zoomed, wrong colour.
            # tiedtke's entries come from real Lens Files and carry BottomLeft + RG/RG. Match them.
            fmt = unreal.CalibratedMapFormat()
            fmt.set_editor_property("pixel_origin", unreal.CalibratedMapPixelOrigin.BOTTOM_LEFT)
            fmt.set_editor_property("undistortion_channels", unreal.CalibratedMapChannels.RG)
            fmt.set_editor_property("distortion_channels", unreal.CalibratedMapChannels.RG)
            e.set_editor_property("map_format", fmt)
            e.set_editor_property("needed_overscan", _andy_overscan(tex))
            entries.append(e)
        if not entries:
            _log(f"  skip {series}: no maps")
            continue
        entries.sort(key=lambda e: e.get_editor_property("focal_mm"))
        prof.set_editor_property("type", unreal.DynamicLensProfileType.ST_MAP)
        prof.set_editor_property("native_sensor_mm", unreal.Vector2D(sensor[0], sensor[1]))
        prof.set_editor_property("squeeze", float(data.get("squeeze", 1.0)))
        prof.set_editor_property("st_maps", entries)
        # The grid covers the whole gate, so the lens's circle is at least the gate diagonal.
        # A measured floor, not a data-sheet number - same convention as the AD_* parametric profiles.
        prof.set_editor_property("image_circle_mm", round((sensor[0] ** 2 + sensor[1] ** 2) ** 0.5, 2))
        _apply_specs(prof, cfg)
        prof.set_editor_property("label", cfg["label"])
        gate = data.get("gate_px", [0, 0])
        prof.set_editor_property("source",
            f"Distortion: Andy Davis (Imagery for Media) creative lens maps, distort ST maps at "
            f"{gate[0]}x{gate[1]}, downsampled 2x for import (measured worst-case error 0.0045 source px). "
            f"Free release, see https://imag4media.com/vfx-rnd/ and SOURCES.md. "
            f"Sensor {sensor[0]} x {sensor[1]} mm derived from the gate at the 8.25 um ARRI pitch"
            + (" (ASSUMED: gate not recognised)" if data.get("sensor_assumed") else "")
            + ". One map per prime, SINGLE FOCUS - these do not breathe. "
              "Image circle = the gate diagonal, a measured floor not a data-sheet value. "
              "Front diameter, iris blades, blade curvature and pupil visibility are PLACEHOLDERS, not measured.")
        unreal.DynamicLensLibrary.refresh_profile(prof)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(prof)
        preset = _create_data_asset("DL_AD_" + name, ANDY_PRESET_PKG, unreal.DynamicLensPreset)
        _set_struct(preset, "distortion", {"profile": prof, "lock_focal_length": True})
        focals = ", ".join(str(int(e.get_editor_property("focal_mm"))) for e in entries)
        preset.set_editor_property("description",
            f"{cfg['label']}. {cfg.get('note', '')} Measured ST maps at {focals} mm "
            f"(the nearest is used). Single focus, so no breathing.")
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(preset)
        created.append(("DLP_AD_" + name, len(entries)))
        _log(f"andy DLP_AD_{name}: {len(entries)} maps, sensor {sensor[0]}x{sensor[1]}")
    _log(f"andy: {len(created)} profiles, {sum(n for _, n in created)} maps")
    return created

# --------------------------------------------------------------------------------------- image circle material

MATERIAL_PKG = "/DynamicLens/Materials"


def build_image_circle_material(save=True, force=False):
    """Creates /DynamicLens/Materials/M_DL_ImageCircle: a post-process mask (after tonemapping) that darkens the frame beyond the
    lens's image circle with the imperfections of a real edge (FDynamicLensImageCircleEdge). Scalar parameters:
    Radius (1 = half the frame width), Softness (fraction of the radius), Aspect (W/H), FalloffPower, Opacity, CenterX/CenterY
    (fraction of half frame), Ellipticity, Wobble/WobbleLobes/WobbleSeed, EdgeNoise/NoiseScale, ChromaticAberration, Scatter,
    MaskStrength; texture parameter Mask (full-frame multiplier, default white)."""
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
    scalar_defaults = [("Radius", 0.9), ("Softness", 0.05), ("Aspect", 1.7778), ("FalloffPower", 1.0), ("Opacity", 1.0),
                       ("CenterX", 0.0), ("CenterY", 0.0), ("Ellipticity", 1.0), ("Wobble", 0.0), ("WobbleLobes", 3.0), ("WobbleSeed", 0.0),
                       ("EdgeNoise", 0.0), ("NoiseScale", 96.0), ("NoiseDepth", 0.3), ("NoiseBlur", 0.0), ("NoiseDetail", 0.5),
                       ("SoftWobble", 0.0), ("SoftWobbleLobes", 3.0), ("SoftWobbleSeed", 0.0),
                       ("CAR", 0.0), ("CAG", 0.0), ("CAB", 0.0), ("Scatter", 0.0), ("MaskStrength", 0.0),
                       ("FadeReach", 0.0), ("FadeAmount", 0.0), ("FadeCurve", 1.0),
                       ("NoiseStretch", 1.0), ("NoiseSeed", 0.0), ("NoiseContrast", 0.0), ("Squareness", 2.0)]
    params = {}
    for i, (nm, default) in enumerate(scalar_defaults):
        pnode = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 150 + 70 * i)
        pnode.set_editor_property("parameter_name", nm)
        pnode.set_editor_property("default_value", default)
        params[nm] = pnode
    mask = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -700, 150 + 70 * len(scalar_defaults))
    mask.set_editor_property("parameter_name", "Mask")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if white:
        mask.set_editor_property("texture", white)
    custom = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property("code", IMAGE_CIRCLE_HLSL)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "ImageCircle")
    names = ["Scene", "UV"] + [nm for nm, _ in scalar_defaults] + ["Mask"]
    inputs = []
    for nm in names:
        ci = unreal.CustomInput(); ci.set_editor_property("input_name", nm); inputs.append(ci)
    custom.set_editor_property("inputs", inputs)
    mel.connect_material_expressions(scene, "Color", custom, "Scene")
    mel.connect_material_expressions(uv, "", custom, "UV")
    for nm, _ in scalar_defaults:
        mel.connect_material_expressions(params[nm], "", custom, nm)
    mel.connect_material_expressions(mask, "", custom, "Mask")
    mel.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    if save:
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
    _log("built " + path)
    return mat


# HLSL of the image-circle Custom node. Normalized frame coordinates: x = -1..1 across the width, y scaled by the aspect.
# Falloff band = Radius*(1-Softness) .. Radius; the mask, the scatter blur and the per-channel radius all live in that band.
IMAGE_CIRCLE_HLSL = """
float asp = max(Aspect, 0.01);
float2 p = float2((UV.x - 0.5) * 2.0 - CenterX, ((UV.y - 0.5) * 2.0 - CenterY) / asp);
p.y /= max(Ellipticity, 0.01);
// superellipse radius: 2 = ellipse, higher = rounded rectangle (the shape of what the render can show)
float nS = clamp(Squareness, 1.5, 64.0);
float r = (nS < 2.01) ? length(p) : pow(pow(abs(p.x), nS) + pow(abs(p.y), nS), 1.0 / nS);
float th = atan2(p.y, p.x);
// waviness of the radius (two harmonics so it does not look like a gear)
float wob = 1.0 + Wobble * (0.7 * sin(WobbleLobes * th + WobbleSeed) + 0.3 * sin((2.0 * WobbleLobes + 1.0) * th + 2.3 * WobbleSeed));
float R = max(Radius * wob, 1e-3);
// waviness of the falloff width: the soft band gets wider and narrower around the circle
float sw = 1.0 + SoftWobble * (0.7 * sin(SoftWobbleLobes * th + SoftWobbleSeed) + 0.3 * sin((2.0 * SoftWobbleLobes + 1.0) * th + 1.7 * SoftWobbleSeed));
float soft = saturate(Softness * max(sw, 0.0));
float band = max(R * soft, 1e-4);
// breakup: value noise in polar space, periodic around the rim (whole cell counts), two octaves (second weighted by
// NoiseDetail), blurred by averaging neighbours (NoiseBlur), pushed towards blobs (NoiseContrast), fading in from
// NoiseDepth inside the circle to full strength at the black edge. Displacement is a fraction of the radius.
float n = 0.0;
if (EdgeNoise > 0.0001)
{
    float cells = max(round(NoiseScale), 1.0);
    float2 q0 = float2((th / 6.2831853 + 0.5) * cells, r * cells / 6.2831853 * max(NoiseStretch, 0.01) + NoiseSeed * 7.31);
    float bl = NoiseBlur * 0.75;
    float acc = 0.0;
    [unroll] for (int t = 0; t < 5; ++t)
    {
        float2 off = (t == 0) ? float2(0, 0) : (t == 1) ? float2(bl, 0) : (t == 2) ? float2(-bl, 0) : (t == 3) ? float2(0, bl) : float2(0, -bl);
        float2 q = q0 + off; float amp = 0.65; float nn = 0.0; float per = cells;
        [unroll] for (int o = 0; o < 2; ++o)
        {
            float2 qi = floor(q), qf = frac(q); qf = qf * qf * (3.0 - 2.0 * qf);
            // wrap the angular axis so the pattern is seamless around the rim
            float x0 = fmod(qi.x + per, per), x1 = fmod(qi.x + 1.0 + per, per);
            float h00 = frac(sin(dot(float2(x0, qi.y), float2(127.1, 311.7))) * 43758.5453);
            float h10 = frac(sin(dot(float2(x1, qi.y), float2(127.1, 311.7))) * 43758.5453);
            float h01 = frac(sin(dot(float2(x0, qi.y + 1.0), float2(127.1, 311.7))) * 43758.5453);
            float h11 = frac(sin(dot(float2(x1, qi.y + 1.0), float2(127.1, 311.7))) * 43758.5453);
            nn += amp * (lerp(lerp(h00, h10, qf.x), lerp(h01, h11, qf.x), qf.y) - 0.5);
            q = q * 3.0 + 17.0; per *= 3.0; amp = 0.65 * NoiseDetail;
        }
        acc += nn;
    }
    n = acc / 5.0;
    // contrast: steepen the wave into blobs, keeping it inside -0.5..0.5
    float k = 1.0 + 6.0 * NoiseContrast;
    n = clamp(n * k, -0.5, 0.5);
}
float depth = smoothstep(R * (1.0 - max(NoiseDepth, 0.01)), R, r);
float rn = r + EdgeNoise * R * 0.25 * n * depth;
// per-channel outer radius: the band starts at the same place for every colour; each channel's edge is offset
// by its own fraction of the radius (lateral CA at the rim), so the tint only appears in the last part of the falloff
float inner = R * (1.0 - soft);
float3 Rc = R * (1.0 + float3(CAR, CAG, CAB));
float3 t = smoothstep(inner.xxx, max(Rc, inner + 1e-4), rn.xxx);
t = pow(t, max(FalloffPower, 0.01));
float3 m = 1.0 - t * saturate(Opacity);
// scatter: inside the band the picture smears radially and lifts a little (light spreading in the edge glass)
float tb = smoothstep(R * (1.0 - soft), R, r);
float3 col = Scene;
if (Scatter > 0.001 && tb > 0.001)
{
    float2 dir = (r > 1e-4) ? p / r : float2(1, 0);
    dir.y *= max(Ellipticity, 0.01) * asp;
    float2 tng = float2(-dir.y, dir.x);
    float len = band * 0.5 * Scatter * 0.35;
    float3 acc3 = 0;
    [unroll] for (int k = 0; k < 8; ++k)
    {
        float u = (k + 0.5) / 8.0 * 2.0 - 1.0;
        float v = sin(u * 7.0) * 0.35;
        acc3 += SceneTextureLookup(UV + (dir * u + tng * v) * len, 14, false).rgb;
    }
    float3 blur = acc3 / 8.0;
    col = lerp(col, blur * (1.0 + 0.15 * Scatter), tb * Scatter);
}
// vignette-like fade underneath the rim: from FadeReach inside the circle up to FadeAmount at its edge
if (FadeAmount > 0.0001 && FadeReach > 0.0001)
{
    float tf = smoothstep(R * (1.0 - FadeReach), R, r);
    tf = pow(tf, max(FadeCurve, 0.01));
    m *= 1.0 - tf * saturate(FadeAmount);
}
float maskv = Texture2DSample(Mask, MaskSampler, UV).r;
m *= lerp(1.0, maskv, saturate(MaskStrength));
return col * m;
"""


def import_all(tiedtke=True):
    build_image_circle_material()
    import_profiles()
    import_projection_profiles()
    import_derived_profiles()
    import_presets()
    if tiedtke:
        import_tiedtke()


# --------------------------------------------------------------------------------------- derived prime profiles

def import_derived_profiles(preset_file=None, save=True):
    """presets.json derived_profiles -> single-focal parametric profiles evaluated from a base profile (for lenses we have
    specs for but no grid: Petzval 58/85, Ultra Prime 10). The distortion is the base lens's at that focal length (flagged)."""
    preset_file = preset_file or os.path.join(DATA_DIR, "presets.json")
    d = json.load(open(preset_file, encoding="utf-8")).get("derived_profiles", {})
    created = []
    for name, spec in d.items():
        base = unreal.load_asset(f"{PROFILE_PKG}/DLP_{spec['base']}")
        if base is None:
            raise RuntimeError(f"base profile DLP_{spec['base']} missing")
        focal = float(spec["focal"])
        focus = [float(x) for x in base.get_editor_property("focus_cm")]
        flat = []
        for f in focus:
            k = base.evaluate(focal, f)
            flat += [k.k1, k.k2, k.k3, k.p1, k.p2]
        asset = _create_data_asset("DLP_" + name, PROFILE_PKG, unreal.DynamicLensProfile)
        ok = unreal.DynamicLensLibrary.fill_profile(asset, spec.get("label", name), spec.get("source", ""), focus, [focal], flat)
        if not ok:
            raise RuntimeError(f"fill_profile failed for {name}")
        _apply_specs(asset, spec)
        asset.set_editor_property("nominal_focal_mm", focal)
        unreal.DynamicLensLibrary.refresh_profile(asset)
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{PROFILE_PKG}/DLP_{name}")
        _log(f"derived profile DLP_{name}: {focal:g} mm from DLP_{spec['base']}")
    return created


# --------------------------------------------------------------------------------------- reset / rename

def reset_asset(path):
    """Re-import one shipped preset or profile from Tools/data (the Reset To Shipped button)."""
    path = str(path).split(".")[0]
    name = path.split("/")[-1]
    d = json.load(open(os.path.join(DATA_DIR, "presets.json"), encoding="utf-8"))
    if name.startswith("DL_T_") or name.startswith("DLP_T_"):
        series = name.split("_", 2)[2]
        import_tiedtke(series_filter=[series] + [series + sfx for sfx in ("_2x", "_1_8x", "_1_5x")])
        return
    if name.startswith("DLP_"):
        key = name[4:]
        if key in d.get("projection_profiles", {}):
            import_projection_profiles(); return
        if key in d.get("derived_profiles", {}):
            import_derived_profiles(); return
        import_profiles(); return
    if name.startswith("DL_"):
        key = name[3:]
        if key in d["presets"]:
            import_presets(only=[key]); return
    _log(f"reset_asset: {name} is not a shipped asset")


def rename_assets_v06():
    """One-off: v0.5 -> v0.6 asset names (prefixes by data source). Leaves redirectors behind for existing references."""
    profiles = {"DLP_ARRI_Master": "DLP_AD_ARRI_Master", "DLP_ZEISS_Supreme": "DLP_AD_ZEISS_Supreme",
                "DLP_Nikkor_6mm_Fisheye": "DLP_L_Nikkor_6mm_Fisheye", "DLP_Nikkor_6mm_Fisheye_Frame": "DLP_L_Nikkor_6mm_Fisheye_Frame",
                "DLP_Nikkor_8mm_Fisheye": "DLP_L_Nikkor_8mm_Fisheye", "DLP_Nikkor_OP_10mm_Fisheye": "DLP_L_Nikkor_OP_10mm_Fisheye",
                "DLP_Optex_4mm_S16_Fisheye": "DLP_L_Optex_4mm_S16_Fisheye", "DLP_Stereographic_10mm_FullFrame": "DLP_L_Stereographic_10mm_FullFrame"}
    presets = {"DL_Master": "DL_AD_Master", "DL_Supreme": "DL_AD_Supreme", "DL_MasterHeavy": "DL_C_MasterHeavy", "DL_Subtle": "DL_C_Subtle",
               "DL_Vintage": "DL_C_Vintage", "DL_Lanthimos_Favourite_6mm": "DL_L_Favourite_6mm", "DL_Lanthimos_Favourite_6mm_Frame": "DL_L_Favourite_6mm_Frame",
               "DL_Lanthimos_Favourite_10mm": "DL_L_Favourite_10mm", "DL_Lanthimos_Favourite_10mm_Rect": "DL_L_Favourite_10mm_Rect",
               "DL_PoorThings_Porthole_4mm": "DL_L_PoorThings_4mm_Porthole", "DL_PoorThings_Lab_8mm": "DL_L_PoorThings_8mm",
               "DL_PoorThings_Petzval": "DL_L_PoorThings_Petzval_58"}
    n = 0
    for pkg, table in ((PROFILE_PKG, profiles), (PRESET_PKG, presets)):
        for old, new in table.items():
            if unreal.EditorAssetLibrary.does_asset_exist(f"{pkg}/{old}") and not unreal.EditorAssetLibrary.does_asset_exist(f"{pkg}/{new}"):
                if unreal.EditorAssetLibrary.rename_asset(f"{pkg}/{old}", f"{pkg}/{new}"):
                    n += 1; _log(f"renamed {old} -> {new}")
    return n


# ------------------------------------------------------------------------------- the lens catalogue

# Who each preset prefix's measurements come from. The catalogue carries this on every entry so a
# consumer (the preset browser, a docs page) never has to re-derive provenance from a name.
ORIGINS = {
    "AD": {"author": "Andy Davis", "org": "Imagery for Media", "url": "https://imag4media.com/vfx-rnd/",
           "licence": "Andy Davis's own terms - redistributed here with permission, NOT Apache-2.0"},
    "T": {"author": "tiedtke", "org": "Real Cinema Lenses",
          "url": "https://tiedtke.gumroad.com/l/realcinemalenses",
          "licence": "tiedtke's own terms - redistributed here with permission, NOT Apache-2.0"},
    "L": {"author": "Dylan G (Mad Rice)", "org": "DynamicLens",
          "url": "https://github.com/Dylanyz/DynamicLens",
          "licence": "Apache-2.0 (reconstruction of a film's look, not measured third-party data)"},
    "C": {"author": "Dylan G (Mad Rice)", "org": "DynamicLens",
          "url": "https://github.com/Dylanyz/DynamicLens", "licence": "Apache-2.0"},
}

TYPE_NAME = {
    unreal.DynamicLensProfileType.PARAMETRIC: "Parametric",
    unreal.DynamicLensProfileType.ST_MAP: "STMap",
    unreal.DynamicLensProfileType.PROJECTION: "Projection",
}


def _stmap_edge_shift(maps, n=48):
    """How much the strongest map BENDS the image, as a percent, ignoring any uniform scale.

    Measured as the swing in the radial ratio |source - centre| / |uv - centre| between the
    tightest and widest sample. Three things this deliberately does not do, each of which was
    tried first and was wrong:

      * Not NeededOverscan. That is one-sided - it only counts a map pulling the source outside
        the frame - so every barrel lens stores exactly 1.0 and reads as undistorted. 20 of the
        22 Andy Davis spherical sets sit at 1.0 for that reason.
      * Not a straight (su,sv) - (u,v) difference. read_st_map_samples returns V in the map's own
        bottom-left convention, so that measures the flip, not the lens (~190% for everything).
      * Not the peak ratio. These maps carry a uniform scale: the Zeiss CP3 85 mm map is flat at
        0.947 at every radius, which is a 5.3% zoom and no bend at all. Taking the peak gave every
        spherical prime the same ~6% and made 85 mm read as more distorted than 18 mm.
    """
    worst = 0.0
    for e in maps:
        tex = e.get_editor_property("map")
        if tex is None:
            continue
        uv = unreal.DynamicLensLibrary.read_st_map_samples(tex, n, n)
        if not uv or len(uv) < n * n * 2:
            continue
        lo, hi = None, None
        for j in range(n):
            for i in range(n):
                du, dv = (i + 0.5) / n - 0.5, (j + 0.5) / n - 0.5
                r = (du * du + dv * dv) ** 0.5
                if r < 0.1:                       # the ratio is all noise near the centre
                    continue
                su = uv[2 * (j * n + i)] - 0.5
                sv = uv[2 * (j * n + i) + 1] - 0.5
                ratio = ((su * su + sv * sv) ** 0.5) / r
                lo = ratio if lo is None else min(lo, ratio)
                hi = ratio if hi is None else max(hi, ratio)
        if lo is not None:
            worst = max(worst, hi - lo)
    return round(worst * 100.0, 2)


def _parametric_edge_shift(rows, sensor):
    """How far the frame corner moves, as a percent of its radius, at the strongest focal.

    Deliberately the same quantity the ST-map profiles report through NeededOverscan, so the two
    are comparable in a browser. Radial terms only; tangential barely moves a corner.
    """
    worst = 0.0
    for row in rows:
        for p in row.get_editor_property("by_focus"):
            shift = (float(p.get_editor_property("k1")) + float(p.get_editor_property("k2"))
                     + float(p.get_editor_property("k3")))   # normalised corner radius, r = 1
            worst = max(worst, abs(shift))
    return round(worst * 100.0, 2)


def _profile_facts(prof):
    """Everything about one profile a browser would want to show at a glance."""
    if prof is None:
        return None
    ptype = TYPE_NAME.get(prof.get_editor_property("type"), "Unknown")
    sensor = prof.get_editor_property("native_sensor_mm")
    focus = [float(f) for f in prof.get_editor_property("focus_cm")]
    rows = prof.get_editor_property("rows")
    maps = prof.get_editor_property("st_maps")
    overscan = None

    if ptype == "STMap":
        focals = sorted(float(e.get_editor_property("focal_mm")) for e in maps)
        # every shipped ST map is a single-focus measurement; FocusCm is 0 on all of them
        focus = sorted({float(e.get_editor_property("focus_cm")) for e in maps})
        overscan = max([float(e.get_editor_property("needed_overscan")) for e in maps] or [1.0])
        edge_shift_pct = _stmap_edge_shift(maps)
        samples = len(maps)
    elif ptype == "Parametric":
        focals = sorted(float(r.get_editor_property("focal_mm")) for r in rows)
        edge_shift_pct = _parametric_edge_shift(rows, sensor)
        samples = sum(len(r.get_editor_property("by_focus")) for r in rows)
    else:
        focals, edge_shift_pct, samples = [], None, 0

    squeeze = float(prof.get_editor_property("squeeze"))
    return {
        "asset": prof.get_path_name().split(".")[0],
        "type": ptype,
        "label": prof.get_editor_property("label"),
        "coverage": prof.get_editor_property("coverage"),
        "source": prof.get_editor_property("source"),
        "squeeze": squeeze,
        "anamorphic": squeeze > 1.001,
        "sensor_mm": [round(sensor.x, 3), round(sensor.y, 3)],
        "image_circle_mm": round(float(prof.get_editor_property("image_circle_mm")), 2),
        "max_aperture": float(prof.get_editor_property("max_aperture")),
        "iris_blades": int(prof.get_editor_property("iris_blades")),
        "blade_curvature": float(prof.get_editor_property("blade_curvature")),
        "front_diameter_mm": float(prof.get_editor_property("front_diameter_mm")),
        "nominal_focal_mm": float(prof.get_editor_property("nominal_focal_mm")),
        "projection": str(prof.get_editor_property("projection")).split(".")[-1] if ptype == "Projection" else None,
        "max_field_angle_deg": float(prof.get_editor_property("max_field_angle_deg")) if ptype == "Projection" else None,
        "focals_mm": focals,
        "focal_min_mm": min(focals) if focals else None,
        "focal_max_mm": max(focals) if focals else None,
        "focus_cm": focus,
        # a profile only breathes if it was measured at more than one focus distance
        "breathes": len(focus) > 1,
        "measurements": samples,
        "needed_overscan": round(overscan, 4) if overscan else None,
        "edge_shift_pct": edge_shift_pct,
    }


def export_catalogue(path=None, save=True):
    """Every preset, with who measured it and what it does -> Tools/data/lens_catalogue.json.

    Generated, never hand-edited: re-run it after any import so it stays true. It is the one place
    that answers "what lenses does this plugin have and where did each come from", and it is what
    a preset browser should read rather than walking the asset registry itself.
    """
    path = path or os.path.join(DATA_DIR, "lens_catalogue.json")
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = ar.get_assets(unreal.ARFilter(class_names=["DynamicLensPreset"], recursive_classes=True))
    entries = []
    for a in assets:
        preset = unreal.load_asset(str(a.get_editor_property("package_name")))
        if preset is None:
            continue
        name = preset.get_name()
        m = re.match(r"DL_([A-Z]+)_", name)
        prefix = m.group(1) if m else ""
        dist = preset.get_editor_property("distortion")
        prof = _profile_facts(dist.get_editor_property("profile"))
        circle = preset.get_editor_property("image_circle")
        vign = preset.get_editor_property("vignette")
        pkg = preset.get_path_name().split(".")[0]
        entries.append({
            "preset": name,
            "asset": pkg,
            "folder": pkg.rsplit("/", 1)[0],
            "prefix": prefix,
            "origin": ORIGINS.get(prefix, {}),
            "display_name": (prof or {}).get("label") or name,
            "description": preset.get_editor_property("description"),
            "distortion_amount": float(dist.get_editor_property("amount")),
            "breathing": float(dist.get_editor_property("breathing")),
            "lock_focal_length": bool(dist.get_editor_property("lock_focal_length")),
            # a lens you can zoom freely: nothing pins the camera to one focal length
            "zoomable": not bool(dist.get_editor_property("lock_focal_length")),
            "image_circle_enabled": bool(circle.get_editor_property("enabled")),
            "vignette_enabled": bool(vign.get_editor_property("enabled")),
            "profile": prof,
        })
    entries.sort(key=lambda e: e["preset"])
    doc = {
        "_doc": "Generated by dl.export_catalogue(). Every DynamicLens preset with its provenance, "
                "optics and coverage. Do not hand-edit - re-run it after any import. The source of "
                "truth for the preset data itself stays Tools/data/presets.json.",
        "_metrics": {
            "edge_shift_pct": "How much the lens bends the image, ignoring any uniform scale. "
                              "STMap: the swing in the radial ratio between the tightest and "
                              "widest sample of the strongest map. Parametric: |K1+K2+K3| at the "
                              "normalised corner. Projection: not defined. The two derivations "
                              "rank consistently but are NOT the same quantity - do not plot an "
                              "ST-map lens and a parametric one on one bar without saying so.",
            "scale": "Roughly: under 5 is a well corrected modern prime, 10-20 is strong "
                     "character, over 20 is a vintage anamorphic.",
        },
        "_licence": "This file describes third-party measured data. See NOTICE and SOURCES.md: "
                    "tiedtke's and Andy Davis's lens data is redistributed with permission under "
                    "their own terms, NOT under this repo's Apache-2.0 licence.",
        "counts": {
            "presets": len(entries),
            "anamorphic": sum(1 for e in entries if (e["profile"] or {}).get("anamorphic")),
            "spherical": sum(1 for e in entries if e["profile"] and not e["profile"]["anamorphic"]),
            "breathing": sum(1 for e in entries if (e["profile"] or {}).get("breathes")),
            "zoomable": sum(1 for e in entries if e["zoomable"]),
        },
        "by_origin": {},
        "presets": entries,
    }
    for e in entries:
        doc["by_origin"].setdefault(e["prefix"], []).append(e["preset"])
    if save:
        with open(path, "w") as f:
            json.dump(doc, f, indent=1)
    _log(f"catalogue: {len(entries)} presets -> {path}")
    return doc
