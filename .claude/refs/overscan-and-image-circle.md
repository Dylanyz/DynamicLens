# Overscan and the image circle — what the maths actually is today

Written 2026-09-19 from a read of the code plus measurements on a test camera in Vice City. This is
the *current* behaviour, including the parts that are wrong. `architecture.md` has the short version;
this file has the derivation and the numbers.

## Overscan

Overscan is one number, `AppliedOverscan`, a multiplier on the rendered frame. The chain:

1. Each distortion path reports `NeededOverscan` — how far outside the frame it has to reach to fill
   the frame after distortion.
   - **Parametric**: `ComputeOverscan` walks the output frame border, inverts the radial polynomial
     at each sample and takes the largest source coordinate, in half-frame units.
   - **ST map**: measured densely around the extrapolated map border at import
     (`BuildExtendedSTMap`), not taken from Epic.
   - **Projection**: does not compute a need at all. It *returns whatever it was given*
     (`ProjectionNeededOverscan = O`), because the fisheye wants as much source as it can get and
     there is no radius at which it is "enough". This matters — see below.
2. `Applied` = `FixedOverscan`, or for Dynamic mode `NeededOverscan` rounded up to a 2% step with
   hysteresis (`DynamicApplied`). Quantisation exists because every change resizes the render target
   and resets TAA.
3. `ApplyRendering` writes `Cam->Overscan = Applied - 1` and `Handler->SetOverscanFactor(Applied)`.

**Ceiling of 2.0, and it is ours.** `Applied` is clamped to [1, 2] in `ApplyToCamera`, and again in
`ApplyRendering` where `Cam->Overscan` is clamped to [0, 1]. **Epic does not clamp overscan** —
`UCameraComponent::Overscan` carries `ClampMax="1.0"` but that is details-panel metadata only, and
`FMinimalViewInfo::ApplyOverscan` has no upper limit. Both of our clamps are a choice nobody has
revisited. See the roadmap entry.

`MaxOverscan` is a *ceiling, not a target*: everything that needs "how far can we see" uses the
ceiling, so the mask does not swim as dynamic overscan steps.

## The two things that darken the edge

`ApplyToCamera` builds one mask from whichever of these reaches the frame corner first:

1. **The lens's physical image circle.** `Eval.ImageCircleRadiusNorm = Profile->ImageCircleMm *
   ImageCircle.Scale / SensorWidthMm`, in half-frame-width units. This *is* rooted in the data
   sheet: a 14.5 mm circle on a 24.89 mm gate gives 0.583, and the measurement off a Poor Things
   still gives 0.587. Good agreement.
2. **The data limit** — the distorted image of the overscanned source rectangle, beyond which there
   are no source pixels. Modelled as a **superellipse** through the axis half-extents (`ValidExtents`)
   and the distorted corner (`ValidCorner`), with the exponent solved by bisection
   (`SolveSquareness`). Computed every frame at the overscan **ceiling**, so it exists continuously
   instead of switching on at the corners.

The tighter one wins, compared at the frame corner (`MaskCornerR`). Whichever wins also becomes the
radius at which vignette and cat's-eye are evaluated, so the falloff belongs to the picture you can
actually see rather than to a black corner.

For the projection path the "superellipse" degenerates to a plain ellipse: `DriveProjection` reports
`Rx`, `Ry` and `MinData` is handed a corner at 0.7071 of each, which forces the exponent to 2.

## Why the fisheyes all look the same

`DriveProjection` bakes an ST map from the fisheye image plane to the rectilinear render. For output
radius `r` it needs source radius `Ru = f·tan(θ)` where `θ = θ(r/f)` is the profile's projection.
The source only exists for `|x| ≤ O·W/2` and `|y| ≤ O·H/2`, so the largest field angle the system can
ever show is

    θ_cap = atan(O·W / 2f)

and the visible circle is drawn exactly there: `RadX = f·g(min(θ_capX, θ_max))`. Beyond it the map
falls back to identity, and the mask covers that (verified: the ellipse never overshoots the true
valid region, at any angle, for any current preset).

The consequence is that **`f` very nearly cancels**. Measured on the shipping presets at their native
gates, overscan 2.0:

| Preset | circle (half-widths) | θ at the circle | θ a rectilinear lens would show there | compression |
|---|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole` | 0.454 | 80.9° | 54.7° | 1.48x |
| `DL_L_Favourite_6mm` | 0.643 | 76.4° | 53.1° | 1.44x |
| `DL_L_PoorThings_8mm` | 0.810 | 72.2° | 51.6° | 1.40x |
| `DL_L_Favourite_6mm_Frame` | 0.980 | 67.4° | 49.6° | 1.36x |
| `DL_L_PoorThings_8mm_Frame` | 0.998 | 66.8° | 49.4° | 1.35x |
| `DL_L_Favourite_10mm` | 1.086 | 68.1° | 51.2° | 1.25x |

A 180° fisheye should reach 90° at the rim, and the whole character of one lives in the last 10–15°,
which a rectilinear source cannot supply at any finite overscan (85° on an 8 mm would need overscan
7.3). So every preset lands in the same narrow 65–81° / 1.25–1.48x band and reads as "a wide lens".

**This is a limit of sourcing a fisheye from one rectilinear render, not a bug in the presets.**
Raising the ceiling does not fix it; only a cube-map or multi-view source would. What Unreal actually
offers for that, and what each option costs, is in `.claude/refs/wide-field-source.md`.

The `[1, 2]` ceiling itself is **self-imposed** - Epic does not clamp overscan. See the roadmap entry.

## Filmback, focal length and overscan are the same knob

Established 2026-09-19 by a third investigation. `DriveProjection` computes
`theta_cap = atan(O * W / 2f)` (`DynamicLensComponent.cpp:586-588`) and
`UCineCameraComponent::GetHorizontalFieldOfViewInternal` computes `2*atan(W*O / 2f)`
(`CineCameraComponent.cpp:301`). **Filmback width, one-over-focal-length and overscan enter the maths
only as their product.** There is no independent gain from any one of them.

But `W` and `f` also rescale the *output* fisheye image plane (`DynamicLensComponent.cpp:598`:
`X = (U - 0.5f) * W`), and overscan does not. So they reach the same field angle by **shrinking the
porthole**, while overscan reaches it and **grows** the porthole. From an 8 mm, 24.89 mm gate, O=2
baseline of 72.2 deg / circle 0.810:

| change | theta_cap | circle (half-widths) |
|---|---|---|
| overscan x2 (O=4) | 80.9 deg | **0.907** up |
| filmback x2 (W=49.78) | 80.9 deg | **0.454** down |
| focal /2 (f=4) | 80.9 deg | **0.454** down |
| overscan x4 (O=8) | 85.4 deg | **0.958** up |
| filmback x4 | 85.4 deg | **0.240** down |

**Overscan strictly dominates.** Widening the filmback or shortening the focal is a worse way to
spend the same physics. This is also why the `_Frame` presets, which shrink the gate, enlarge the
circle and lose field angle - they are moving up this same table.

## The centre is critically sampled, and the rim has surplus

The intuitive objection to a very wide rectilinear source is that it spends all its pixels at the rim
and starves the centre. At a **fixed** render width that is true and brutal: at 175 deg HFOV on a
1920-wide render the central 40 deg of field is 31 pixels across, while a rim pixel spans 0.16 arcmin,
nine times finer than the eye.

**That is not what this plugin does.** With `bScaleResolutionWithOverscan` the render target is
`1920*O` wide, so on-axis source density is `1920*f/W` px/rad - **independent of O** - and the
equidistant output circle has exactly the same density. They are algebraically identical, so:

| preset | O | theta_cap | circle | **source px per output px at centre** | at the rim |
|---|---|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole` | 2 | 80.9 deg | 0.454 | **1.000** | 11.4x @ 73 deg |
| `DL_L_PoorThings_8mm` | 2 | 72.2 deg | 0.810 | **1.000** | 5.6x @ 65 deg |
| `DL_L_Favourite_6mm` | 2 | 76.4 deg | 0.643 | **1.000** | 7.7x @ 69 deg |

The fisheye centre is critically sampled today and the rim carries 5-11x surplus. Past `O = 2` the
centre falls off as **exactly `2/O`**, and that is entirely Epic's clamp at
`CameraStackTypes.cpp:542` (`OverscanResolutionFraction` to `[1,2]`), not physics. Pay `(O/2)^2` in
pixels and it comes back to 1.000.

## Two real quality bugs on the fisheyes today

Both are present at the current overscan of 2.0 and neither has anything to do with distortion maths.

**LOD: smaller than first thought, and Nanite is fine.** *Corrected 2026-09-24 (late).* The first
write-up said every mesh picks LOD "6.2x further away" on the 4 mm. That compared the 161.7 deg render
against a 90 deg one (`1/tan(halfFOV)` = 6.2), which is the wrong baseline: what matters is the same
camera *without* overscan, whose centre density the fisheye output already matches (the 1.000 above).
Against that baseline:

- **Nanite: not coarsened** while overscan <= 2 with `bScaleResolutionWithOverscan`.
  `FPackedView::UpdateLODScales` uses `0.5f * ViewToClip.M[1][1] * ViewSizeAndInvSize.Y`
  (`NaniteShared.cpp:195-202`): pixels per unit tan. Overscan divides `M[1][1]` by O and scaled
  resolution multiplies the view size by O, so they cancel. Past O = 2, Epic's resolution-fraction
  clamp stops the cancelling and clusters coarsen by `O/2`, the same factor as the centre softness.
  `LODDistanceFactor` does not reach the cluster LOD at all, only culling (`NaniteShared.cpp:249,269`).
- **Discrete static-mesh LOD: coarsened by exactly O** (2x at O = 2), on every preset, not just
  fisheyes. `ComputeBoundsScreenSize` (`SceneManagement.cpp:939`) measures a *fraction of the screen*,
  not pixels, and the overscanned screen is O times wider. Fixable per view by dividing
  `FSceneView::LODDistanceFactor` by the resolution fraction actually applied (`min(O, 2)`, or 1
  without scaled resolution). See the roadmap entry for why that is not a one-liner.

**The near plane eats the rim.** Clipping is on view-space `Z = d*cos(theta)`, not on ray distance, so
at the default 10 cm near plane everything nearer than **0.64 m along the ray** is clipped at 81 deg
off-axis, 1.15 m at 85 deg, 2.87 m at 88 deg. An ultra-wide fisheye therefore eats a growing sphere of
nearby geometry. Fixable: `UCineCameraComponent::CustomNearClippingPlane` has
`ClampMin = "0.00001"` (`CineCameraComponent.h:87`) and feeds `GetFinalPerspectiveNearClipPlane`, and
reversed-Z with infinite far tolerates a millimetre near plane well. **Worth setting on every
`DL_L_*` camera regardless of what else changes.**

## How far overscan actually reaches

`theta_cap = atan(O*W/2f)`, so each doubling of overscan halves the remaining gap to 90 deg. On the
4 mm from O=2: **+4.5 deg, +2.3 deg, +1.1 deg, +0.6 deg**.

| preset | O=2 | O=3 | O=4 | O=6 | O for 85 deg |
|---|---|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole` | 80.9 / 0.454 | 83.9 / 0.471 | 85.4 / 0.479 | 86.9 / 0.488 | **3.67** |
| `DL_L_Favourite_6mm` | 76.4 / 0.643 | 80.9 / 0.680 | 83.1 / 0.699 | 85.4 / 0.719 | 5.51 |
| `DL_L_PoorThings_8mm` | 72.2 / 0.810 | 77.9 / 0.874 | 80.9 / 0.907 | 83.9 / 0.941 | 7.35 |
| `DL_L_Favourite_6mm_Frame` | 67.4 / 0.980 | 74.5 / 1.083 | 78.2 / 1.138 | 82.1 / 1.194 | - |
| `DL_L_PoorThings_8mm_Frame` | 66.8 / 0.998 | 74.1 / 1.106 | 77.9 / 1.164 | 81.9 / 1.223 | - |

(theta_cap in degrees / circle radius in half-widths. The circle **grows** with overscan, so raising
it also closes some of the 23% porthole deficit.)

The last 5 degrees of a 180 deg fisheye - where all its character lives - stay unreachable. That part
is a hard wall: `atan(K) < 90` for every finite `K`, in any engine. Only a cube map or multi-view
source crosses it.

## Why the image circle "does not appear when it should"

On every `DL_L_*` fisheye the data limit is tighter than the lens circle, so what you see is the data
limit, never the lens:

| Preset | lens circle | data limit | what is drawn |
|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole` | 0.583 | **0.454** | data limit — porthole ~23% too small, and 0.93 elliptical rather than round |
| `DL_L_Favourite_6mm` | 0.924 | **0.643** | data limit |
| `DL_L_PoorThings_8mm` | 0.924 | **0.810** | data limit |
| `DL_L_Favourite_10mm` | 1.607 | **1.086** | data limit (corner cut only) |

Three visible consequences:

- `ImageCircle > Scale` appears to do nothing on these presets, because it only moves the loser.
- The porthole is an **ellipse**, because the data limit is one. A real circular fisheye's circle is
  round. `Ellipticity` on the MID reads 0.93 on the 4 mm.
- The circle moves when overscan or the gate changes, which is not how a lens behaves.

Nothing in the details panel says which of the two is active, which is why this is hard to diagnose
from the editor.

## `Distortion > Amount` does not reach the projection path

`FDynamicLensSettings::Evaluate` applies `Distortion.Amount * AmountMultiplier` to `K1..K3, P1, P2`
only. `DriveProjection` never reads it. There is currently **no control that tunes how fisheye a
`DL_L_*` ultra-wide is** — Amount, the multiplier, and the override block all do nothing there.

## Presets that are actually broken

Audited 2026-09-19 across the whole `DL_L_*` family by stepping a test camera through every preset
with Match Camera To Profile and reading `needed` against `applied`:

| Preset | Symptom |
|---|---|
| `DL_L_Favourite_10mm_Rect` | fixed overscan 1.5, needs 1.62 — the frame edge is sourced from outside the render, so the edges smear. Mask sits at 1.06 half-widths, past the frame edge, so nothing covers it. |

Every other `DL_L_*` had `needed ≤ applied` at its native gate and nominal focal. The fisheyes are
not "broken" in this sense — they are under-distorted, which is the separate problem above.

## Measuring this yourself

The level viewport **does not apply the camera's post-process** when piloting a CineCameraActor in
this project — a saturation override of 0 on the camera changes nothing either, so it is not
DynamicLens-specific. `HighResShot` from a piloted viewport therefore shows no distortion and no
image circle, and is worthless for judging the look. Use PIE (`editor_request_begin_play`, then
`set_view_target_with_blend` to the camera) or Movie Render Graph.

Two further traps in PIE on CitySample: the editor window must not be minimised or `HighResShot`
never produces a frame, and world partition streams around the player pawn, so a camera parked far
from the pawn renders an empty world with only the skydome.

The component itself *does* tick in the editor with no viewport involvement, so
`last_overscan_factor`, `needed_overscan_factor` and `image_circle_radius` can be read straight off
the component while stepping presets. That is how the audit table above was made.

## Resolution cost of fisheyes, measured 2026-09-25

Source pixels per output pixel (linear) = `(Rf/O) * (S/Mag) * J(theta)`, with `Rf = min(O,2)` in the
editor/PIE (Epic's clamp, `CameraStackTypes.cpp:539-543`), S the Fit field scale, Mag the Coverage
magnification, J = sec^2 (radial) or tan/theta (tangential). The **centre is always the minimum**; the rim
is 2-15x oversampled.

| Preset | O | centre | ~px of detail across 1920 |
|---|---|---|---|
| non-Fit fisheyes (8/6/4 mm) | 2 | 1.00 | 1920 |
| PoorThings_8mm_Fit | 3 | 0.62 | 1190 |
| Favourite_6mm_Fit | 3 | 0.49 | 940 |
| 8mm_Fit at Coverage 1.2 | 3 | 0.48 | 920 |

- **ST-map and parametric presets lose nothing** (O <= 2, so Rf = O). Their only softness is bilinear
  resampling, same as a Nuke STMap.
- The editor note's `200/O` ignores S and Mag and over-reports the Fits.
- **`ProjectionMapSize = 256` is too coarse**: up to ~2 px of position wobble near the rim at 1920 on the
  6/8 mm Fits. 2048 drops it to ~0.14 px.
- **Movie Render Graph has no 2x cap** on the Post Process Material path: it sets
  `OverscanResolutionFraction = 1 + Overscan` unclamped (`MovieGraphImagePassBase.cpp:134-139`). The TSR
  path's cap is CameraCalibrationCore's (`LensDistortionSceneViewExtension.cpp:667`).

**Beating the cap in the viewport/PIE, from a plugin:** override `SceneViewInitOptions.OverscanResolutionFraction`
per view in a scene view extension (`SetupView` for PIE/MRG, `BeginRenderViewFamily` for the level editor,
where `ViewActor` is set too late for `SetupView`). Nothing downstream re-clamps. Cost `(O/2)^2` vs today.

**Not rendering out-of-circle pixels:** only ~6% of the overscanned rect is outside the circle; the waste is the
over-sharp rim. A camera-attached mask ring in `HiddenPrimitives` saves ~5-20%; a custom VRS image generator
~5-15%, needs `r.VRS.Enable` (global), skips Lumen/shadows, and likely not active in MRG. Neither is worth
building yet. Rasterisation cannot render a curved projection; "nested frusta" needs a second renderer, which
Dylan rejected along with the cube capture (2026-09-25).
