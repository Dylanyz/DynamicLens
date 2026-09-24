# Andy Davis vs tiedtke — what overlaps, what differs, what to ship

Researched 2026-09-24. Answers "his lenses match tiedtke's; which should we use, and can the AD
anamorphics be free zooms?" Every number below was measured against the original files unless it
says otherwise. Licensing is unchanged by any of this: `NOTICE` and
`.claude/rules/licensing-and-credits.md` still govern both datasets.

## The three Andy Davis datasets (they are not one thing)

| Dataset | Format | In the plugin as | Lenses |
|---|---|---|---|
| **Cinelens Lens Files** (UE `.uasset`, read by `Tools/read_lensfiles.py` → `Tools/data/raw/andy_davis_cinelens.json`) | Parametric: Brown-Conrady (5 floats) or 3DE4 Anamorphic Standard Degree 4 (14 floats), per focus × zoom | `DL_AD_Master`, `DL_AD_Supreme` (parametric) | 106 files, only 33 carry distortion: Master 17, Supreme 14, Cooke FFi ANA 1.8x 7 (85 mm all zero), Signature 2 |
| **Creative lens maps, spherical** (EXR `distort_*` / `undistort_*`) | ST map, one per prime | `DL_AD_*` ST-map presets, `Content/Profiles/AndyDavis` | 22 series, 110 primes |
| **Creative lens maps, anamorphic** (16 zips `*_ana_*`) | ST map, one per prime | not imported, deliberately | 16 series |

tiedtke's *Real Cinema Lenses* pack = UE Lens Files with ST-map tables, 19 anamorphic series → `DL_T_*`.

## 1. What exists in both

**Every AD anamorphic ST map is a tiedtke map.** Same focal list per series, pixel data equal to
the float16 half-ulp (0.00024) — see `SOURCES.md`. Re-checked today on Panavision E.

| Series | AD anamorphic maps (mm) | tiedtke `DL_T_*` (mm) | Same data? |
|---|---|---|---|
| Atlas Orion 2x | 40 50 65 80 | same | yes |
| Cineovision 2x | 25 35 50 85 | same | yes |
| Cooke FFi 1.8x | "27" 40 50 75 85 100 135 | same | yes, and **AD Cinelens also has it parametric** (below) |
| Cooke SFi 2x | 32 50 75 100 | same | yes |
| Elite MK4 2x | 25 40 50 75 100 | same | yes |
| Hawk V-Lite Vintage74 2x | 28 55 80 110 | same | yes |
| Iscorama Pre-36 1.5x | 50 85 105 | same | yes |
| Kowa Cine Prominar 2x | 40 50 75 100 | same | yes |
| Lomo Round Front 2x | 35 50 75 100 | same | yes |
| Panavision AutoPanatar 2x | 40 50 75 | same | yes |
| Panavision C 2x | 20 30 35 40 50 55 60 75 105 135 150 180 200 | same | yes |
| Panavision D 2x | 40 | same | yes |
| Panavision E 2x | 28, 35 (T2.0), 35 (T2.3), 40 50 75 100 135 180 | 28 35 40 50 **60** 75 100 135 180 | yes, see data flaws |
| Panavision G 2x | 35 40 50 60 75 100 | same | yes |
| Panavision Primo 2x | 35 40 50 75 100 | same | yes |
| Todd-AO High Speed 2x | 35 55 75 | same | yes |
| ARRI/Zeiss Master Anamorphic, Angenieux Optimo 44-440, PS-Technik 35-70 | — | tiedtke only | — |

Parametric overlap, which is where a real choice exists:

| Lens | AD Cinelens (3DE4 anamorphic, parametric) | tiedtke / AD ST map |
|---|---|---|
| Cooke FFi 1.8x | 32, 40, 50, 75, 100, 135 (85 macro all zero, 180 empty) | "27", 40, 50, 75, 85, 100, 135 |
| Panavision C 2x, Hawk V-Lite 1.3x | lens objects exist, **no distortion data** | Panavision C maps; Hawk here is the Vintage74 2x, a different lens |

The spherical overlap is AD vs AD, not tiedtke: Zeiss Supreme (parametric ships, ST map skipped),
ARRI Signature (ST maps ship; Cinelens has only 18 mm with K1/K2, 15 mm all zero).

## 2. Same data? Same format?

- **AD anamorphic ST maps and tiedtke: identical data and format** (ST maps). Both go down the
  **ST map** path. Nothing to choose between. Credit both.
- **AD Cinelens Cooke FFi: different format, same solve.** I fitted the 3DE4 degree-4 model to the
  ST maps. The fit reproduces Andy's coefficients once two conventions are right:
  - **pixel aspect 1.8**, even though the Lens File stores `PixelAspect = 1.0`. The EXR header says
    `pixelAspectRatio 1.8004`, 4448 × 3096, the squeezed LF open gate the Lens File also lists.
  - the polynomial maps the **distorted** position to the **undistorted** source, the same
    direction as a `distort` ST-map lookup.

  At 135 mm, for example, the fit gives CY04 0.096, CX22 0.059 and CY24 -0.139, which is exactly
  what the file stores. So these are not independent measurements either. They would use a new
  **Parametric (anamorphic)** path, which is the roadmap entry. Today the parametric path only
  evaluates Brown-Conrady.
- **The implementation catch:** feed UE's `UAnamorphicLensModel` the stored PA 1.0 with the squeezed
  36.7 mm sensor and the normalisation is wrong by 1.8x in x. That error is 200-640 px RMS in my
  tests. The importer must set PA 1.8, or give it a desqueezed 66.06 × 25.54 filmback. Verify
  in-editor against `DL_T_Cooke_FFi` before shipping.

## 3. Focus, f-stop, vignetting: what is actually in the files

| | Focus / breathing | f-stop | Vignetting | Other |
|---|---|---|---|---|
| AD Master / Supreme Lens Files | **Yes.** 3-46 focus samples per lens (most 25-40; Supreme 40 mm has 3, 65 mm 8) (encoder values, e.g. Master 32 mm 0 → 9203). K1/K2 change across focus (Master 32: K1 -0.034 → -0.011). **FxFy is constant over focus in all 31 lenses**, so what "breathes" is distortion, not field of view | none (a UE Lens File has no iris axis) | none | P1/P2/K3 zero; image-centre table empty |
| AD Cinelens Cooke FFi | **One focus sample** (200) per lens, so no breathing. Squeeze X 0.887-1.006 is a per-focal constant | none | none | FxFy is a placeholder `[1, 1.437]`. The `.ini` next to it has real specs: focal 40.6 / 47.75 / 73.4 / 96.8 / 132 mm, T2.3 (85 and 180: T2.8), 11 blades, close focus |
| AD creative maps (spherical and anamorphic) | single focus | none. Panavision E "35 T2.0" and "35 T2.3" are identical maps (max diff 3e-7): two lens versions, not aperture data | none | `undistort_*` twins with 100 px padding |
| tiedtke | single focus | none | none | profile physical specs are placeholders (T2.8, 11 blades, 110 mm front) |

**Nobody measured vignetting or aperture.** Everything the plugin does with f-stop (bokeh, cat's
eye, cos⁴ falloff) is modelled from assumed specs. Only `DL_AD_Master` and `DL_AD_Supreme` carry
real focus data.

## 4. Could the AD anamorphic be a free zoom? The honest comparison

Cooke FFi 1.8x, 4448 × 3096 squeezed px. The maps are the ground truth, because they are the
shipped `DL_T_Cooke_FFi`. Each fit first removes a per-axis scale and offset, since the maps have
their own framing normalisation.

| Focal | AD coefficients vs map, RMS / max px | Best possible degree-4 fit, RMS px | Top-edge bend, map / AD | Corner bend, map / AD |
|---|---|---|---|---|
| 40 | **52 / 232** | 16 | 80 / 89 | 57 / **209** |
| 50 | 1.4 / 4.5 | 1.2 | 75 / 80 | 216 / 236 |
| 75 | 14 / 41 | 12 | 32 / 34 | 51 / **96** |
| 100 | 7.4 / 13 | 7.0 | 17 / 17 | 37 / 25 |
| 135 | 0.06 / 0.11 | 0.06 | 6.7 / 7.1 | 3.1 / 3.4 |

Between the measured focals (the thing that matters for a zoom):

| Predicting | RMS px |
|---|---|
| 50 from 40/75, lerped AD coefficients | 20 |
| 50 from 40/75, blended ST maps | 46 |
| 50, snapping to the 40 map (what `DL_T` does today) | 52 |
| 100 from 75/135, lerped AD coefficients | 12 |
| 100 from 75/135, blended ST maps | 17 |
| 100, snapping to the 75 map | 26 |

What that means:

- **At measured focals the ST map wins, always.** It is the data. AD's coefficients match it at 50
  and 135 mm. At 100 and 75 mm they are off by up to 13 and 41 px max. At 40 mm they are a different
  solve and overstate the corners about 3.6x.
- **The ST maps hold more than degree 4 can express.** Even the best fit leaves 7-16 px RMS at
  40/75/100 mm. That non-polynomial structure is the "character", and parametric loses it
  everywhere. There is little left-right asymmetry: the centre offset is only 2-7 px, and the
  displacement is mirror-symmetric.
- **Between focals, parametric is smoother and closer** than snapping or blending maps, roughly 2x
  better RMS. That is the real prize, and the only one.
- **The 50 mm outlier is in the data, not the solve.** The 50 mm map itself bends 2-3x harder
  vertically than 40 or 75, and AD's 50 mm coefficients reproduce it to 1.4 px. Every method lurches
  at 50 mm. Whether the lens really does that cannot be told from here, because both datasets are
  the same measurement.
- **Breathing:** neither dataset breathes for anamorphic.
- **Coverage:** parametric has no 85 mm (zero coefficients). It does have a 32 mm, which is the map
  both packs label "27" (below).

## 5. Data flaws found on the way (both packs inherit them)

| Flaw | Evidence | Affects |
|---|---|---|
| Cooke FFi **"27 mm" is the 32 mm** | AD's zip has `distort_…_027mm` with no 032, and `undistort_…_032mm` with no 027. distort 027 → undistort 032 round-trips to **0.00 px**; the 40 mm control is 10.5 px RMS. Cooke's 1.8x FF range and Andy's `.ini` start at 32 | `DL_T_Cooke_FFi` focal list and label |
| Panavision C **20 mm == 30 mm** | byte-identical in AD's EXRs (max diff 2e-7) and in tiedtke's | `DL_T_Panavision_C_Series`. Bears on the roadmap's "C Series 20 mm edge smear": stepping to 30 mm cannot tell the two apart, and at most one of the labels is a real lens |
| Panavision E **"60 mm" is a copy of the 50 mm** | tiedtke samples identical; AD has no 60 mm E map | `DL_T_Panavision_E_Series` |
| Cooke FFi frame aspect | FFi maps are 4448 × 3096 at PA 1.8 (2.59:1 desqueezed); other series are 3656 × 1556 at PA 1.0. Every `DLP_T_*` uses one 46 × 18.66 mm (2.47:1) frame | worth checking: may stretch FFi about 5% |

## What this means for us

1. **There is no "AD vs T" choice for ST maps.** Keep shipping `DL_T_*` and keep the double credit.
   Importing AD's anamorphic maps still adds nothing.
2. **The only real addition is one zoomable Cooke FFi**, 32-135 mm. Ship it *next to*
   `DL_T_Cooke_FFi`, not instead of it:
   - `DL_T_Cooke_FFi`: "measured maps, exact at 40/50/75/85/100/135 (+32), snaps between"
   - `DL_AD_Cooke_FFi_Zoom`: "parametric, continuous 32-135, smooth, loses fine character;
     approximate at 40 and 75"

   Put the per-focal errors above in its `Source`. Leave the 50 mm in and say it lurches. Skipping
   it for smoothness would be a look decision, so it is Dylan's call.
3. **Before building it:** PA 1.8 (or a desqueezed filmback), the direction check, and a
   side-by-side against the map at 50 and 135 mm, where they should agree to about a pixel. That is
   the acceptance test.
4. **Fix the labels in `presets.json` or the importer:** "27" → 32, drop or flag the E 60 mm, and
   flag C 20/30 as one measurement. They are presentation errors, which is cheap to fix and matters
   more than the zoom.
5. **Grouping.** The Preset Browser's `Family` (`DL.Family`) is the *data source* (Andy Davis /
   tiedtke / Lanthimos / Custom), not the maker. It already filters by spherical/anamorphic,
   Parametric/ST map/Projection, breathes, prime and focal range, and groups by Family / Optics /
   DataType. What is missing:
   - a **`DL.Maker` tag** (Cooke, Panavision, Zeiss, ARRI, Canon…) and a **Group by Maker**. That
     puts `DL_AD_Cooke_S4i`, `DL_AD_Cooke_S7i`, `DL_T_Cooke_SFi`, `DL_T_Cooke_FFi` and the FFi zoom
     side by side, spherical next to anamorphic.
   - a **`DL.Series` tag**, so two presets of the same lens can be shown as "also available as …".

   Both belong in the editorial layer of `presets.json` (and `andy_stmap_sets`), then
   `dl.resave_presets()` after the C++ tag change.

## Measured vs assumed

**Measured today:** focal lists per series (zip contents vs `tiedtke_stmap_samples.json`). The
27 = 32 round-trip. The C 20 = 30 and E 60 = 50 duplicates, and E 35 T2.0 = T2.3. The EXR
dimensions and pixel aspects. Every px figure in section 4 (48 × 33 grid over each full map). The
PA 1.8 / direction finding (free fits reproduce stored coefficients at 50, 100, 135 mm). That FxFy is
constant over focus in all 31 Master/Supreme files.

**Carried over, not re-measured:** AD anamorphic maps = tiedtke to 0.00024 (`SOURCES.md`,
2026-09-16; spot-checked on Panavision E today).

**Assumed / not known:**
- That UE's anamorphic displacement map is used as a distorted→undistorted lookup. It is read from
  the shader, not rendered.
- Whether the 50 mm's strong vertical bend is the real lens or a bad grid.
- Which of C 20/30 is the real lens.
- Every physical spec on `DL_T_*` (aperture, blades, front diameter).
- All vignetting and f-stop behaviour, in both datasets.
