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
                if save:
                    unreal.EditorAssetLibrary.save_asset(dst)   # a duplicate only lives in memory until saved
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
        _set_struct(preset, "distortion", {"profile": prof, "lock_focal_length": True})
        preset.set_editor_property("description", f"tiedtke {name.replace('_', ' ')} {squeeze:g}x anamorphic ST maps, exact at the measured focal lengths (nearest is used). Use Match Camera To Profile for the native 2.39 frame.")
        if save:
            unreal.EditorAssetLibrary.save_loaded_asset(preset)
        created.append((prof_name, n))
        _log(f"tiedtke {prof_name}: {n} maps, squeeze {squeeze:g}")
    return created



# --------------------------------------------------------------------------------------- Andy Davis creative lens maps

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

    `root` is the folder of half-res EXRs and manifest.json produced by Tools/prep_andy_stmaps.py;
    it defaults to $DYNAMICLENS_ANDY_DIR. The maps are his free release, not redistributable here,
    so nothing under it belongs to this repo - see NOTICE and SOURCES.md.

    Geometry (focal lengths, sensor, overscan) is measured and comes from the manifest and the
    textures. The editorial and physical layer comes from presets.json `andy_stmap_sets`.
    """
    root = root or os.environ.get("DYNAMICLENS_ANDY_DIR")
    if not root or not os.path.isdir(root):
        raise RuntimeError("pass root=<folder with manifest.json> or set DYNAMICLENS_ANDY_DIR; "
                           "generate it with Tools/prep_andy_stmaps.py")
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
