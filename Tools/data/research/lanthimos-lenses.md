# The Lanthimos lenses — research record

Everything known about the lenses behind the `DL_L_*` presets, in one place, so the next person
(or model) can improve them without re-doing the research. The numbers themselves live in
`Tools/data/presets.json`; this file is the *provenance and the reasoning*, including what is
measured, what is a data sheet, and what is a guess.

**Nothing here is a substitute for `presets.json`.** If a number disagrees, the JSON wins and this
file is stale — fix it.

## Why these lenses exist as presets

Both films get their look from putting a lens on a camera it was not made for, or from a projection
that is not rectilinear. That is the whole point of the family:

- **The Favourite** (2018, Robbie Ryan BSC ISC). Panavision Millennium XL2, 35 mm 4-perf, released
  1.85:1. Most of the film is on "an extremely wide 10 mm"; the fisheye inserts are a Nikkor 6 mm.
  Sets used: Nikkor + Panavision PVintage. Sources: Kodak and Cinematography World interviews.
- **Poor Things** (2023, Robbie Ryan). ARRICAM LT/ST, 35 mm 4-perf, released 1.66:1. The porthole
  shots are an **OpTex 4 mm fisheye built for 16 mm/Super 16, mounted on a 35 mm camera** — the
  lens's image circle (14.5 mm) is far smaller than the gate (24.89 mm wide), so you see the edge
  of the lens itself. Lab/ballroom wides are reported as an 8 mm fisheye. Other lenses: ARRI/Zeiss
  Ultra Prime 10 mm, Master Zoom 16.5-110, Angenieux Optimo 24-290, Lomography Petzval 58 and 85
  (rehoused by True Lens Services), Leica R on VistaVision. Sources: Noam Kroll's article,
  Cinematography World.

**The mismatch is the effect.** Dylan's framing of it: the image circle should not be a decoration
you switch on, it should be what you get when the lens does not cover the sensor. See
`.claude/refs/overscan-and-image-circle.md` for how far the code currently is from that.

## Gate sizes

Both films are 35 mm 4-perf Super 35 (24.89 x 18.66 mm full gate), extracted to the release ratio:

| Film | Ratio | Gate used as `native_sensor_mm` |
|---|---|---|
| The Favourite | 1.85:1 | 24.89 x 13.45 mm |
| Poor Things | 1.66:1 | 24.89 x 15.00 mm |

The `_Frame` preset variants deliberately use a *smaller* gate, which is not the camera's gate: it
is the blow-up that makes the lens's circle reach the corners the way the films' frames do. The
blow-up factor is assumed; the framing is what was matched.

## The lenses

| Profile | Lens | Projection | Circle | Data sheet vs guess |
|---|---|---|---|---|
| `L_Nikkor_6mm_Fisheye` | Nikon Fisheye-Nikkor 6 mm f/2.8 | Equidistant | 23 mm | Data sheet: 220° AoV, 23 mm circle, 236 mm front, f/2.8. Assumed: blades, pupil visibility. |
| `L_Nikkor_8mm_Fisheye` | Nikon Fisheye-Nikkor 8 mm f/2.8 | Equidistant | 23 mm | Data sheet: 180° circular, 23 mm circle, 123 mm front. Assumed: blades, pupil visibility. The lens for Poor Things' 8 mm shots is *reported*, not confirmed. |
| `L_Nikkor_OP_10mm_Fisheye` | Nikon OP Fisheye-Nikkor 10 mm f/5.6 | **Orthographic** | 20 mm | Data sheet: r = f·sin θ, 180°, f/5.6, circle = 2f. Assumed: blades, front, pupil. Not currently used by any preset. |
| `L_Optex_4mm_S16_Fisheye` | OpTex 4 mm T2 (Super 16) | Equidistant (assumed) | 14.5 mm | Reported by Ryan: S16 fisheye on a 35 mm ARRICAM. Circle = S16 coverage. **Projection is a guess** — an S16 4 mm fisheye could as easily be equisolid. Front, blades, pupil assumed. |
| `L_Stereographic_10mm_FullFrame` | unnamed "extremely wide 10 mm" | Stereographic | 40 mm | **Entirely a reconstruction.** The frames read as ~130° on the diagonal with strongly bowed lines; stereographic r = 2f·tan(θ/2) reproduces that. The real lens is not named in any source found. |
| `L_UltraPrime_10` | ARRI/Zeiss Ultra Prime 10 mm T2.1 | rectilinear (parametric) | 31.1 mm | Data sheet for the optics. **Distortion is a placeholder**: the Master Prime grid clamped at its widest measured focal, 12 mm. A real 10 mm grid would replace it. |
| `L_Petzval_58` / `_85` | Lomography Petzval, TLS rehoused | rectilinear (parametric) | 43 mm | Lomography data: 58 mm f/1.9 and 85 mm f/2.2, Waterhouse round stop (hence 16 fully curved blades), full-frame. **Distortion is the Master Prime grid** — there is no Petzval grid, and a Petzval's whole point is that it is not a Master Prime. The swirl comes from the Petzval/bokeh model, not from distortion. |

## Measurements taken from film frames

These are the only numbers here that were *measured* rather than read off a data sheet.

- **Poor Things 4 mm porthole, 2026-09-06.** From a 1400x845 crop of a 1.66:1 still. Blurred-
  luminance iso-contour circle fit: centre (660, 411), R = 364 px, i.e. offset −0.057 half-width in
  x and −0.028 half-height in y. Radial mean luminance: plateau 0.40 over 0.75–0.85 R, 50% at
  1.04 R, 10% at 1.13 R. Blue fraction of RGB rises 0.25 → 0.35 at 1.05–1.075 R (the blue rim).
  Outer black radius 1.13 R = 0.587 half-width = a **14.6 mm circle on a 24.9 mm gate**, which
  matches the OpTex 14.5 mm spec to within a percent. That agreement is the strongest evidence in
  this file that the physical model is right.
- **The Favourite 6 mm**, from the film's frames: no hard rim, corners roll off over ~45% of the
  radius, top corners darker than bottom. Recorded as the preset's `image_circle_edge` values
  rather than as a separate measurement.

**No film frames are stored in this repo.** They are copyrighted and this repo is public and
Apache-2.0 (`.claude/rules/licensing-and-credits.md`). What is kept is the measurement and enough
description to repeat it from a frame you supply. The method: crop to the active image, blur the
luminance heavily, fit a circle to an iso-contour, then take the radial mean of luminance and of the
blue fraction in units of that radius.

## What is known to be wrong or unfinished

Measured 2026-09-19 in Vice City; the working is in `.claude/refs/overscan-and-image-circle.md`.

1. **Every projection preset shows about the same amount of fisheye** — 65–81° of field and a
   1.25–1.48x angular compression at the visible edge, whether it is the 4 mm 180° fisheye or the
   10 mm. Focal length very nearly cancels, because the visible circle is set by how far the
   *rectilinear source* reaches at the overscan ceiling, not by the lens. This is why the
   ultra-wides "just look like a wide lens".
2. **What you see as the image circle on these presets is not the lens's image circle.** It is the
   data-limit ellipse — the edge of what the overscanned render can supply. On the 4 mm the lens
   circle would be 0.587 half-widths (matching the measurement above) and the data limit is 0.454,
   so the data limit wins: the porthole comes out ~23% too small and slightly egg-shaped (0.93
   vertical) instead of round.
3. **`Distortion > Amount` does nothing on the projection path.** It only multiplies Brown-Conrady
   coefficients. There is currently no way to dial the fisheye up or down.
4. **`DL_L_Favourite_10mm_Rect` renders with unsourced edges**: fixed overscan 1.5, needs 1.62.
5. **The Petzval and Ultra Prime distortion is a Master Prime stand-in**, openly, and should be
   replaced when a real grid exists.
6. `lens_catalogue.json` reports `edge_shift_pct` as "not defined" for projection profiles, so the
   fisheyes are invisible to any question like "which are the strongest lenses".

## Open questions worth answering

- Is the OpTex 4 mm equidistant or equisolid? One frame with a known-geometry subject would settle it.
- What *is* The Favourite's 10 mm? If it is a Nikkor, the projection should be equidistant rather
  than stereographic, and the preset changes substantially.
- Did the films optically blow up the fisheye frames, and by how much? The `_Frame` variants assume
  1.7x (6 mm) and 1.33x (8 mm) purely to land the corners where the frames show them.
