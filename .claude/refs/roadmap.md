# Roadmap — work that is ready but deliberately waiting

Things that have been investigated, are worth doing, and are **not** being done yet, each with
what it is gated on. This is not a wish list: an entry only belongs here once the research is
done and someone could pick it up and build it.

**How to use it.** When the gate on an entry has cleared, *propose it to Dylan* — do not just
start. When you finish an entry, delete it from this file rather than marking it done; git history
is the record.

---

## Where this stands — handoff, 2026-09-25 (evening)

**Fisheye session done, all committed and installed** (installed DLL matches HEAD). Read this before
the older block below, parts of which it supersedes.

- **Fixed:** every `DL_L_*` fisheye had applied *no* distortion since v0.3 (empty distortion map).
  Also a ~2-frame raw-overscan flash after every fisheye rebuild.
- **New:** one preset per lens (4, 6, 8, 10 mm). Image Circle > **Scale** (Lens / Frame) scales circle
  and picture together. **Field** = Fit to Circle / True Angles. Dynamic fisheye overscan. 1024 bake map.
  Honest centre-sharpness notes. The `_Fit` and `_Frame` presets were merged and deleted.
- **Decided by Dylan:** cube capture is **rejected** ("it breaks too much"). Keep all looks, but as
  controls rather than presets. Fisheyes open mostly filling the frame. Viewport sharpness is
  deferred (entry below). Vertical squeeze is skipped until a shot asks for it (entry below).
- **Verification method:** `visual-verification.md`. Dylan reviewed the contact sheets and the live
  viewport.
- **Still open from before:** the TSR Render Mode tooltip line, and the six `DL_T_*` overscan ceilings (`todo.md`).

---

## Where this stands — handoff, 2026-09-24 (late night)

**Paused on purpose.** Everything that needed no eyes is done and pushed (last commits `ae7e8ed`
hide presets, `178f4dc` Sequencer phases 2-3 + Lens Kits, `dd9870b` LOD re-diagnosis, `1b5f9c6`
Circle Coverage + Match Camera fix, `7bfb9df` TSR PIE crash). The installed DLL matches HEAD. The
editor was **closed at Dylan's request** (heat) and he expects to test in about two days, around
2026-09-26. **Do not relaunch the editor or start new C++ work until he has tested** - the next step
is his review, then fixes from it, batched into one build.

**2026-09-26 (planned): an agent may run the test list itself with computer use.** Dylan enabled
computer use in the Claude app (2026-09-25) and will be off the PC. That agent may launch the editor
(CitySample) for this, run the list below visually, take real screenshots, and **close the editor
when done** (heat). If the screen shows the Windows lock screen, stop: never enter a password - just
leave the list for Dylan. Report results in this block; fix nothing visual without his OK.

**Results, 2026-09-25 run (agent with computer use, editor unlocked):**
- 1 Preset Browser hide: **PASS - Dylan tested by hand, works.** Synthetic clicks from computer use
  never fired the eye button (the star beside it did), so treat that button as untestable by automation.
  Previously logged as: **FAIL as clicked.** The eye button shows its tooltip and highlights, but
  four clicks never hid the lens, and nothing reached the ini. The star and search beside it work.
  The code path (`ToggleHidden` -> `SetHidden` -> `Save`) reads correctly. Dylan to try by hand.
- 2 Circle Coverage: **PASS** (PIE, overscan overridden to 1.0). 0.5 = circle ~half the frame
  diagonal, 1.0 = touches the corners; readback 0.500 / 1.000, mask "lens image circle".
- 3 Anamorphic oval: **PASS.** `DL_T_Arri-Zeiss_MasterAnamorphic` at 0.8 draws a horizontal oval.
- 4 Lens kit: **PASS.** `LS_KitTest` (`/Game/Claude/DynamicLens/`), focal keyed 8 then 58 at frame
  48; scrubbing 10/60/20/70/47/48 via Sequencer switches 8 mm fisheye <-> Petzval 58 every time.
- 5 Match Camera: **PASS** (function called from Python, same as the button). Leica R -> Master
  Anamorphic with auto-match off keeps squeeze 1; Match Camera -> squeeze 2, 23 x 18.66 mm, 35 mm.
- 6 Accumulation DOF vs swirl / cat's eye: **claim does NOT hold for cat's eye.** `DL_L_PoorThings_Petzval_58`,
  f/2, focus 3 m, dot grid at 15 m, editor viewport piloting the camera (Lit; camera post-process
  *does* apply there). Converged 256-sample A/B: lens Bokeh off -> every highlight small and round;
  Bokeh on (barrel radius/length set) -> highlights toward the edges squashed toward the centre,
  city lights at the frame edge clearly lens-shaped. Accumulation DOF honours the barrel settings.
  Swirl is not separable from cat's eye by eye here (both elongate tangentially). (An earlier
  "all round" frame was taken before accumulation had started - ignore it.)
- **The preset's spherical aberration 8 is far too strong under Accumulation DOF:** with Drive on it
  doubles the blur and breaks every highlight into a spray of sparse ghost discs. Drive off, or
  spherical 0 with Drive on, gives a clean frame. Tune `SphericalAberration` in `presets.json`
  (units are Seidel W040 in cm, 0-100) - Dylan's call, it is a Petzval preset.
- 7 Iris texture: **reaches the component but the blade shape does not show.** Dynamic Lens writes a
  256 px iris texture and `bEnableBokehTexture` true. With 5 straight blades (component override),
  `BokehEdgeSoftness` 0 and f/1.4, the centre highlight is still round (thresholded crops). Suspect
  the transient `UTexture2D` is not used by the pass, or the weighting; needs a known-good texture A/B.
- 8 Controls that could stand in: **none for swirl or cat's eye** (read from
  `AccumulationDOFComponent.h`). Only `ComaAberration` is position-dependent (tails growing toward the
  edges); the rest are `SphericalAberration`, axial CA (+ bands), spectral lateral CA (drives off
  `SceneFringeIntensity`), bokeh texture/tint/edge softness. So the plugin cannot fake the look: warn.
- 9 Exposure: **not dimmer and no vignette stacking** in the viewport preview with CitySample's
  auto-exposure: mean 140.2 (post-process DOF) vs 139.9 (accumulated, same f/2), corner/centre 0.47
  in both. Auto-exposure could mask dimming; repeat with manual exposure before trusting it.
- 10 MRG render, Auto Activate on vs off: **cost ~39x, not 3-10x.** `MRG_AccumTest` + `LS_AccumTest`
  (`/Game/Claude/DynamicLens/`), 10 frames, 854x480, temporal 5, spatial 1, TSR, 256 aperture samples:
  on 63.7 s, off 1.6 s. Accumulated render is *brighter* (mean 141 vs 107, spherical 8 spreading the
  highlights), corner/centre 0.50 vs 0.40 - no vignette stacking. The SA-8 sample-spray look is in the
  final render too, not just the preview. Output left in the session scratchpad, not in the project.
- **Temporal / spatial samples do not break Dynamic Lens** (2026-09-25, MRG, 10 frames with a 12 deg
  pan, Post Process Material mode, 854x480). Petzval 58, `DL_L_PoorThings_8mm_Fit` and Master
  Anamorphic at T1S1 / T8S1 / T1S8: temporal 8 adds correct motion blur and keeps circle, bokeh and
  distortion intact; spatial 8 matches T1S1 in structure. Spatial 8 renders ~20-27% brighter - but a
  lens-off control does the same (76.5 -> 95.0), so that is MRG + auto-exposure, not the plugin.
- **Harness gotcha:** `MRG_DLTest` (and so any copy) has Custom Playback Range start/end and a 30 fps
  output-rate override switched on, rendering frames ~1150-1156 - past every test sequence, so there is
  no camera cut and MRG renders an unrelated upside-down view. Turn those three overrides off first.
- **Crash:** pressing Play (PIE) while the viewport's Accumulate preview is on kills the editor
  inside `AccumulationDOF`/`AccumulationDOFEditor` (access violation). Epic experimental plugin; turn
  Accumulate off before PIE.
- Gotcha: ticking an Override block seeds it from the preset, so from Python set `override_*`
  **before** writing the struct, or the values are overwritten.

**Dylan's test list, in this order** (given to him 2026-09-24):
1. Preset Browser (Window > Cinematics): hide a lens, check the Hidden section, then check it is gone
   from the component's Preset dropdown - the one piece automation could not reach.
2. Circle Coverage: `DL_L_PoorThings_8mm`, tick the Image Circle override, Size = Coverage, try 0.5 and 1.0.
3. Anamorphic circle: a `DL_T_*` preset at Coverage 0.8 should draw an oval.
4. Lens kit: Kit = `DLK_L_PoorThings`, key the CineCamera focal at 8 then 58, scrub.
5. Match Camera: spherical preset -> a `DL_T_*` with auto-match off, press Match Camera, squeeze should become 2.

**Then, same session - verify the Accumulation DOF claims** (Dylan, 2026-09-25: the agent may enable
the experimental **Accumulation Depth of Field** plugin in CitySample, restart included). They are one
creator's observations, not Epic docs (details: the "Final-render validation" entry). On one camera in
a scratch level under `/Game/Claude/`, with a Dynamic Lens preset that has strong swirl and cat's eye
(`DL_L_PoorThings_Petzval_58`, read only - never edit it):
6. Add the Accumulation DOF Camera Component, Number of Samples 256, and accumulate in the viewport.
   Do **Petzval swirl** and **cat's eye** actually vanish? Compare against the same frame without it.
7. Does the plugin's **iris texture** reach the component (blade shape visible at Bokeh Softness 0)?
8. Any per-position or aberration controls on the component that could stand in for swirl / cat's
   eye? List them. That decides whether the plugin can fake the look or should just warn.
9. Exposure: is it dimmer, and does the plugin's vignette stack on top?
10. One short MRG render (Sampling Method temporal 5, TSR, spatial 1) with Auto Activate on vs off.
    Time both. Read `01-mrg-gotchas.md` first.
Leave the plugin enabled afterwards, and record which claims held. Close the editor at the end.

**The three open decisions - Dylan said "idk", so these defaults stand until he says otherwise:**
TSR PIE crash -> leave it, use Post Process Material for PIE and TSR only for Movie Render Graph
(still to do: say so in the Render Mode tooltip, one line, fold into the next build); static-mesh LOD
hack -> skip; Panavision C Series smear -> no action unless it recurs, then try Match Camera once.

**1. Built, installed, committed - waiting on Dylan's eyes, not on code:**

| What | Where to look |
|---|---|
| Force Bokeh Quality (Petzval fix; editor was at High scalability) | `architecture.md`, verified in PIE |
| ~~`DL_L_*_Fit` fisheyes~~ superseded 2026-09-25: merged into one preset per lens with Scale + Field, reviewed by Dylan | handoff block above |
| `DL_AD_Cooke_FFi_Zoom` (3DE4 anamorphic, continuous 32-135 mm) | the Cooke entry below |
| Preset keyable in Sequencer (`SetPreset`) | `sequencer-integration.md`, verified by scrubbing |
| Image Circle > Size = Coverage, anamorphic lens-circle ellipse, Match Camera fix | the last two entries |
| Sequencer phase 2 (locked-focal note, tooltips) and phase 3 Lens Kits (`DLK_L_PoorThings`, `DLK_L_Favourite`) | `sequencer-integration.md` status block; verified from Python, not scrubbed in Sequencer |
| Preset Browser, plus `DynamicLens.PresetBrowser` console command, plus hide presets | next section; its UI has now been seen by automation, not by Dylan |

**2. Decisions only Dylan can make:** the six `DL_T_*` overscan ceilings (`todo.md`); the scratch
assets from 2026-09-19 (`/Game/Cinematics/_render/zz_dltest_MRG`, `Saved/MovieRenders/dltest/` -
nothing deleted, `Saved/` off limits); ~~the cube-source question~~ (rejected 2026-09-25); whether the static-mesh LOD fix is worth a render-path hack (the LOD entry
below - re-diagnosed, much smaller than first written); what to do about TSR mode crashing PIE (an
Unreal 5.8 bug with `bCropOverscan`, reproduced on a stock CineCamera - entry below).

**3. Good next work that needs no eyes:** none left; wait for Dylan's review. Editor restarts were
allowed only while he was away (2026-09-24) - **ask again before any restart from now on.**

**4. Do not trust automated screenshots.** The PC sits on the Windows lock screen when Dylan is away.
**The PIE "centre smear" is not the lock screen and not the plugin (proven 2026-09-25, unlocked
screen, computer use):** in CitySample PIE, *any* CineCamera `Overscan > 0` collapses the whole frame
into a radial smear of sky and fog - a stock CineCamera with no Dynamic Lens does it at 0.1, and it
recovers at 0. Every Dynamic Lens preset sets overscan, so every PIE view smears. For PIE checks,
override the component's overscan to Fixed 1.0 (edges will clamp) or verify in the editor/MRG. Open
question: CitySample-only or engine-wide (try a blank project). Legacy Movie Render Queue shows no lens effect, and Movie
Render Graph works but streams the showroom out. Verify with numbers read off the component
(`needed_overscan_factor`, `last_overscan_factor`, `image_circle_radius`, `active_mask`, `notes`).
Test assets: `/Game/DynamicLensTest/L_DLTest` (`DLTest_Cam`, a sphere grid for bokeh) in CitySample;
**new scratch assets go in `/Game/Claude/`** (Dylan's request). Never touch `/Game/GTA6/Maps`.

**5. Do not touch `DL_L_PoorThings_Petzval_58` / `_85` or any `DL_C_*`.** New looks go in as new
presets beside the old ones (Dylan, 2026-09-24: "create new lenses rather than overriding"); he
deletes the ones he does not like.

**Read first:** `overscan-and-image-circle.md`, `image-circle-guide.md`, `andy-davis-vs-tiedtke.md`.

---

## Final-render validation: Movie Render Graph and Accumulation DOF

**Gated on:** Dylan building his final render config (not started as of 2026-09-25). He plans to use
**Accumulation DOF** in finals, or at least try it. Propose this when he gets there, or earlier if he
asks for a test render.

**Why.** Every verification so far was PIE, Python readbacks, or one short scratch MRG render. The
final look will run under settings nothing here has been tested with.

**What exists.** The Bokeh block's **Drive Accumulation DOF** (since v0.3.2) feeds Epic's experimental
Accumulation DOF component on the same camera: a procedural N-gon iris from blade count and
curvature, plus spherical aberration and coma. It has never been checked in a real render.

**What to test**, on one short shot in the host project's final-style graph, scratch assets in
`/Game/Claude/`:
1. **Accumulation DOF + lens.** Does the iris texture show? Do cat's eye (barrel) and Petzval swirl
   still happen? They are post-process DOF settings, which Accumulation DOF may bypass. If it does,
   say so in the tooltips and decide whether to feed it barrel/swirl another way. Does distortion
   and the image circle compose correctly on top?
2. **Temporal samples 4-8** (the planned finals setting): a Dynamic overscan that changes mid-frame
   is cached per output frame (`sequencer-integration.md`). Prefer Fixed overscan for these renders
   and confirm there are no pops.
3. **TSR render mode in MRG** at overscan up to 2, plus the double-overscan note in `architecture.md`.
   TSR mode also crashes PIE (the TSR entry below), so check it only in MRG.
4. **Cost.** Accumulation DOF is quoted at 3-10x per frame. Measure it with the lens effect on and off.

**What Dean Yurke's UE 5.8 Accumulation DOF video already answers** (summarised in the host
project's `06-transcript-map.md` section 5, with timestamps; his observations, not Epic docs):
- **Petzval swirl and cat's eye do not work under Accumulation DOF.** That largely answers test 1:
  on a camera using it, the plugin's swirl and barrel settings do nothing. The iris texture is the
  channel that does work, and it is the one the plugin already drives. Make the Bokeh tooltips say
  so, and consider showing a Notes line when the component is present.
- Its **Bokeh Texture** takes a lens-kernel image. Set **Bokeh Softness** toward 0 or the shape
  washes out. Check the plugin's iris texture survives the softness default.
- **Anamorphic** uses the camera's own Squeeze Factor plus a crop, so it should follow what Match
  Camera sets (now that the Match fix is in).
- It reads **dimmer**, so he uses manual exposure and compensates. Check the plugin's vignette on
  top does not double the darkening.
- **Auto Activate** on the component decides whether MRG renders it at all, which is handy for A/B.
- His graph: Sampling Method node with temporal samples 5 (temporal count lives there, not on the
  Deferred Renderer), TSR on, spatial samples 1, shutter Frame Open. Cost roughly 3-10x per frame.
  Only the ratio transfers to this rig (CPU-bound).
- He also shows `r.LensDistortion.Panini.D/.S`, Epic's own barrel warp. **Never combine it with a
  Dynamic Lens camera**: it would distort on top of the lens profile.
- His MRG build guide (the other Yurke video, section 2) is the node-by-node template for a final
  graph.

**Host-project context:** CitySample keeps its render knowledge in
`.claude/refs/final-render-config/` (MRG Python gotchas, measured performance, an Accumulation DOF
transcript). Read its `README.md` and `01-mrg-gotchas.md` before scripting any graph.

## Cooke FFi anamorphic zoom - built 2026-09-24, needs Dylan's eyes

`DL_AD_Cooke_FFi_Zoom` (profile `DLP_AD_Cooke_FFi_Anamorphic`, `presets.json` section
`anamorphic_profiles`, importer `dl.import_anamorphic_profiles()`). Profiles with `AnamorphicRows`
switch the component to Epic's `UAnamorphicLensDistortionModelHandler`; the 14 parameters are
interpolated across focal length, pixel aspect forced to 1.8 per `.claude/refs/andy-davis-vs-tiedtke.md`.
**Not visually verified** (the automated PIE harness is unreliable, see below): check the direction of
the distortion against `DL_T_Cooke_FFi` at 50 and 135 mm, and the 50 mm lurch. `dl.export_catalogue()`
still assumes K1+K2+K3 in `_parametric_edge_shift` and needs an anamorphic branch.

---

## Viewport sharpness on fisheyes (per-view resolution past Epic's 2x)

**Gated on:** Dylan, who deferred it on 2026-09-25 ("delay the render sharpness thing"). Nothing technical.

**Why.** Fisheyes past overscan 2 are soft in the centre in the viewport and PIE only: Epic clamps the
resolution fraction to 2, and Fit and Scale magnify the centre further (worst measured ~49%). Movie Render
Graph is already uncapped on the Post Process Material path, so finals are not affected. Numbers in
`overscan-and-image-circle.md` ("Resolution cost of fisheyes").

**The work.** A scene view extension overriding `SceneViewInitOptions.OverscanResolutionFraction` per view for
Dynamic Lens cameras (`SetupView` for PIE, `BeginRenderViewFamily` for the level editor). Exposed as a
per-camera setting (discussed: Viewport Sharpness Fast / Full, maybe a project default). Cost `(O/2)^2`.
Dylan was unsold on the UI shape; propose again before building.

---

## Vertical squeeze for fisheyes (optional look)

**Gated on:** a shot that asks for it. Dylan agreed on 2026-09-25 to skip it until then.

**What.** A round fisheye filling a wide frame crops the circle's top and bottom. At 8 mm, Scale 1.1
the frame shows 42 deg up while the raw render has 74 deg. A vertical squeeze of the fisheye map would
fit that band in, at the cost of making round things oval: an undesqueezed-anamorphic look. A
*radial* squeeze was ruled out, because the frame corners already sit at ~82 deg against a ~83 deg
render ceiling. About an hour's work: a Y factor on the fisheye plane in the `DriveProjection` bake
and in `NeedFor`.

---

## Hold the finished map on the ST-map path too

**Gated on:** nothing. It is small; do it with the next C++ batch.

`DriveProjection` now holds the last finished lens file while a rebuilt one's derived data is in
flight (`architecture.md`, gotchas). `DriveSTMap` still shows zero displacement for ~2 frames on a
preset switch. At overscan <= 2 it is barely visible, but it is the same bug. Reuse
`DynamicLensLensFileReady` and the Shown* state.

---

## Two quality bugs on the fisheyes, unrelated to distortion

**Gated on:** nothing. Both are small and both are worth doing whatever else happens.

**Near plane eats the rim.** Clipping is on view-space `Z = d*cos(theta)`, not ray distance, so at the
default 10 cm near plane everything nearer than 0.64 m *along the ray* is clipped at 81 deg off-axis -
2.87 m at 88 deg. Set `UCineCameraComponent::CustomNearClippingPlane` (`ClampMin = "0.00001"`,
`CineCameraComponent.h:87`) to about a millimetre on `DL_L_*` cameras. Reversed-Z with infinite far
handles it. This gets worse if the overscan ceiling goes up, so do it first.

**LOD - re-diagnosed 2026-09-24 (late), now gated on Dylan.** The "6x coarser" was measured against a
90 deg render; against the same camera without overscan, **Nanite is not coarsened** (O <= 2, scaled
resolution) and **discrete static-mesh LOD is coarsened by exactly O** - 2x at O = 2, on every preset.
Numbers and code references in `overscan-and-image-circle.md`. In a Nanite-heavy level like CitySample
this barely shows.

*The fix, and why it is not built:* divide `FSceneView::LODDistanceFactor` by the applied resolution
fraction for views of a Dynamic Lens camera. `r.StaticMeshLODDistanceScale` would do it but is global
(every viewport, particles, ray tracing), so it needs a per-view hook - a small scene view extension.
`SetupView` is the natural place, but in the level editor it runs *before* the viewport sets
`View->ViewActor` (`EditorViewportClient.cpp:1650` vs `LevelEditorViewport.cpp:2563`), so the
extension cannot tell which camera a view belongs to. The workable spot is `BeginRenderViewFamily`,
which needs a `const_cast` on the family's views before the renderer copies them. It works on paper
for the editor, PIE and Movie Render Graph (all three set `ViewActor`), but it is a hack in the render
path for a small gain, and the result can only be checked visually (LOD colouration view mode).
**Ask Dylan whether it is worth it** before building.

## Panavision C Series 20 mm edge smear (tiedtke ST-map clamp detection)

**Gated on:** nothing but a build/restart slot. Seen 2026-09-21 on a Black Eye camera in a film
project, preset `DL_T_Panavision_C_Series` at 20 mm with Match Camera To Profile: vertical smear
down the right edge, a dark strip down the left edge, smeared bottom-left corner.

**What was ruled out.** Overscan is sufficient - the component read needed 1.06 against applied
1.08 (Dynamic, ceiling 1.5). The image circle radius was 1.41 half-widths, well past the corners of
a 2.39 frame, so no mask covers the edge either.

**Working theory.** The 20 mm map is the widest in the set, and tiedtke's maps clamp to [0,1] where
the source leaves the frame - widest on the left and right of a 2x anamorphic, which is where the
artifacts sit. `BuildExtendedSTMap` in `DynamicLensLibrary.cpp` is meant to detect those clamped
bands and extrapolate over them; a needed overscan as low as 1.06 on the widest map suggests the
clamp is *not* being detected here, so the smear is sampled straight through and the overscan is
measured off clamped texels. Same class of bug as the earlier Cooke FFi edge smear.

**Checked 2026-09-24, not reproduced.** A plain CineCamera in `/Game/DynamicLensTest/L_DLTest`
(CitySample), 23 x 18.66 mm at 2x squeeze, 20 mm, f/8, PIE: both edges clean. What was learned:

- **The 20 mm and 30 mm maps are the same data** in tiedtke's pack (as is Panavision E 60 = 50),
  so "step to 30 mm" cannot tell map from preset. See `.claude/refs/andy-davis-vs-tiedtke.md`.
- The clamp bands are clean hard clamps, 40-75 texels wide per row on a 3656-wide map, with a
  linear ramp straight into valid data. No soft ramp, so the missing guard band (the comment in
  `BuildExtendedSTMap` mentions one, the code has none) is not what bites here.
- The maps are half-float: near 1.0 the step is 1/2048 = 0.00049, just under the 0.9995 clamp test,
  so the last valid texel on the right is sometimes marked clamped. One texel; harmless.
- So the smear is probably specific to that camera: Black Eye rig, its crop, or a filmback that
  Match Camera To Profile did not actually set. **Needs the original shot** to go further.

**Next steps.**
1. Get the camera settings from the film project's shot (filmback, crop, squeeze, overscan mode).
2. Reproduce with those numbers on `DLTest_Cam`.
3. Fix goes in `BuildExtendedSTMap` (clamp detection or guard band). C++, so build, then install on
   Dylan's next restart per `.claude/rules/updating-the-plugin.md`. Confirm on the Cooke FFi too.

## ST-map and projection distortion collapse to a centre smear in automated PIE

**Harness-only (Dylan, 2026-09-24): his Panavision C Series renders are fine.** Keep this for anyone
automating visual checks: by the end of the session every preset collapsed, parametric included.

**Symptom.** In PIE, a `DL_T_*` preset collapses the frame into a radial smear of the centre after
10-40 s; a `DL_L_*` projection preset does it from the first frame. `Apply Distortion` off shows the
scene fine; a parametric preset in the same session renders fine; switching back breaks again.
Re-applying the preset or forcing a new extended map does not fix it. **Bisected: the code as of
`842ee8b` (before 2026-09-24) does exactly the same**, so it is not from the bokeh or fisheye work.

**Ruled out:** GC (forced `obj gc`), dynamic resolution (off), frame rate (steady ~100 fps), the
image-circle MID (off, still broken), Epic's derived-data jobs (nothing in `LogCameraCalibrationCore`
at Verbose). Both the parametric and the ST path feed the same `M_DistortionPostProcess` MID, so the
difference is in what Epic's ST-map Lens File writes into the handler's displacement maps.

**Why it is the harness.** The machine was sitting on the Windows lock screen the whole time (seen
in a desktop capture at 19:29), the editor was never focused, and every look was a `HighResShot`. Frequent screenshots kept a `DL_T_*` healthy for 30 s+.

**Harness notes for whoever picks this up** (all in `/Game/DynamicLensTest`, CitySample):
- `L_DLTest` has `DLTest_Cam` and a sphere grid for bokeh. Load it in its own call; duplicating and
  loading a map in one call trips an Unreal GC assert.
- The PIE pawn falls and world partition unloads the showroom: pin it behind the camera.
- Legacy Movie Render Queue renders `DLTest_Cam` with **no** lens effect in either render mode
  (verified in-render: the component, blendables and view target are all correct). Movie Render
  Graph (`MRG_DLTest`, a copy of `zz_dltest_MRG` writing to a scratch folder) does apply it, but the
  showroom streams out there too. Save the level before any render: renders read the saved map.
- `read_render_target_raw` returns nothing for the handler's RG16F maps; a copy-to-RGBA32F via a
  material also read zeros. Reading the displacement maps back still needs a working method.

## TSR render mode crashes PIE - an Unreal 5.8 bug, not ours; needs Dylan's call

**Re-tested 2026-09-24 (late): the guard does not hold, and the plugin is not the cause.** In a
throwaway map, PIE viewing through a camera in TSR mode asserts `InTexture.IsValid()`
(`ScreenPass.inl:171`, all frames in `UnrealEditor-Renderer.dll`) within a frame or two. Bisected:

| Setup, PIE view target | Result |
|---|---|
| TSR, `DL_AD_Master` (parametric), overscan 1.02 | crash |
| TSR, `DL_AD_ARRI_Signature` (ST map), overscan 1.00 | fine |
| TSR, same ARRI preset forced to Fixed overscan 1.1 | crash |
| TSR, three ST / projection presets, `r.MotionBlurQuality 0` | crash (so not motion blur) |
| **stock CineCamera, no Dynamic Lens, `Overscan` 0.1 + `bCropOverscan`** | **crash** |
| stock CineCamera, `Overscan` 0.1, `bCropOverscan` off | fine |

So `bCropOverscan` with any overscan crashes a plain PIE game viewport in 5.8. TSR mode has to set it
(Epic's TSR lens distortion renders the overscanned frame and crops it), so any TSR-mode camera with
overscan above 1 crashes PIE. Movie Render Graph takes a different path and rendered TSR mode fine
on 2026-09-24. The data type never mattered; the earlier "ST-map or projection" framing came from
those presets being the ones that overscan.

**Options, Dylan's call:** (1) leave it: use Post Process Material for PIE and TSR for Movie Render
Graph renders, and say so in the Render Mode tooltip; (2) have the component skip `bCropOverscan` in
a PIE/game viewport - but MRG also renders from a PIE world, and its TSR path is the reason TSR mode
exists, so this needs a reliable "is this an MRG render" test first; (3) report it to Epic with the
stock-CineCamera repro above. Nothing lost in the tests - every crash was on an unsaved throwaway map.

## Match Camera To Profile was silently undone - fixed 2026-09-24 (late)

It wrote the filmback and squeeze, then called `ClearEffect()`, whose `RestoreLook` put back the bokeh
layer's backed-up squeeze and sensor width. So whenever the squeeze changed while the effect was
running - the **Match Camera** button pressed after a preset switch, or any script - the match was
reverted. Preset changes through the dropdown, the browser and A1/A2 clear first, so they were fine.
Now it clears before writing. Measured: `DL_AD_Cooke_FFi_Zoom` reached from a spherical preset went from
squeeze 1 / needed overscan 1.41 to squeeze 1.8 / 1.05. **Possibly the Panavision C Series smear**
(entry above): a Black Eye camera where Match Camera was pressed by hand would have kept the old
filmback and squeeze. Ask Dylan how that camera was set up before chasing `BuildExtendedSTMap`.

## Circle Coverage, and anamorphic image circles - built 2026-09-24 (late), needs Dylan's eyes

Image Circle > **Size = Coverage** with **Circle Coverage**, and the 1/squeeze lens-circle ellipse.
What was built and verified is at the top of the Coverage section in `image-circle-guide.md`. Not
seen rendered. Look at: the 8 mm and 4 mm at Coverage 0.5 / 1.0 (the magnified fisheye past the lens
field is new picture), and an anamorphic at Coverage 0.8. `DLP_AD_Cooke_FFi_Anamorphic` could now be
given a real image circle.
