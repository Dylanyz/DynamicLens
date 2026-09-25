# Presets, profiles, and adding a lens

## The data model

**Profile** = the optics of one lens. Distortion data (parametric coefficients, an ST map, or a
fisheye projection), native sensor, image circle diameter, nominal focal length, blade count and
curvature. One asset per lens or per prime.

**Preset** = a look built on a profile. Holds five blocks — Distortion, Image Circle, Vignette,
Bokeh, Overscan — and is what a user picks on the component.

**Source of truth is `Tools/data/presets.json`, not the assets.** The assets are generated.
A value tuned only in the editor is lost at the next `dl.import_presets()`. Always write the change
back to the JSON.

| JSON section | Generates |
|---|---|
| `profile_specs` | per-profile overrides applied to imported profiles (`AD_*`) |
| `projection_profiles` | analytic fisheye profiles (`L_*`), including the `_Frame` variants |
| `derived_profiles` | single-focal primes cut from a base grid (Petzval 58/85, Ultra Prime 10) |
| `presets` | every `DL_*` preset asset |
| `prefixes` | the naming scheme |
| `notes` | measurement provenance, e.g. how the porthole was measured |

## The catalogue

| Prefix | Origin | Character |
|---|---|---|
| `DL_AD_*` (parametric) | Andy Davis's measured ARRI/Zeiss Lens Files | Clean and accurate, and they **breathe** (a real focus stack). `DL_AD_Master`, `DL_AD_Supreme`. The baseline to compare against. |
| `DL_AD_*` (ST map) | Andy Davis's creative lens maps, spherical | 22 series / 110 primes of measured spherical character, under `Profiles/AndyDavis`. Vintage Canon K-35 and FD, Nikon AI-S, Leica R, Cooke S4i (17 focals) and S7i, Leitz Thalia and Summilux-C, Tribe7 Blackwing7, Zeiss CP2/CP3/Supreme Radiance, ARRI Signature, and the modern full-frame sets. **Single focus, so no breathing.** |
| `DL_T_*` | tiedtke ST maps, one per series | Real lenses with real flaws. Cooke FFi/SFi, Panavision C/D/E/G/Primo/AutoPanatar, Atlas Orion, Lomo Round Front, Elite MK, Kowa Cine Prominar, Iscorama Pre-36, Hawk V-Lite Vintage, Todd-AO, Angenieux Optimo, PS-Technik, ARRI/Zeiss Master Anamorphic. |
| `DL_L_*` | Lanthimos-film reconstructions | The Favourite 6 mm and 10 mm (plus `_Frame` and `_Rect` variants), Poor Things 4 mm porthole, 8 mm, Petzval 58/85, Ultra Prime 10 mm, Master Zoom 16-110, Optimo 24-290, VistaVision Leica R. |
| `DL_C_*` | Dylan's own looks | `DL_C_MasterHeavy`, `DL_C_Subtle`, `DL_C_Vintage`, `DL_C_Vintage_Raw`. **`DL_C_Vintage` is a favourite. Do not "correct" it without asking** — `_Raw` exists precisely to preserve the un-rescaled coefficient behaviour its look depends on. |

Every number that did not come from a data sheet is marked "assumed" in the profile's Source field.
Keep doing that. Where each source came from and what it contributed: `SOURCES.md`.

**`DL_T_*` and Andy Davis's creative lens maps are the same measurements.** Measured 2026-09-16:
sampling 18 lenses across 15 series, tiedtke's ST maps and Andy Davis's published maps agree to
within 0.00024 — one half-ulp of float16, i.e. storage rounding and nothing else. The data cannot
say which way the sharing runs, so credit both, and do not describe them in docs as two independent
measurement sets. Re-importing Andy's anamorphic maps would add nothing; his *spherical* sets are
the real gap, since every `DL_T_*` is anamorphic.

## Picking a lens for a shot

- Wide, distorted, period, dreamlike → the `DL_L_*` reconstructions. The porthole and the 6 mm are
  the extreme end.
- A specific real lens look → `DL_T_*`. These carry genuine asymmetry and edge behaviour.
- Modern, clean, "good glass" → `DL_AD_*`.
- Anamorphic bokeh → anything with a squeeze; check the Bokeh block's Squeeze source.

The `_Frame` variants use a smaller filmback so the image circle reads larger in frame. That is the
intended way to make a porthole bigger, alongside `Image Circle > Scale`.

## Lens kits

A kit (`UDynamicLensKit`, `DLK_<name>` in `/DynamicLens/Kits`) is a list of focal length + existing
preset pairs. On a component's **Kit** it picks the preset from the camera's focal, nearest in
log-focal space. It adds no lens data of its own: every entry is a preset that already exists.

Kits come from the `kits` section of `Tools/data/presets.json` via `dl.import_kits()` (also part of
`dl.import_all()`), same rule as presets: never edit one only in the editor. Shipped:
`DLK_L_PoorThings` (4, 8, 10, 58, 85 mm) and `DLK_L_Favourite` (6, 10 mm), both built from the
original `DL_L_*` presets, not the `_Fit` variants.

## Adding a lens

1. Add the entry to `Tools/data/presets.json` in the right section, with a Source field.
2. Run the matching importer: `dl.import_presets()`, `dl.import_profiles()`,
   `dl.import_projection_profiles()`, or `dl.import_derived_profiles()`.
3. Verify in the editor at several focal lengths and a focus sweep. Watch for edge artefacts at the
   widest end, which is where clamped or extrapolated data shows first.
4. If it came from someone else's measured data, **add the credit to `NOTICE` in the same commit**,
   and add its path to the excluded list if the data is theirs rather than yours.

## How the ST maps became profiles

`dl.import_tiedtke()` walks the pack's Lens File assets. For each one it reads the ST map texture,
its channel layout and pixel origin, duplicates the texture into `Content/Profiles/Tiedtke/Textures`
so it survives outside the pack, and writes a profile asset pointing at the copy. Anamorphic series
get their squeeze from the filename suffix (`_2x`, `_1_8x`, `_1_5x`); the native frame is tiedtke's
46 × 18.66 mm desqueezed 2.39 gate.

At runtime the profile is driven through `DriveSTMap`, which extends the map past its clamped border
before handing it to Epic. See `architecture.md` for why that is necessary.

The tiedtke pack is free but is **his**. The plugin ships the derived profiles with credit and a
link; the licence carve-out in `NOTICE` covers them.
