# Preset and profile data flow — the JSON is the source of truth

## The rule

**`Tools/data/presets.json` is the source of truth. The `.uasset` files are generated.**

A value tuned only in the editor is destroyed by the next `dl.import_presets()`. So any change worth
keeping goes into the JSON first, then gets imported. Never the other way round.

## When X, do Y

| Situation | Do |
|---|---|
| Dylan asks for a lens or look to change | edit `presets.json`, then `dl.import_presets(only=[...])` |
| Dylan says he tweaked something in the editor and likes it | read the values off the asset, write them into `presets.json`, *then* re-import to confirm they round-trip |
| About to run `dl.import_presets()` with no `only=` | check whether he has unsaved editor tweaks; ask if unsure, because it overwrites every preset |
| Adding a whole new lens | the right section of `presets.json` (see the table below), with a `Source` field, then the matching importer |
| A number did not come from a data sheet | mark it "assumed" in the profile's `Source`. Keep doing this; it is how the honest ones are told from the guesses. |

| JSON section | Generates |
|---|---|
| `profile_specs` | per-profile overrides applied to imported profiles (`AD_*`) |
| `projection_profiles` | analytic fisheye profiles (`L_*`), including the `_Frame` variants |
| `derived_profiles` | single-focal primes cut from a base grid (Petzval 58/85, Ultra Prime 10) |
| `presets` | every `DL_*` preset asset |
| `prefixes` | the naming scheme |
| `notes` | measurement provenance, e.g. how the porthole was measured |

## Do not "fix" the custom looks

`DL_C_Vintage` is a favourite of Dylan's. `DL_C_Vintage_Raw` exists specifically to preserve the
un-rescaled coefficient behaviour its look depends on, which is otherwise a bug. Neither gets
"corrected" without asking him, even if the maths looks wrong. Same for anything under `DL_C_*`.

Details on the catalogue and the ST-map conversion: `.claude/refs/presets-and-profiles.md`.
