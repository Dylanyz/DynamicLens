# Dynamic lens system — original investigation + design (2026-09-06)

Moved 2026-10-04 from CitySample `.claude/refs/lens-system-design.md` (restructure phase 4): only the hunks not already
in this repo's README / CLAUDE.md / `.claude/refs/`, copied verbatim (source lines 13, 21-41, 46-144, 158-185).
Historical design notes: paths under `Content/CinematicTemplate/Lenses/`, `Scripts/` and the LensRig design refer to the
retired Python prototype in CitySample; the component design in `architecture.md` supersedes the LensRig section.
Most valuable part: **What UE 5.8 gives us (engine source)** — the handler API this plugin drives.

From the v2 note (line 13):
> assets (asset-picker dropdowns, tooltips, clamps, categories); `UDynamicLensLibrary` (AddToAllCineCameras, FillProfile);

> **BUILT 2026-09-06 → `Content/CinematicTemplate/Lenses/DynamicLens/`** (README there). What actually shipped differs
> from the plan below in two ways, both learned the hard way:
> 1. **No LensComponent.** Components only tick when a viewport is Realtime (`UEditorEngine::Tick` uses
>    `LEVELTICK_TimeOnly` otherwise), and `LevelEditorSubsystem.editor_set_viewport_realtime(True)` only clears
>    overrides, it can't switch Realtime on. Instead the rig owns a transient `SphericalLensDistortionModelHandler`
>    (`unreal.new_object(cls, camera_component)`) and a transient in-memory `LensFile` with one point; calling
>    `LensFile.evaluate_distortion_data(0,0,filmback,handler)` draws the displacement maps and computes overscan.
>    Then `camera.add_or_update_blendable(handler.distortion_post_process_mid)` + `camera.overscan = factor-1`.
>    Python-only, no world tick needed. (`CameraCalibrationSubsystem.find_or_create_distortion_model_handler` is
>    deprecated and returns None in 5.8.)
> 2. `AActor.add_component_by_class` isn't exposed to Python in 5.8; if a component is ever needed, use
>    `SubobjectDataSubsystem.add_new_subobject` — and beware: an exception *after* the add on every tick piles
>    up components/actors (47 zombie LensComponents, 233 zombie proxy cameras happened). Register spawned objects
>    in state *before* any call that can throw.
>
> Verification gotchas: Epic MCP `CaptureViewport` renders its own view **without camera post-process** (no
> distortion, no vignette visible) — use `CaptureEditorImage` (`Scripts/capture_viewport.py --editor`) or
> `HighResShot`; both need the viewport to actually redraw (Realtime, or `editor_invalidate_viewports()`).
> Settings actor = `BP_DynamicLens` (found by class, not tag — CDO tags didn't propagate to spawned instances).
> Still to verify: PIE/MRG path (needs a PIE session), bokeh values by eye, anamorphic profiles (not built).


## What's in `Content/CinematicTemplate/Lenses/`

| Source | Assets | Data type | Coverage |
|---|---|---|---|
| **Andy Davis** (Imagery for Media) `ARRI-ZEISS_Master/` | 17 `ULensFile` (12–150 mm + 100 macro) + `CameraLens_*.ini` | **Parametric spherical** K1 K2 K3 P1 P2, one zoom point per file, **many focus points** (shot at ~35 witness marks min→∞ on Alexa SXT 2.8k, 23.76×17.82 mm) | full focus breathing per prime |
| Andy Davis `ZEISS_Supreme/` | 14 `ULensFile` (15–200 mm) + ini | same | 40 mm / 65 mm files are tiny (12–20 KB) → probably few focus points, check |
| **tiedtke** `1_5x/ 1_8x/ 2x/` | ~80 `ULensFile` + one `Texture2D` ST map each | **ST map** (RG, 32-bit EXR source embedded in the texture, bottom-left origin), one focus, one zoom | 18 anamorphic series; Panavision C has **13 focal lengths (20–200)**, E has 9, Cooke FFi 7 |
| `ClaudeDynamicLens.uasset` | 1 `ULensFile` "LF_DynamicCinema" (UE 5.7, from MDR project) | parametric, focus × zoom table | the old attempt that snapped |

Andy's raw material in `ProjectHub/UE Assets/Lenses/Resources/`: three EXR sequences (35/38/26 frames = focus marks) of
distortion maps for Master 12 mm, Master 25 mm, MK 65 mm; his site text (lens presets + "dynamic lens models" posts);
Zeiss "Depth of Field and Bokeh" (Nasse 2010). His ini has a typo: Master 025mm `MaxFocalLength=246`.

### Live-editor probe results (2026-09-06, read-only)

- Python bindings all exist: `LensComponent.set_distortion_state`, `LensDistortionState`, `CameraCalibrationSubsystem.find_or_create_distortion_model_handler`,
  `LensDistortionModelHandlerBase.set_distortion_state`, `Spherical/AnamorphicLensModel`, `DistortionRenderingMode`,
  `CameraComponent.overscan/crop_overscan`, `PostProcessSettings.depth_of_field_petzval_bokeh/_barrel_radius`.
- LensFile tables are NOT `get_editor_property`-able; use `lf.get_distortion_points()` / `get_focal_length_points()` /
  `get_st_map_points()` (each returns point-info structs with `.focus .zoom .distortion_info.parameters`).
- **Andy's ARRI Master data**: 24–45 focus points per prime, only **K1 + K2** non-zero. Real breathing, big at the wide end:
  12 mm K1 goes **−0.042 (min focus) → +0.026 (∞)** (barrel flips to pincushion); 35 mm −0.033 → −0.018; 100 mm −0.011 → −0.004.
  Zoom column is inconsistent (12 mm file: 0 and 12; 35 mm: 35 and 325; 100 mm: 75 and 100) — treat the file name as the focal.
- **The focus axis is UE focus distance in cm** (0 → ~9,600 cm, 12 mm file → 18,058 cm). Confirmed in
  `LensComponent.cpp`: in `UseCameraSettings` mode `EvalInputs.Focus = CineCameraComponent->CurrentFocusDistance` (cm) goes
  straight into the table. Use the values as-is; no ring→distance law needed, no need to ask Andy. (Dylan spotted this,
  2026-09-06.) The first row sits at 0 cm, below real min focus; rows 0–1 cover the close range. Andy's encoder table is a
  single key at min focus (12 mm → 33, 35 mm → 35, 100 mm → 100 cm) and is irrelevant for us.
- Zeiss Supreme coverage is uneven: 40 mm has 3 focus points, 50 mm has 45.
- **tiedtke lens files are broken in this project**: their ST-map textures are referenced at `/Game/Lenses/...` (the pack's
  original location) but the pack lives at `/Game/CinematicTemplate/Lenses/`, so `st_map_info.distortion_map` is **None** on
  every file. Fix = move the pack to `/Game/Lenses` or re-point the texture refs. The textures themselves are tiny:
  **32×32 px, TC_HDR_F32** (a grid of UV samples, not a pixel map) — perfect for parametric fitting.

- **All 31 Andy files dumped** → `Scripts/LensRig/data/andy_davis_lensfiles.json` (distortion, focal-length, image-centre
  points per file). Each file also carries a leftover zoom point from the previous focal (he duplicated files), so filter
  to the dominant zoom. Trend is clean: Masters K1 at ∞ from −0.069 (18 mm) → −0.018 (35) → −0.004 (100) → +0.003 (150);
  every lens breathes (K1 more negative at min focus). Supreme 15 mm has a big K2 (+0.19–0.24) = mustache profile.
- **Viewport FOV is readable without a camera**: `LevelEditorSubsystem.get_level_viewport_fov()` / `set_level_viewport_fov()`
  and `get_pilot_level_actor()`. So the rig reads the viewport FOV directly for the free viewport and only touches the
  camera when one is piloted (for focus distance, filmback, f-stop, squeeze).

## What UE 5.8 gives us (verified in engine source, not docs)

- **`ULensDistortionModelHandlerBase`** (`CameraCalibrationCore`) is a standalone UObject: `SetDistortionState(FLensDistortionState)`,
  `ComputeOverscanFactor()`, `SetOverscanFactor()`, `ProcessCurrentDistortion()`, `GetDistortionMID()`. Create one via
  `CameraCalibrationSubsystem.FindOrCreateDistortionModelHandler(picker, ModelClass)`. **No Lens File needed** — we feed the
  state ourselves every tick. Models: `SphericalLensModel` (K1 K2 K3 P1 P2), `AnamorphicLensModel` (3DE4 degree-4:
  CX02 CX04 CX22 CX24 CX44, CY…, SqueezeX/Y, LensRotation, PixelAspect), plus Brown-Conrady UD/DU.
- **`ULensComponent`** has `DistortionSource = Manual` + `SetDistortionState()`: it then does everything (handler, overscan,
  rendering mode) without ever touching a Lens File. Rendering modes: `PostProcessMaterial` (camera blendable),
  `TemporalSuperResolution` (distortion applied inside TSR, sharpest; `r.TSR.LensDistortion`), `PassAfterTonemap`.
- **Camera overscan is native**: `CameraComponent.Overscan` (0–1), `bCropOverscan`, `bScaleResolutionWithOverscan`,
  `AsymmetricOverscan`. LensComponent writes `Overscan = factor-1` and `bCropOverscan = (mode == TSR)`. **MRG honours it**
  ("Crop Camera Overscan", 5.6+), so renders need nothing extra.
- Lens File interpolation in 5.8 is a **Coons-patch blend across focus × zoom** (`LensInterpolationUtils`) — smooth. The
  snapping in the old `LF_DynamicCinema` was the file/eval setup, not the engine. Irrelevant now: we bypass Lens Files.
- **Bokeh, new in 5.8 `FPostProcessSettings`**: `DepthOfFieldPetzvalBokeh` (+Falloff, exclusion box) = swirly bokeh,
  `DepthOfFieldBarrelRadius/Length` = **cat's-eye** mechanical vignetting, `DepthOfFieldMatteBoxFlags[3]`,
  `DepthOfFieldAspectRatioScalar`, plus the existing `DepthOfFieldSqueezeFactor` (oval anamorphic bokeh) and
  `DiaphragmBladeCount` (polygonal iris). Not available: custom bokeh texture, spherical-aberration ring/"soap-bubble"
  brightness profile, colour fringing (Zeiss paper §"Bokeh" — those are aberration signatures, not iris shape).
- **Imperfecter** (Hubert Mika, 1.5.2) is a post-process toolkit (dirt, grain, film damage, its own simple distortion). Keep it
  for grain/dirt; don't stack its distortion on ours.

## The three ways to distort in UE (Dylan's question)

| Method | How it works | Focal/focus aware | Overscan | Anamorphic | Verdict |
|---|---|---|---|---|---|
| **Camera Calibration plugin** (Lens File + Lens Component) | Physically parametric (K1–K3 P1 P2 or 3DE anamorphic) or ST map → GPU displacement map → applied as camera blendable, or inside TSR (5.5+) | yes, tables over focus × zoom | computed automatically, written to the camera | yes, proper model | the right maths; the asset/editor part is the buggy, per-camera part |
| **Generic post-process material** (what Imperfecter and most "lens FX" packs do) | material in the PP chain warps `ScreenPosition` UVs with a radial `k·r²` style formula + a strength slider; scales the image up to hide the edges | no | none (loses resolution instead) | no | fine for a quick look, not for a system |
| **Ours (LensRig)** | Camera Calibration **handler** fed by our own curves every tick; no Lens File asset, no manual per-camera setup | yes, any focal/focus | native camera overscan (+ MRG crop) | yes | recommended |

**Imperfecter** (Hubert Mika, v1.5.2) = a post-process toolkit: lens dirt, film grain/digital noise, film damage (dust,
scratches), chromatic aberration, vignette, halation-type effects and a simple radial distortion, all as its own PP
materials driven by a manager actor. Keep it for grain/dirt/CA; turn its distortion off so it doesn't stack with ours.

## Recommended architecture: **LensRig** (one actor, engine-wide)

One `BP_LensRig` (or Python ticker) in the level — ticks in editor, PIE and during MRG. Nothing on the cameras is authored by hand.

Per tick:
1. **Find the active view**: piloted/locked CineCameraActor (viewport actor lock or Sequencer camera cut), else the free
   editor viewport (FOV from the viewport client, assume the preset's filmback).
2. **Read** focal length, focus distance, f-stop, filmback, squeeze.
3. **Evaluate the preset**: parameters = `P(f, focus)` from a smooth 2-D table (bicubic in log-focal × 1/focus) → 5 spherical
   or 13 anamorphic floats, plus vignette / bokeh values. Any focal works.
4. **Apply to a cine camera**: ensure a transient `ULensComponent` exists on it (auto-added, `Manual` source), push the state,
   let it handle overscan + rendering mode (TSR for renders, PPM in editor if TSR mode looks odd in the viewport).
   Also push bokeh/vignette values into the camera's `PostProcessSettings` overrides.
5. **Apply to the free viewport**: same handler MID as a blendable in a global `PostProcessVolume` and widen the viewport
   FOV by the overscan factor (restore on toggle-off).
6. **Toggle/preset switch** = a Blueprint/console variable on the rig; presets are data assets.

Why not a Lens File per camera: crashes, per-camera setup, and the Lens File editor is the buggy part. Manual-mode
LensComponent + handler never loads a Lens File. Why not a pure custom post-process material: we'd re-implement Epic's
displacement map, overscan maths and TSR integration; the handler gives all three.


## Bokeh (answer to "could bokeh be implemented")

Yes, without custom shaders: the rig drives the 5.8 DOF fields per preset and per f-stop:
- **Iris shape**: `DiaphragmBladeCount` (Andy's inis already carry 9/16 blades per series).
- **Anamorphic oval**: `DepthOfFieldSqueezeFactor` = lens squeeze.
- **Cat's-eye at the edges**: `DepthOfFieldBarrelRadius/Length` — strongest wide open, fades when stopped down (Zeiss paper:
  aperture is the dominant variable), so key it to f-stop.
- **Swirl** (vintage/Petzval presets): `DepthOfFieldPetzvalBokeh` + falloff.
- **Not doable natively**: bright-edge/soap-bubble rings, onion rings, green/purple fringing (spherical + longitudinal chromatic
  aberration). Could be faked later with a light post-process on highlights, but it's a separate feature — park it.

## Verification plan (needs engine control, ~1 session)

1. Dump + fit Andy's tables; plot K1(f, focus) sanity (12 mm strongly negative barrel, 100 mm ≈ 0).
2. Spawn a CineCamera, add LensComponent Manual, push a state from the fit, `CaptureViewport` at 12 / 24 / 49.5 / 100 mm and
   two focus distances: smooth, no black corners, overscan value read back.
3. Free viewport path with the global PPV.
4. One MRG test render (TSR mode, crop overscan) to confirm the tick runs per frame and temporal samples are stable.
5. Then the ST-map fitting for the anamorphic presets.

## Open questions / risks

- Tick ordering inside MRG (temporal samples): if the state lags a frame we bake per-shot keys instead (Sequencer track on
  the LensComponent's `DistortionState` is `Interp`, so it can be keyed).
- Free-viewport overscan fights manual FOV changes → store the user FOV and only scale ours.
- Displacement-map resolution for 4K renders (handler default) — bump `CreateDisplacementMaps`.
- Aspect handling for anamorphic (crop vs desqueeze) is a preset rule, decide once we see it.
