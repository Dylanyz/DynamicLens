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

## The image circle / data mask

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
