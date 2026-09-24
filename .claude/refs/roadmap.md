# Roadmap — work that is ready but deliberately waiting

Things that have been investigated, are worth doing, and are **not** being done yet, each with
what it is gated on. This is not a wish list: an entry only belongs here once the research is
done and someone could pick it up and build it.

**How to use it.** When the gate on an entry has cleared, *propose it to Dylan* — do not just
start. When you finish an entry, delete it from this file rather than marking it done; git history
is the record.

---

## Where this stands — handoff, 2026-09-24

The lens-character work below came out of one long investigation (2026-09-19). A fresh agent picking
it up needs these five things before touching anything.

**1. Three entries are waiting on a decision from Dylan, not on research.** Do not start them.

| Entry | The question he has to answer |
|---|---|
| The `DL_L_*` fisheye rework | Cube source (real 180 deg+, costs SSR / motion blur / Lumen-on-HWRT) or stay one-faced and fit the projection to the circle? He was weighing it and had not decided. |
| Raise the overscan ceiling to 4 | Go-ahead, plus: accept a 2x softer centre for free, or keep it sharp for 4x the pixels? |
| Scratch assets | Still unanswered from 2026-09-19: delete `/Game/Cinematics/_render/zz_dltest_MRG` and the 11 test clips in the CitySample project's `Saved/MovieRenders/dltest/`? **Nothing has been deleted.** Ask before doing so; `Saved/` is off limits regardless (`CLAUDE.md` hard rule 2). |

**2. Two entries are not gated on anything** — the near-clip and LOD fixes, and the Panavision C
Series smear. The near-clip and LOD work is the recommended starting point: it is small, it helps
whichever way the fisheye decision goes, and it may change how the fisheyes look enough to inform
that decision.

**3. Restore points exist, and the presets get changed in place.** Dylan asked for in-place changes
rather than `_v2` variants, with commits first so it can be reverted: DynamicLens `aa13e04`,
CitySample Diversion `dv.commit.48`.

**4. Do not touch `DL_L_PoorThings_Petzval_58` or `_85`.** Dylan likes the swirl. They are
parametric, not projection, so none of this work reaches them — keep it that way.

**5. You cannot judge any of this from the level viewport.** Piloting a CineCameraActor does not
apply the camera's post-process in the CitySample project — verified with a saturation override, so
it is not DynamicLens-specific. A piloted `HighResShot` shows no distortion and no image circle at
all. Use PIE (`editor_request_begin_play`, then `set_view_target_with_blend` to the camera) or Movie
Render Graph. Two further traps: the editor window must not be minimised or `HighResShot` silently
produces nothing, and world partition streams around the *player pawn*, so a camera parked far from
the pawn renders an empty world with only the skydome. The component itself does tick in the editor
with no viewport involved, so `last_overscan_factor`, `needed_overscan_factor` and
`image_circle_radius` can be read straight off it while stepping presets — that is how the audit
table in `.claude/refs/overscan-and-image-circle.md` was built, and it is the cheap way to re-check
any preset.

There is a `DLTest_Cam` CineCameraActor with a Dynamic Lens component saved into `L_ViceCity` for
exactly this. It is committed in `dv.commit.48`.

**Read first:** `.claude/refs/overscan-and-image-circle.md` for how the maths works today and what is
measurably wrong with it, then `.claude/refs/wide-field-source.md` for what Unreal can and cannot do
past 90 deg off-axis. `Tools/data/research/lanthimos-lenses.md` has the provenance of every `DL_L_*`
number, measured versus assumed.

**6. The Preset Browser is installed** (2026-09-24) and waiting only on Dylan's review - see the
next section.

---

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

**Requested addition: hide presets** (Dylan, 2026-09-24). For lenses he doesn't love but won't delete:
a Hide action on each row (next to the favourite star), hidden presets drop out of every view, a
**Hidden** group/section lists them with Unhide, and a **Show hidden** filter toggle brings them back
inline. Store them like `Favourites`: a set of package names in the browser's per-user config section
(`DynamicLens.PresetBrowser`, `SaveConfig`), never on the preset asset, so hiding is personal and
survives `dl.import_presets()`. **Bonus, if cheap:** also hide them from the component's own Preset
dropdown. That is an asset picker, so it needs a detail customization with an `OnShouldFilterAsset`
that reads the same config set - `DynamicLensComponentDetails` already exists to hang it on. Fold
into the review round below so it costs no extra restart.

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

**LOD and Nanite coarsen by 6x on the porthole.** Both derive one scalar from *on-axis* pixel density
(`SceneManagement.cpp:939`, `NaniteShared.cpp:197-202`), proportional to `1/tan(halfFOV)`. The 4 mm at
O=2 renders 161.7 deg wide, so every mesh picks LOD as if 6.2x further away and Nanite clusters are
6.2x coarser - uniformly, including at the rim where the source already has 5-11x surplus pixels.
The 8 mm at 144.4 deg is 3.1x. Compensate per camera with `r.StaticMeshLODDistanceScale` and Nanite's
LOD scale factor. Needs a value that tracks the actual FOV rather than a magic number.

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

## TSR render mode + ST-map or projection preset crashed the renderer in PIE

**Guarded 2026-09-24, verify.** Asserted `InTexture.IsValid()` in `ScreenPass.inl:171`, both when
switching Render Mode mid-PIE and simply starting PIE with a camera saved in TSR mode. Pre-existing
(the `842ee8b` build crashes on PIE start). `ApplyRendering` now skips the SVE hand-off until both
handler displacement maps have an RHI texture; a TSR-mode Movie Render Graph render ran without
crashing afterwards. Re-test the PIE-start case interactively.

## Match Camera To Profile did not take when called from script in PIE

Seen 2026-09-24: `match_camera_to_profile()` on the PIE copy right after setting the preset left the
filmback at 23.76 x 18.66, squeeze 1. May be ordering (called in the same frame as the preset
change) rather than a bug. Check before relying on it in any automation.

## Circle Coverage control, and anamorphic image circles

From `.claude/refs/image-circle-guide.md` (2026-09-24). **Circle Coverage** = circle diameter over the
delivered frame's diagonal, on the sensor before desqueeze, crop applied; a Physical/Coverage mode that
turns fit on and, past the lens's field limit, enlarges the fisheye image rather than the circle.
Anamorphic lens circles need an ellipse at 1/squeeze - today the circle is measured on the desqueezed
width, so a real one would be drawn 2x too narrow (why `DLP_AD_Cooke_FFi_Anamorphic` has none).
