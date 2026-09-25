# Roadmap — work that is ready but deliberately waiting

Things that have been investigated, are worth doing, and are **not** being done yet, each with
what it is gated on. This is not a wish list: an entry only belongs here once the research is
done and someone could pick it up and build it.

**How to use it.** When the gate on an entry has cleared, *propose it to Dylan* — do not just
start. When you finish an entry, delete it from this file rather than marking it done; git history
is the record.

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

**Dylan's test list, in this order** (given to him 2026-09-24):
1. Preset Browser (Window > Cinematics): hide a lens, check the Hidden section, then check it is gone
   from the component's Preset dropdown - the one piece automation could not reach.
2. Circle Coverage: `DL_L_PoorThings_8mm`, tick the Image Circle override, Size = Coverage, try 0.5 and 1.0.
3. Anamorphic circle: a `DL_T_*` preset at Coverage 0.8 should draw an oval.
4. Lens kit: Kit = `DLK_L_PoorThings`, key the CineCamera focal at 8 then 58, scrub.
5. Match Camera: spherical preset -> a `DL_T_*` with auto-match off, press Match Camera, squeeze should become 2.

**The three open decisions - Dylan said "idk", so these defaults stand until he says otherwise:**
TSR PIE crash -> leave it, use Post Process Material for PIE and TSR only for Movie Render Graph
(still to do: say so in the Render Mode tooltip, one line, fold into the next build); static-mesh LOD
hack -> skip; Panavision C Series smear -> no action unless it recurs, then try Match Camera once.

**1. Built, installed, committed - waiting on Dylan's eyes, not on code:**

| What | Where to look |
|---|---|
| Force Bokeh Quality (Petzval fix; editor was at High scalability) | `architecture.md`, verified in PIE |
| `DL_L_*_Fit` fisheyes: continuous projection K, fit-to-circle, overscan ceiling 4, 1 mm fisheye near clip | the fisheye entries below; originals untouched except the near clip |
| `DL_AD_Cooke_FFi_Zoom` (3DE4 anamorphic, continuous 32-135 mm) | the Cooke entry below |
| Preset keyable in Sequencer (`SetPreset`) | `sequencer-integration.md`, verified by scrubbing |
| Image Circle > Size = Coverage, anamorphic lens-circle ellipse, Match Camera fix | the last two entries |
| Sequencer phase 2 (locked-focal note, tooltips) and phase 3 Lens Kits (`DLK_L_PoorThings`, `DLK_L_Favourite`) | `sequencer-integration.md` status block; verified from Python, not scrubbed in Sequencer |
| Preset Browser, plus `DynamicLens.PresetBrowser` console command, plus hide presets | next section; its UI has now been seen by automation, not by Dylan |

**2. Decisions only Dylan can make:** the six `DL_T_*` overscan ceilings (`todo.md`); the scratch
assets from 2026-09-19 (`/Game/Cinematics/_render/zz_dltest_MRG`, `Saved/MovieRenders/dltest/` -
nothing deleted, `Saved/` off limits); the cube-source question for a real 180 deg fisheye
(`wide-field-source.md`); whether the static-mesh LOD fix is worth a render-path hack (the LOD entry
below - re-diagnosed, much smaller than first written); what to do about TSR mode crashing PIE (an
Unreal 5.8 bug with `bCropOverscan`, reproduced on a stock CineCamera - entry below).

**3. Good next work that needs no eyes:** none left; wait for Dylan's review. Editor restarts were
allowed only while he was away (2026-09-24) - **ask again before any restart from now on.**

**4. Do not trust automated screenshots.** The PC sits on the Windows lock screen when Dylan is away.
Automated PIE `HighResShot`s of ST-map/projection presets collapse into a centre smear (harness
artefact - his real renders are fine), legacy Movie Render Queue shows no lens effect, and Movie
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

## Preset Browser — installed, waiting on Dylan's review

**Gated on:** Dylan looking at it. The code is done and in HEAD; nothing
about it is waiting on a decision.

**What it is.** A dockable *Lens Presets* window (Window > Cinematics) plus a **Browse** button in
the Dynamic Lens component's Preset row. Filters by maker, spherical/anamorphic, data type,
breathing, image circle and prime; six sort modes; four groupings; search; favourites; recents; a
per-row curvature bar; and a detail pane carrying each lens's full `Source` attribution verbatim.
Clicking a lens applies it to every selected camera in one undo transaction. Built because the flat
alphabetical dropdown stopped scaling at 60 presets, and because the `DL_*` prefixes encode
provenance rather than optics, so spherical and anamorphic can never sort together by name.

**State, 2026-09-24 (late):**

| | |
|---|---|
| Code | in HEAD. Both modules compile clean. |
| Installed DLL | 2026-09-24, matches HEAD (it also carries the Force Bokeh Quality fix) |
| Preset tags | all 60 re-saved; `DL.Distortion` holds the curvature metric (CP.3 0.051, Master Anamorphic 0.128, Optimo 0.300, Favourite 6mm 0.606) |
| The UI | **has never been looked at.** Written blind; nobody has seen it render. |

**Next action:** open **Window > Cinematics > Dynamic Lens Preset Browser** and get Dylan's eyes on
the layout. Expect fixes; each one costs a build plus a restart, so gather them all before
rebuilding. Once it is signed off: document it (`todo.md`) and push it to a release (Dylan,
2026-09-24: "push preset browser to release once it's ready").

**Hide presets - built 2026-09-24 (late), installed.** An eye on every row hides a lens; hidden ones
fold into a **Hidden** section at the foot of the list (click the header to open it, the eye unhides),
and **Show hidden inline** puts them back in the list, dimmed. They also drop out of the component's
Preset dropdown (an `SObjectPropertyEntryBox` with `OnShouldFilterAsset` in
`DynamicLensComponentDetails`) and of A1/A2 stepping. The set lives in
`DynamicLensHiddenPresets` (`DynamicLensTypes.h`), stored as `Hidden=` package names in the browser's
`[DynamicLens.PresetBrowser]` section of `EditorPerProjectUserSettings.ini` - never on the asset, so it
is personal and survives `dl.import_presets()`. **Verified in the live editor via the Slate inspector:**
hiding, the count ("61 of 65 - 1 hidden"), the folded section, the dimmed row, unhiding, and stepping
skipping a hidden lens both ways. **Not verified:** the Preset dropdown filter (automation could not
reach the component's details row) - check it on Dylan's review.

**Two traps, both hit already:**

- `DynamicLens.uplugin` declares `DynamicLensEditor`, and this repo *is* the live plugin, so
  relaunching without installing raises a "Missing Modules: DynamicLensEditor" dialog at startup.
- Tags are written only when an asset is **saved**. `dl.import_presets()` touches just the 19 presets
  in `presets.json`; `dl.resave_presets()` covers all 60 without re-importing anything.

**Design decisions worth not re-litigating:**

- **The browser reads Asset Registry tags and never loads a preset to display one.** Presets hard-
  reference their profile and ST-map profiles hard-reference their textures, so loading the
  catalogue to read labels would pull ~107 MB of ST maps into memory. `DL.*` tag names live in
  `DynamicLensTags` in `DynamicLensTypes.h` so the two modules cannot drift.
- **Applying always goes through `UDynamicLensComponent::ApplyPreset`**, the same path the A1/A2
  buttons use, so the Match Camera checkboxes mean the same thing however a preset was picked.
- **The Browse button is inline in the Preset row, not a full-width row beneath it.** A custom row
  added to a category always lands after every property in that category, which would have put it
  below Amount Multiplier, nowhere near the preset.
- **`DL.Distortion` is curvature, not overscan** — the worst departure from a straight mapping, as a
  fraction of half the frame. Overscan was the obvious first choice and is wrong: it measures how far
  a map's samples fall outside the frame, which is an artefact of how each author scaled their maps.
  All 19 Andy Davis spherical sets reported exactly 1.0 under it. Two traps when measuring
  curvature off an ST map, each of which yields a plausible-looking number that means nothing:
  `ReadSTMapSamples` walks rows top-down while the maps are BottomLeft origin (unflipped, every lens
  reads ~1.9, nearly a whole frame, and they all look alike), and the maps clamp to [0,1] where the
  source leaves frame, so those pinned samples must be dropped or they swamp the peak. Parametric
  profiles need `MakeMonotonic` or a large K3 runs away past the corner (Zeiss Supreme read 0.89
  against 0.05 with it).

**Unverified, because the UI has never run:** layout and spacing, arrow-key stepping, favourites and
recents persistence, grouping, every filter, and whether the anamorphic filter reads `DL.Squeeze`
correctly on the tiedtke sets.

---

## Cooke FFi anamorphic zoom - built 2026-09-24, needs Dylan's eyes

`DL_AD_Cooke_FFi_Zoom` (profile `DLP_AD_Cooke_FFi_Anamorphic`, `presets.json` section
`anamorphic_profiles`, importer `dl.import_anamorphic_profiles()`). Profiles with `AnamorphicRows`
switch the component to Epic's `UAnamorphicLensDistortionModelHandler`; the 14 parameters are
interpolated across focal length, pixel aspect forced to 1.8 per `.claude/refs/andy-davis-vs-tiedtke.md`.
**Not visually verified** (the automated PIE harness is unreliable, see below): check the direction of
the distortion against `DL_T_Cooke_FFi` at 50 and 135 mm, and the 50 mm lurch. `dl.export_catalogue()`
still assumes K1+K2+K3 in `_parametric_edge_shift` and needs an anamorphic branch.

---

## The DL_L_* fisheye rework

**Stay-one-faced half built 2026-09-24** as the `_Fit` presets (continuous K, Amount reaches the projection,
fit-to-circle, ActiveMask readout, near clip, overscan ceiling 4). Still open: Circle Coverage, and the
cube-source alternative below.

**Gated on:** Dylan's decision, now that the research is in
(`.claude/refs/wide-field-source.md`). UE can supply more than 81 deg off-axis, but only by rendering
six faces: `USceneCaptureComponentCube` is the one mechanism that works in the level viewport, PIE
*and* Movie Render Graph. It costs a second `FSceneRenderer`, and it loses screen-space reflections,
DFAO history and motion blur, with Lumen needing hardware ray tracing to survive the face seams.

So there are two different shapes this work can take, and they want different code:

- **Cube source.** The lenses become their real projections - a 220 deg Nikkor actually 220 deg - and
  the image circle falls out of the physics with no compromise. Bigger job, and the fisheye presets
  would render differently from every other preset in the plugin.
- **Stay one-faced.** Everything below still applies, and the projection gets fitted to the circle
  because 81 deg is all there is. Cheap, self-contained, no rendering features lost.

Dylan was weighing these as of 2026-09-19 and had not decided. Do not start either without an answer.

**Why.** Measured 2026-09-19: every projection preset shows 65-81 deg of field and 1.25-1.48x
angular compression regardless of focal length, so a 4 mm 180 deg fisheye and a 10 mm look the same.
The full diagnosis, with numbers, is in `.claude/refs/overscan-and-image-circle.md`.

**The choice.** Unreal can source only ~81 deg off-axis from one rectilinear render at the 2.0
overscan ceiling. Either (a) put those 81 deg where the real lens would put them, which leaves the
picture stopping well short of the image circle - today's behaviour, porthole 23% small and
elliptical - or (b) fit the projection so 81 deg fills the circle at its real physical size, which
makes the circle correct and round at the cost of bending harder than the real lens does. Recommended
(b): the thing being matched is a film frame, not a test chart. Moot if the research finds a real
>90 deg source.

**The work, once unblocked:**

1. **A one-parameter projection family** on `UDynamicLensProfile`, replacing the four-way
   `EDynamicLensProjection` enum with a continuous `k`: `r = (f/k)tan(k*theta)` for k>0,
   `r = f*theta` at k=0, `r = (f/|k|)sin(|k|*theta)` for k<0. k=1 rectilinear, 0.5 stereographic,
   0 equidistant, -0.5 equisolid, -1 orthographic. Keep the enum as a preset-authoring convenience
   that writes k. This is what makes a tunable "how fisheye" possible at all.
2. **Make `Distortion > Amount` reach `DriveProjection`.** It currently only multiplies
   Brown-Conrady coefficients (`FDynamicLensSettings::Evaluate`), so on every `DL_L_*` ultra-wide
   the Amount slider, the multiplier and the override block all do nothing. Blend k from 1
   (rectilinear) toward the profile's k, and allow past it for exaggeration.
3. **Image Circle > Coverage**, circle diameter over frame diagonal: 0.3 a tight porthole, 1.0
   exactly kissing the corners, above ~1.2 invisible. Replaces `Scale`, which currently appears to
   do nothing on these presets because it only moves the loser of the two masks.
4. **Report which mask is active** in the details panel. Today you cannot tell whether you are
   looking at the lens's image circle or the data limit, which is why this took a session to find.
5. **`DL_L_Favourite_10mm_Rect`**: fixed overscan 1.5, needs 1.62, so its frame edges are sourced
   from outside the render and smear. Independent of everything above - just raise it.

**Do not touch `DL_L_PoorThings_Petzval_58` or `_85`.** Dylan likes the swirl. They are parametric,
not projection, so nothing here reaches them - keep it that way.

**Change the presets in place, not as _v2 variants** (Dylan, 2026-09-19). Restore points for the
revert: DynamicLens `aa13e04`, CitySample Diversion `dv.commit.48`.

---

## Raise the overscan ceiling from 2 to 4

**Gated on:** Dylan's go-ahead. Nothing technical. This is the cheapest real improvement available to
the fisheyes and it is independent of the cube-capture question - do it either way.

**What it buys.** `theta_cap = atan(O * W / 2f)`, so raising the ceiling to 4 takes the shipping
fisheyes from **66.8-80.9 deg** to **77.9-85.4 deg** of field, and *grows* the image circle at the
same time (the 4 mm porthole goes 0.454 -> 0.479 half-widths, closing part of its 23% deficit). No
cube capture, no new rendering path. Full numbers in `.claude/refs/overscan-and-image-circle.md`.

**What it costs, and it is a choice.** Epic clamps `OverscanResolutionFraction` to `[1,2]`
(`CameraStackTypes.cpp:542`), which is the *only* thing that turns extra overscan into lost centre
resolution - the fisheye centre is otherwise sampled at exactly 1.000 at any overscan. So at O=4
either accept a centre 2x softer for free, or keep it sharp for 4x the GPU pixels. Make that visible
in the UI; do not let it be silent.

**Every ceiling that has to move together:**

| Site | Clamp |
|---|---|
| `DynamicLensComponent.cpp:401` | `Applied = FMath::Clamp(Applied, 1.f, 2.f)` |
| `DynamicLensComponent.cpp:569` | `O = FMath::Clamp(AppliedOverscan, 1.f, 2.f)` in `DriveProjection` |
| `DynamicLensComponent.cpp:663` | `CamOverscan = FMath::Clamp(AppliedOverscan - 1.f, 0.f, 1.f)`, written straight to `Cam->Overscan` |
| `DynamicLensTypes.h:766, 775` | `MaxOverscan` / `FixedOverscan` `ClampMax = "2.0"` (metadata) |
| `LensDistortionSceneViewExtension.cpp:667` | engine-side: `InverseOverscan` clamped `[0,2]` on the SVE path - a third 2 to clear |
| `DynamicLensComponent.cpp:600` | `Theta < HALF_PI - 0.01f`, a hard 89.43 deg cap inside the ST-map bake |

Line 663 matters most: it writes `Cam->Overscan` directly, bypassing `SetOverscan`, with its own
`[0,1]` clamp. `FMinimalViewInfo::ApplyOverscan` has **no** upper bound, so that assignment can
legitimately carry 3.0 for O=4.

**Three ways to keep centre resolution**, none needing an engine change: set
`bScaleResolutionWithOverscan = false` and raise primary screen percentage instead
(`kMaxResolutionFraction = 4.0f`, `SceneView.h:2277`); or render oversized in Movie Render Graph and
downscale, which is where these presets get finished anyway (ceiling: O=8 on a 1920 output needs
15360 wide, just inside the 16384 D3D12 limit); or accept the softness.

**Re-check the Movie Render Graph double-count** (`.claude/refs/architecture.md`) at the new ceiling.
It was diagnosed at 2.0 and nothing has verified it behaves at 3 or 4.

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
