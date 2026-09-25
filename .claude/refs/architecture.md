# DynamicLens architecture — read before changing C++

Every item here was paid for with a debugging session. Re-deriving them is expensive; contradicting
them usually reintroduces a bug Dylan already reported.

## The shape of it

`UDynamicLensComponent` sits on a CineCameraActor, ticks after its owner, and each frame:

1. **Resolves** settings: preset asset, overwritten by any ticked Override block on the component,
   scaled by the multipliers. `ResolveSettings()`.
2. **Evaluates** the profile at the current focal length, focus distance and f-stop →
   `FDynamicLensEval`. `FDynamicLensSettings::Evaluate()`.
3. **Drives distortion** down one of three paths, which produce a `FLensDistortionState` plus the
   overscan the distortion needs and the extent of usable image data.
4. **Applies the look**: image-circle material MID parameters, vignette, and depth-of-field.

Files: `DynamicLensTypes.h/.cpp` (data + maths), `DynamicLensComponent.h/.cpp` (the driver),
`DynamicLensLibrary.cpp` (ST-map extension), `DynamicLensModule.cpp` (startup fixups).

## The three distortion paths

| Path | Profile data | Driver | Used by |
|---|---|---|---|
| **Parametric** | Brown-Conrady K1/K2/K3/P1/P2 fitted per focal + focus | `DriveParametric` | `DL_AD_*`, most `DL_C_*` |
| **ST map** | One measured UV displacement texture per prime | `DriveSTMap` | every `DL_T_*` |
| **Projection** | Analytic fisheye (equidistant, equisolid, stereographic, orthographic) | `DriveProjection` | `DL_L_*` ultra-wides, the porthole |

All three feed Epic's **CameraCalibrationCore**: the component builds a *transient* `ULensFile`,
hands it the state, and lets Epic render the displacement maps. We do not write our own distortion
shader for the image itself, only for the image circle.

## Engine internals this fights

These are Epic behaviours that are not obvious and that the plugin works around. Do not "simplify"
the workarounds away.

- **`UCineCameraComponent::GetCameraView` overwrites `DepthOfFieldBladeCount` and
  `DepthOfFieldSqueezeFactor` from `LensSettings` every single frame.** Setting them on the
  post-process settings does nothing. Bokeh blades and anamorphic squeeze must be driven through
  `Cam->LensSettings`, and when squeeze is driven you must compensate `Filmback.SensorWidth` by the
  squeeze factor or the framing shifts. Back up and restore the originals.
- **Scalability below Cinematic silently kills every bokeh setting we drive.** High
  (`sg.PostProcessQuality 2`) sets `r.DOF.{Gather,Scatter,Recombine}.EnableBokehSettings=0` and
  `r.DOF.Scatter.BackgroundCompositing=1`. The last one matters most: bright highlights stop being
  scattered as sprites, and the sprite vertex shader is where Petzval stretch is applied regardless
  of bokeh shape. The post-process values still read correctly, so nothing looks wrong from the
  component's side. This was the 2026-09-24 "Petzval swirl stopped working" report: the editor had
  been set to High. `bForceBokehQuality` (default on) raises those four cvars, ref-counted across
  components, and restores them when the last camera releases them. Also note that Epic forces a
  plain circle, with no gather-side bokeh simulation, whenever `f-stop <= LensSettings.MinFStop`.
- **`UActorComponent::PreEditChange` unregisters the component** while a details-panel slider is
  being dragged, which kills the live preview. That is why `PostEditChangeProperty` applies
  immediately on `Interactive` change type, not just on the final value.
- **`ULensFile::EvaluateDistortionForSTMaps` refuses a filmback larger than its LensInfo sensor.**
  The sensor we hand it must cover the camera's whole filmback.
- **`BlendDisplacementMaps.usf` crops a larger sensor by a UV scale but does NOT rescale the
  displacement values.** So an ST map presented on a larger sensor under-distorts. The fix is to
  store displacement in *camera-frame units* in the extended map rather than map units, and to
  ignore Epic's own overscan estimate for extended maps.
- **`UCameraCalibrationSettings::DisplacementMapResolution` defaults to 256×256**, far too coarse
  for ST maps; it shows as soft, stepped edges. `DynamicLensModule` raises it to 2048 at
  `OnPostEngineInit` via reflection, only if the project still has the default. Override with
  `DynamicLens.DisplacementMapResolution`.
- **Black Eye camera actors** subclass `ACineCameraActor` and set FOV through `SetFieldOfView`,
  which writes `CurrentFocalLength`. The component adds `AddTickPrerequisiteActor(Owner)` in
  `OnRegister` so it reads the post-FOV value, not last frame's.

## Overscan

**The derivation, the measured numbers and the known failures are in
`.claude/refs/overscan-and-image-circle.md`.** What follows is the short version.

Distortion pulls image in from outside the frame, so the render must be wider than the frame. Too
little and the edges are empty; too much and you waste resolution and trigger a resize.

- **Dynamic mode** computes the overscan the current distortion needs, then quantises it to a
  **2% step with hysteresis**. Raw continuous overscan caused visible pops, because every change
  resizes the render target and resets temporal anti-aliasing.
- **Needed overscan is deliberately focus-invariant**: it is the maximum over the profile's whole
  focus range, evaluated at near, far and infinity. Focus pulls therefore never resize the target.
  Distortion still *breathes* with focus, which Dylan explicitly wants. His words: "I want that to
  happen. I just want it to be smoother." Do not remove breathing to fix a pop.
- `MaxOverscan` is a ceiling, not a target. Everything that needs to know "how far can we see"
  uses the ceiling, not the current value, so the mask does not swim as overscan steps.

### Movie Render Graph double-counts overscan on the post-process path

**Render Mode must be Temporal Super Resolution for anything rendered through Movie Render Graph.**
Post Process Material mode renders roughly `Overscan`x too tight in MRG while looking correct in the
viewport.

`ApplyRendering` sets `Cam->bCropOverscan = false` on the post-process-material path, because the
distortion material is what maps the overscanned source back to the frame. MRG reads that as
`ViewInfo.CropFraction == 1.0` and takes its `bCameraRequestsNoCrop` branch
(`MovieGraphDeferredPass::GetResolutionAndCameraInfo`): it leaves the accumulator at output size and
never centre-cuts, but `ReapplyOverscanPreservingEngineScaling` still expands the FOV *and* sets
`OverscanResolutionFraction = 1 + Overscan`. The overscan is applied twice on the way in and
consumed once by the distortion material, so the frame comes out about `Overscan`x tight. The
engine deliberately ignores `bScaleResolutionWithOverscan` here, so we cannot turn the second
application off from our side.

The SVE path sets `bCropOverscan = true`, which puts MRG on the branch it handles correctly:
accumulator enlarged by the overscan, `OverscanResolutionFraction` left at 1, centre-cut at write
time. Framing then matches the viewport exactly. It does **not** require TSR anti-aliasing in the
graph - verified with FXAA (2026-09-19, `ls_s1_demo1_mck_window`, `DL_L_Favourite_10mm`, fixed
overscan 2.0, 854x480). Whether the component should force SVE under MRP, or at least warn, is open.

## The image circle / data mask

Full treatment, including why the lens's own circle currently never wins on a `DL_L_*` preset:
`.claude/refs/overscan-and-image-circle.md`.

Two different things produce a dark edge, and the tighter one wins:

1. **The lens's physical image circle** — `Profile->ImageCircleMm` over the filmback width, times
   `ImageCircle.Scale`.
2. **The edge of what the render can actually show** — beyond it there are no source pixels and you
   get smeared garbage. This is not a circle: it is the distorted image of the overscanned source
   rectangle. The plugin models it as a **superellipse** whose exponent is solved by bisection from
   the distorted corner (`ValidExtents`, `ValidCorner`, `SolveSquareness`), computed every frame at
   the overscan ceiling.

An ellipse was tried first and clipped 2.47:1 corners, which is what produced the rim artefacts on
tiedtke lenses. A plain inscribed circle was tried before that and appeared suddenly rather than
sweeping in. The superellipse is the version that behaves like a real lens gate: it covers the
artefacts beyond what the lens can show and zooms at the same rate as the image.

The mask is drawn by a Custom HLSL node in `M_DL_ImageCircle`, rebuilt by
`dl.build_image_circle_material(force=True)`. It carries superellipse radius, radius wobble,
falloff-width wobble, two-octave polar value noise with depth/blur/detail, per-channel outer radius
for chromatic aberration, an 8-tap radial scatter glow, the vignette-like Fade, and an optional mask
texture. **If you change the HLSL you must rebuild the material**, and a rebuild orphans any live
MID, so `ClearEffect`/`ApplyLook` null it deliberately.

**Anything in that HLSL that samples the scene itself must go through
`ViewportUVToSceneTextureUV` then `ClampSceneTextureUV`.** The `UV` input is a `ScreenPosition`, i.e.
*viewport* UV, while `SceneTextureLookup` wants *buffer* UV. The two coincide only when the viewport
fills the whole scene-colour buffer, which is true in the editor and false under Movie Render Graph,
where camera overscan makes the buffer bigger than the frame. The Scatter taps passed viewport UV
straight through, walked off the valid rect in every MRG render, and read unwritten memory, which
came back as NaN/Inf and encoded as neon magenta/green speckle in a band around the rim - only in
renders, never in the viewport, which is why it survived so long (2026-09-18, `ls_s1_demo1_mck_window`).

## ST maps

tiedtke's maps are **clamped to [0,1] where the source leaves the frame** — wide bands at the
corners, thin at the edge centres. Sampling into a clamped band produces smears, which is what
"Cooke FFi has edge artifacts" was.

`BuildExtendedSTMap` fixes this by marking each texel valid or clamped, then extrapolating linearly
first along rows and then along columns into a transient extended map (1024 wide, `PF_G32R32F`)
that stores displacement in camera-frame units and is presented as a sensor Extend× larger. Needed
overscan is then measured densely around the extrapolated frame border, never taken from Epic.

The gradient baseline for extrapolation is 1.5% of the frame, not a few texels: a short baseline
picks up sampling noise and the clamp's own soft ramp. There is also a guard band that skips the
last few texels before the clamp begins.

## Distortion range handling

Outside a profile's measured focal range, `EDynamicLensRangeMode` decides:

- **Clamp** (default) — rescale the coefficients to the new focal: K1·s², K2·s⁴, K3·s⁶, P·s, where
  s = f / f_clamped. Applying a 25 mm lens's raw coefficients at a 6 mm field of view is what made
  `DL_C_Subtle` look "all weird" and put artefacts on the edge of `DL_C_Vintage`.
- **ClampRaw** — the old un-rescaled behaviour, kept because `DL_C_Vintage_Raw` depends on its look.
- **Extrapolate** — continue the fit. Use sparingly.

`RadialInverse` searches only the **rising** part of the distortion polynomial. A high-order fit
folds back on itself past the frame edge, and a naive bisection lands on the wrong root, which was
reporting a needed overscan of 4.0 below 12 mm.

## Gotchas that are easy to reintroduce

- **A new ST-map lens file shows zero displacement for ~2 frames.** Epic derives displacement
  asynchronously and `EvaluateDistortionForSTMaps` clears the maps while the job is in flight. On a
  fisheye at overscan 3-4 that flashed the raw, hugely overscanned render after every rebuild (preset,
  focal, Scale, overscan step). `DriveProjection` now holds the last finished lens file with its own
  overscan and circle until the new one reports back (`DynamicLensLensFileReady`, 30-tick timeout).
  `DriveSTMap` still has the short version of this on a preset switch (overscan <= 2, barely visible).

- Profile specs were once ignored for ST-map profiles because validity was tested with
  `IsValidProfile`; it must be `Profile != nullptr`.
- Textures duplicated during import exist in memory only until saved. `import_tiedtke` saves them.
- A rebuilt material leaves a parentless MID that blanks the whole post-process chain.
- Details panels sort `CallInEditor` buttons alphabetically, hence the `A1_`…`A4_` prefixes that
  keep Previous before Next.
- `ShowOnlyInnerProperties` on a struct with its own categories nests the group three deep. The
  Camera row is a plain struct row for that reason.
- Focal length shown in Profile Info was one step stale until it was read from the camera rather
  than the cached evaluation.
