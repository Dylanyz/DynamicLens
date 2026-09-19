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

**Hard ceiling of 2.0.** `Applied` is clamped to [1, 2] in `ApplyToCamera`, and again in
`ApplyRendering` where `Cam->Overscan` is clamped to [0, 1]. Epic's camera overscan is a 0–1 field,
so 2x the frame is the most that can be asked for through this route.

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
Raising the ceiling does not fix it; only a cube-map or multi-view source would.

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
