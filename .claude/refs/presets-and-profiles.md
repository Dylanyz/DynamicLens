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
| `projection_profiles` | analytic fisheye profiles (`L_*`), one per lens |
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
| `DL_L_*` | Lanthimos-film reconstructions | The Favourite 6 mm and 10 mm (plus a `_Rect` variant), Poor Things 4 mm porthole, 8 mm, Petzval 58/85, Ultra Prime 10 mm, Master Zoom 16-110, Optimo 24-290, VistaVision Leica R. |
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

Fisheyes are one preset per lens (2026-09-25). **Image Circle > Scale** sizes the circle and the picture
with it, porthole to filled frame; **Field** is Fit to Circle or True Angles. These replaced the old
`_Fit` and `_Frame` presets. The fisheye presets open at Scale 1.1 of the frame, overscan Dynamic up to 4.

## Lens kits

A kit (`UDynamicLensKit`, `DLK_<name>` in `/DynamicLens/Kits`) is a list of focal length + existing
preset pairs. On a component's **Kit** it picks the preset from the camera's focal, nearest in
log-focal space. It adds no lens data of its own: every entry is a preset that already exists.

Kits come from the `kits` section of `Tools/data/presets.json` via `dl.import_kits()` (also part of
`dl.import_all()`), same rule as presets: never edit one only in the editor. Shipped:
`DLK_L_PoorThings` (4, 8, 10, 58, 85 mm) and `DLK_L_Favourite` (6, 10 mm), built from the
`DL_L_*` presets.

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

## Preset Browser tags

The Preset Browser lists presets from Asset Registry tags written by `UDynamicLensPreset::GetAssetRegistryTags`, and **must never load a preset to display one** (presets hard-reference profiles, ST-map profiles hard-reference ~107 MB of textures). Tag names live in `DynamicLensTags` (`DynamicLensTypes.h`) so the runtime and editor modules cannot drift: `DL.Label`, `DL.Description`, `DL.Family`, `DL.Source`, `DL.ProfilePath`, `DL.Type`, `DL.Squeeze`, `DL.FocalMin`/`DL.FocalMax`, `DL.ImageCircleMm`, `DL.MaxAperture`, `DL.SensorMm`, `DL.MapCount`, `DL.Breathes`, and `DL.Distortion` - curvature (worst departure from a straight mapping as a fraction of half the frame), not overscan.

Tags are written only when an asset is saved. After any change to `GetAssetRegistryTags`, run `dl.resave_presets()` or the browser's filters go stale.

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
