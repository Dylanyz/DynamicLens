# Image circle guide — how big the circle is, what wins, and how to resize it

Written 2026-09-24, after the continuous projection K, `bFitFieldToCircle` and the overscan ceiling
of 4 landed. Numbers were computed offline in Python by replicating `Apply`, `DriveProjection`,
`DriveParametric` and `DynamicLensMath` against `Tools/data/presets.json` and
`Tools/data/profiles/*.json`, at each profile's native gate and nominal focal (what
Match Camera To Profile gives). The non-fit fisheye numbers reproduce the tables in
`overscan-and-image-circle.md` exactly (4 mm at O=2 gives 0.454), which is the check on the method.
Nothing here was measured in a render.

Units: **radius in half-frame widths** (1.0 = the left/right frame edge), the unit the mask material
and `ImageCircleRadius` on the component use. The frame corner sits at `sqrt(1 + (H/W)^2)`: 1.137
on 1.85:1, 1.168 on 1.66:1, 1.189 on the Master Prime gate, 1.218 on ALEXA LF Open Gate.

## The one ratio that matters

    Coverage C = image-circle diameter / frame diagonal        (both in mm, same frame)

| C | What you see |
|---|---|
| < 1 / aspect-corner (about 0.5–0.55) | a porthole that doesn't reach the top and bottom edges |
| about 0.55–1.0 | circle crosses the top/bottom edges, cuts the corners |
| 1.0 | circle exactly touches the corners |
| above `1 / (1 - Softness)`, plus centre offset and wobble | nothing visible |

Radius in half-widths = `C x corner`. So on a 1.66:1 gate, C=0.5 is radius 0.584, C=1 is 1.168.

## The two masks, recap

`Apply` builds one mask from whichever reaches the frame corner first (`MaskCornerR`):

1. **Lens circle**: `ImageCircleMm x Scale / W`. The physical circle. Doesn't depend on overscan.
2. **Data limit**: where the render runs out of source pixels. Grows with overscan, and on the
   projection path it's an ellipse.

`ImageCircle.Scale` only moves (1). If (2) is tighter, Scale does nothing you can see.
**`bFitFieldToCircle` inverts this**: it squeezes the field until (2) sits exactly on (1), so the lens
circle always wins, stays round and stays physical, and Scale works again.

## Per family, native gate

### Spherical wides (parametric: Master Prime, Supreme, Ultra Prime 10, 10mm_Rect)

| Preset @ focal | Gate mm | IC mm | **C** | lens R | corner | data limit at O = 1.5 / 2 / 3 / 4 | Wins |
|---|---|---|---|---|---|---|---|
| `DL_AD_Master` 12–25 | 28.25 x 18.17 | 33.6 | **1.000** | 1.189 | 1.189 | outside the corners at every O | lens circle, just touching the corners |
| `DL_AD_Supreme` 15–25 | 36.7 x 25.54 | 46.3 | **1.036** | 1.262 | 1.218 | outside | none |
| `DL_L_PoorThings_UltraPrime_10mm` | 24.89 x 15.0 | 31.1 | **1.070** | 1.249 | 1.168 | outside | none |
| `DL_L_Favourite_10mm_Rect` (Master @ 10, extrapolated) | 28.25 x 18.17 | 33.6 | **1.000** | 1.189 | 1.189 | outside | lens circle, just touching the corners |
| `DL_L_PoorThings_Petzval_58/85` | 24.89 x 15.0 | 43.0 | **1.480** | 1.728 | 1.168 | outside | none |

The data limit never wins on a spherical parametric at its native gate. These lenses need overscan
of about 1.1–1.6, and the mask is computed at the ceiling, so the data limit always sits past the corner.

**Caveat, derived rather than rendered:** at the Master Prime's wide end (12 mm and below, so the
Ultra Prime 10, `10mm_Rect` and `C_MasterHeavy`), the radial polynomial stops rising at about
**O = 1.53–1.55**. `ValidExtents` at a higher ceiling reads the polynomial after it has turned back
down. By O=3 the extent goes negative, and `MinData` silently drops the data limit. That's harmless
while overscan needed stays under applied overscan, but past about 1.5 the data mask on these
lenses no longer means anything. **Don't raise MaxOverscan on the parametric wides. It buys nothing.**

### Spherical ST maps (Andy Davis `DL_AD_*` ST sets, Cooke S4i)

`image_circle_mm` = gate diagonal, so **C = 1.000** on every one of them, and they need overscan
of 1.0–1.10. The data limit never wins, and the lens circle sits exactly on the corner.

**Visible side effect:** the mask's soft band runs from `R x (1 - Softness)` to `R`. So at C = 1.0
with the default Softness 0.05, the extreme corner pixel goes fully black and about **0.5 % of the
frame area** darkens. That covers every `DL_AD_*` ST preset, `DL_AD_Master`, the Master-based
`DL_L_*` and `DL_C_*`, and Cooke S4i. `ActiveMask` says "none" or "lens image circle" depending
on the 0.01 mm rounding of the diagonal, but the band draws either way, because `ApplyLook`
gets the radius even when it's past the corner. If that corner kiss isn't wanted, C needs to
be about 1.06.

### Anamorphics (tiedtke `DL_T_*`)

`image_circle_mm` = 0, so **there is no lens circle at all**. Only the data limit can draw. These
presets use the default Dynamic overscan with a **1.5 ceiling**, and ST-map data limit is a scaled
rectangle, `Cover = Ceiling / NeededOverscan`:

| Preset (worst focal) | needed | Cover at ceiling 1.5 | at 2.0 |
|---|---|---|---|
| `DL_T_Cineovision` | 2.00 | **0.75**, visibly cropped edges | 1.00 |
| `DL_T_Todd_AO_HighSpeed` | 1.82 | **0.82** | 1.00 |
| `DL_T_Panavision_E_Series` | 1.78 | **0.84** | 1.00 |
| `DL_T_Elite_MK` | 1.58 | 0.95 | 1.00 |
| `DL_T_Panavision_D_Series` | 1.53 | 0.98 | 1.00 |
| `DL_T_Hawk_V-Lite_Vintage` | 1.51 | 0.99 | 1.00 |

All other `DL_T_*` need 1.5 or less and show no mask. Those rounded-rectangle crops are data limit,
not lens character. Setting Max Overscan to 2 on those six removes them. TSR tops out at 2 anyway.

### Circular fisheyes, default (non-fit) path

`theta_cap = atan(O x W / 2f)`, circle = `f x g(min(theta_cap, theta_max))`. Radii are
`Rx / Ry` in half-widths.

| Preset | f | Gate | IC mm | C | lens R | O=2 | O=3 | O=4 | Wins |
|---|---|---|---|---|---|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole` | 4 | 24.89 x 15.0 | 14.5 | 0.499 | 0.583 | 0.454 / 0.421 | 0.471 / 0.448 | 0.479 / 0.462 | data |
| `DL_L_Favourite_6mm` | 6 | 24.89 x 13.45 | 23.0 | 0.813 | 0.924 | 0.643 / 0.555 | 0.680 / 0.618 | 0.699 / 0.652 | data |
| `DL_L_PoorThings_8mm` | 8 | 24.89 x 15.0 | 23.0 | 0.791 | 0.924 | 0.810 / 0.695 | 0.874 / 0.790 | 0.907 / 0.842 | data |
| (OP 10 mm orthographic profile) | 10 | 24.89 x 13.45 | 20.0 | 0.707 | 0.804 | 0.746 / 0.645 | 0.776 / 0.720 | 0.788 / 0.753 | data |

Each 1-step overscan raise makes the ellipse rounder (0.86 at O=2 to 0.93 at O=4) and bigger, but
the circle **never** gets to the lens's own size. For the lens circle to win, Scale has to be
**below**:

| Preset | O=2 | O=3 | O=4 |
|---|---|---|---|
| 4 mm porthole | 0.76 | 0.80 | 0.81 |
| 6 mm | 0.67 | 0.72 | 0.74 |
| 8 mm | 0.84 | 0.92 | 0.96 |

That's why Scale "does nothing" from 1.0 upwards on these presets.

### Full-frame fisheye and the `_Frame` variants (non-fit)

| Preset | Gate | C (physical) | data corner O=2 / 3 / 4 | corner | Data limit clears the corners at |
|---|---|---|---|---|---|
| `DL_L_Favourite_10mm` (stereographic) | 24.89 x 13.45 | 1.414 | 0.999 / **1.165** / 1.262 | 1.137 | **O = 2.78** |
| `DL_L_Favourite_6mm_Frame` | 14.4 x 7.78 | 1.405 | 0.915 / 1.037 / 1.102 | 1.137 | O = 4.82 |
| `DL_L_PoorThings_8mm_Frame` | 18.7 x 11.27 | 1.053 | 0.938 / 1.063 / 1.131 | 1.168 | O = 4.87 |

So the 10 mm is corner-clean from O=3, and the `_Frame` pair at O=4 is **97 %** of the way to its
corners, with a soft data-limit roll-off at the very corner.

### Fit mode (`_Fit` presets): the lens circle always wins

The circle is always `IC x Scale / W`, round. `S` is the field compression: the fraction of the
lens's real angle that ends up at each radius. S = 1 is the true lens. Lower S means it bends harder
than the real lens does.

| Preset (fixed O=3) | circle R | S at O=2 / 3 / 4 | rim angle shown at O=3 (lens: real) |
|---|---|---|---|
| `DL_L_PoorThings_4mm_Porthole_Fit` | 0.583 | 0.834 / 0.888 / 0.916 | 79.9 deg (90) |
| `DL_L_Favourite_6mm_Fit` | 0.924 | 0.689 / 0.731 / 0.753 | 80.3 deg (110) |
| `DL_L_PoorThings_8mm_Fit` | 0.924 | 0.863 / 0.936 / 0.975 | 77.1 deg (82.4 at the frame edge) |
| `DL_L_Favourite_10mm_Fit` | 1.607 (off-frame) | 1.000 / 1.000 / 1.000 | 70.5 deg (70.5), identical to the real lens |

Scale behaves in fit mode now. Shrinking the circle **reduces** compression: the 8 mm Fit at C=0.6
(Scale 0.76) is S=1.0, the real lens. Past the point where the binding radius is the frame edge,
not the circle, S stops changing.

**The fit mode can put rectilinear pixels inside the circle, derived from the code.** The circle
drawn is `IC x Scale`, but `DriveProjection` maps any pixel whose lens angle is past
`MaxFieldAngleDeg` to identity. Identity means a rectilinear sample, not black. So wherever
`IC x Scale / 2 > f x g(theta_max)`, a ring of un-fisheyed picture sits just inside the rim,
under the soft edge:

| Fit preset | lens-field circle `2 f g(theta_max)` | C limit before the ring appears | At Scale 1 |
|---|---|---|---|
| 4 mm porthole | 12.57 mm | **0.432** | **ring from R 0.505 to 0.583 already, with Softness 0.25 about half hides it** |
| 6 mm | 23.04 mm | 0.814 | clean, 0.04 mm to spare |
| 8 mm | 25.13 mm | 0.865 | clean; Scale > 1.09 gives a ring |
| 10 mm stereographic | 40.0 mm | 1.414 | clean |

The 4 mm data doesn't agree with itself. An equidistant 4 mm that reaches 90 deg makes a 12.6 mm
circle, not the Super 16 14.5 mm. Either the projection or the 90 deg is wrong. Two fixes would
work: clamp the fit-mode circle to `min(IC x Scale, 2 f g(theta_max))` in code, or set the Optex's
`max_field_angle_deg` to about 104 (equidistant, 7.25 mm / 4 mm = 1.81 rad) in `presets.json`.
Either way it's Dylan's call, and it belongs in `todo.md`.

**Fit on a `_Frame` gate is the best full-frame fisheye there is today.** The circle sits past the
frame (C 1.4 / 1.05), so no circle shows, and the fit only has to source the frame:

| Frame gate + fit | S at O=2 / 3 / 4 | corner angle shown (real) |
|---|---|---|
| 6 mm Frame | 0.896 / 0.979 / **1.000** | 78.1 deg (78.1) |
| 8 mm Frame | 0.894 / 0.976 / **1.000** | 78.2 deg (78.2) |

At O=4 that's the real equidistant lens, with nothing compressed. No `_Frame_Fit` preset exists yet.

## The knobs, and what each one costs

| Knob | What it moves | Cost |
|---|---|---|
| **Filmback** (`_Frame` variants; Camera > Filmback) | C scales as 1 / frame diagonal. A smaller gate gives a bigger circle, both lens and data. | Moves field of view. On a fisheye a smaller gate *loses* field angle at the edge (`theta_cap` drops), so it's the physically honest route to "circle touches corners". Any HUD or sensor-sized overlay moves too. |
| **Image Circle > Scale** | the lens circle only, `R = IC x Scale / W` | Free, and the only non-physical knob. Parametric, ST and fit: works fully. Non-fit projection: only wins below the Scale limits in the table above. Fit: past C_field you get the rectilinear-ring bug. |
| **Overscan** (Fixed or Max) | the data limit only. Fisheye `theta_cap` gives +4.5 / +2.3 / +1.1 deg per doubling on the 4 mm. | Past 2 the centre goes soft as `2/O`, unless you pay `(O/2)^2` more pixels. TSR caps at 2. LOD and Nanite coarsen with render FOV. Buys nothing on the parametric wides. |
| **Focal length** | fisheye circle radius `f x g(theta)`: a longer f means a bigger circle but less field | Same product as filmback in `theta_cap`. Presets lock focal, so unlock first. `f` almost cancels out of the non-fit data circle, so it's a weak lever. |
| **Fit Field To Circle** (profile) | makes the lens circle win, round, at its physical size, on the projection path | The picture bends harder than the real lens, by S. Fit presets use O=3, and the defaults have S around 0.73–0.94. The ring bug applies if C is past the lens's field. |
| **Projection K / Amount** (profile, `bUseProjectionK`) | how fisheye the picture is. With fit on, how much S has to give. | Not a circle knob as such, but a lower K (stronger fisheye) needs less field for the same circle, so S goes up. |
| **Softness, Center Offset, Wobble** | where the visible edge sits compared with R | The visible edge starts at `R (1 - Softness)`, so a 0.45-softness circle looks 45 % smaller than its radius. |

## Decision table

| I want... | Spherical or ST-map preset | Circular fisheye | Full-frame fisheye |
|---|---|---|---|
| **a tighter porthole** | Scale < 1. The C you get is `IC x Scale / diagonal`. | Use a `_Fit` preset, then Scale < 1: the circle is round and S goes toward 1. On a non-fit preset, Scale does nothing until you drop below the table's limit (0.76–0.96). | A `_Fit` preset with Scale: 10 mm Fit at Scale 0.5 gives C 0.71 |
| **the circle just touching the corners** | C = 1: Scale = `diagonal / IC`. AD ST presets are already there. For no black corner pixel, C of about 1.06. | You can't at the native gate without the ring bug (C_field is 0.81 / 0.87). Use a `_Frame` gate plus fit with Scale `diagonal / IC` (0.71 for the 6 mm Frame, 0.95 for the 8 mm Frame), at O=4 for S=1. | `DL_L_Favourite_10mm_Fit`, Scale 0.707, S=1 |
| **no circle at all** | Turn Image Circle off, or push C past `1/(1 - Softness)`. The data mask stays, correctly. | Not physical: a circular fisheye *is* the circle. The nearest thing is the `_Frame` gate plus fit at Scale 1 (circle off-frame), O=4. | `DL_L_Favourite_10mm` at O ≥ 2.8, or `_10mm_Fit` at any O |
| **to remove the edge crops on the vintage anamorphics** | Max Overscan 2 on the six `DL_T_*` listed above | — | — |

Turning Image Circle off never removes the **data limit**, and it shouldn't. Beyond the data limit
there's only smeared garbage.

## Image Circle > Size = Coverage (built 2026-09-24, late)

Built as proposed below, with the recommendations taken: the geometric definition (Softness does not
move the circle), Coverage mode implies fit on fisheyes, and past the lens's field the fisheye image is
magnified by `m = C / C_field`. The anamorphic ellipse is in too: the lens circle's radius is
`IC x Scale x squeeze / W` wide and `1/squeeze` tall. Verified from Python (no render): on
`DL_AD_Master` R = C x corner exactly; on the 8 mm C = 0.6 / 0.865 / 1.0 gives R 0.701 / 1.010 / 1.168
with S 1.0 / 0.80 / 0.83; on the 4 mm C = 0.5 gives R 0.584, S 0.83 (non-fit was data-limited at
0.454); on `DL_T_Hawk_V-Lite_Vintage` at 2x, C = 0.8 reads back 0.80 and C = 1 touches the corners.
Every preset renders exactly as before in Physical mode (all 65 compared).

## Proposal: an Image Circle > Coverage control (the design, kept for reference)

**Definition.** `Coverage = image-circle diameter / diagonal of the delivered frame`, measured on
the **physical sensor before desqueeze**, with any Cropped Aspect Ratio applied. 1.0 touches the
corners. It replaces the need to think in mm or half-widths.

**Naming clash:** `UDynamicLensProfile` already has a read-only `Coverage` string, the
focal/sensor summary. Call the new control **Circle Coverage**, or rename the old string to
`Summary`.

**Mode.** An enum on the Image Circle block:

- **Physical** (the default, today's behaviour): C follows from `ImageCircleMm` and the filmback,
  and changes with the gate, as a real lens does. Show the resulting C read-only.
- **Coverage**: hold C fixed whatever the filmback, focal or crop. That's what a DP means by
  "keep the porthole this size".

**Mapping onto what already exists.** `D` = delivered-frame diagonal, `IC` =
`EffectiveImageCircleMm()` (so tiedtke's 0 falls back to the diagonal).

| Path | Coverage C maps to |
|---|---|
| Parametric, ST map | `Scale = C x D / IC`. The data limit can still win, but only at a low ceiling; report it. |
| Projection, fit on | `Scale = C x D / IC` while `C <= C_field = 2 f g(theta_max) / D`. **Above C_field**, magnify the fisheye image plane: `r = m f g(theta)` with `m = C / C_field`, which is the `_Frame` trick done inside the map without touching the camera filmback. That also fixes the rectilinear ring. |
| Projection, fit off | Honour C only while it's under the data corner (the Scale-limit table). Otherwise clamp and say why in Notes, with the overscan that would get there. Or switch fit on by itself when Coverage mode is on. **Recommend making Coverage mode imply fit**, since it's the only path where the number means what it says. |

**Edge cases.**

- **Anamorphic squeeze.** A real anamorphic's circle is round on the *squeezed* sensor, so in the
  desqueezed picture it's an ellipse `squeeze` times wider. Today `ImageCircleRadiusNorm = IC / W`
  uses the **desqueezed** W and draws a round circle, which would come out `squeeze` times too
  narrow if any `DL_T_*` ever got a real `image_circle_mm`. The fix: measure C on the squeezed
  gate (2x, 23 x 18.66 mm gives a 29.6 mm diagonal, not the desqueezed 49.6), and draw with
  Ellipticity = `1/squeeze` in half-width units. C = 1 then touches the corners in both spaces.
  Front-anamorphic vignetting is often oval the other way. Leave that to Center Offset and
  Ellipticity overrides.
- **Crop (Cropped Aspect Ratio).** `Apply` already crops W and H before anything else, so C is
  against the delivered frame. Going from 1.66 to 2.39 shortens the diagonal, so in Physical mode
  the circle looks *bigger* relative to frame. That's correct: the lens didn't change. In Coverage
  mode it stays put.
- **Softness and offset.** C is the geometric radius. The visible edge is at `C (1 - Softness)`.
  If "1.0 = touches the corners" should mean *visibly* touches, define the control on the
  inner edge instead: `R = C x corner / (1 - Softness)`. Recommend the geometric version with a
  tooltip, because the soft band is a look, and changing Softness shouldn't move the circle.
- **Center offset and wobble** add up to `|offset| + wobble x R` to the corner distance. The
  "invisible" threshold is about `(1 + |offset|/corner + wobble) / (1 - Softness)`. For
  `DL_L_Favourite_6mm` (0.45, 0.04, 0.03) that's C about 1.9.
- **ST maps with a sensor bigger than the map** (Sensor Fit = Scale) stretch the map, so the
  "lens" diagonal is the camera's. C is still well defined against the delivered frame.

**Also report** C, the active mask and S (in fit mode) on the component's Debug block. It's the
number you need to diagnose any of the above, and `ActiveMask` alone doesn't show the corner
soft band.

## Follow-ups this surfaced (not done; for `todo.md` if Dylan agrees)

1. Fit-mode circle bigger than the lens field shows rectilinear pixels (4 mm Fit already does).
   Clamp the circle, or fix the Optex `max_field_angle_deg`.
2. `_Frame` + fit at O=4 is an exact full-frame fisheye with S=1. Worth two presets.
3. The six `DL_T_*` above need a Max Overscan of 2 to lose their data-limit crops.
4. The AD ST, Master and Cooke S4i presets darken the extreme corners through the soft band at C=1.0. Decide
   whether that's wanted.
5. `lens_catalogue.json` hasn't been regenerated since the `_Fit` presets landed. Run
   `dl.export_catalogue()`.
