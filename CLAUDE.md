# DynamicLens — measured lens character for Unreal CineCameras

A C++ UE 5.8 plugin by Dylan G (Mad Rice). Add a **Dynamic Lens** component to a CineCameraActor,
pick a preset, and the camera gets the distortion, image circle, vignette and bokeh of a real lens,
driven live by focal length, focus distance and f-stop. Profiles are fitted from measured cinema-lens
data, not invented. Open source, Apache-2.0, at https://github.com/Dylanyz/DynamicLens.

**This repo *is* the installed plugin.** `Engine\Plugins\Marketplace\DynamicLens` in the engine
install is a **directory junction to this folder**, not a copy. So editing a `.uasset` or a `.py`
here edits the live plugin, with no sync step. Only C++ needs a build.

## Start here (progressive disclosure)

- How the effect is actually produced, and the Epic internals it works around → `.claude/refs/architecture.md`. **Read before changing any C++.**
- Every component control and what it is for → `.claude/refs/using-the-component.md`
- The preset/profile data model, the lens catalogue, adding a lens → `.claude/refs/presets-and-profiles.md`
- Installing a DLL under a live editor, and why we don't → `.claude/refs/hot-swap.md`
- Keeping these docs true → `.claude/refs/maintenance.md`

Behaviour rules auto-load from `.claude/rules/`: editor restarts, the build/install cycle, the
preset data flow, licensing and credits. Read them; they are the ones that bite.

## Layout

| Path | What |
|---|---|
| `Source/DynamicLens` | the C++ module: types and maths, the component, the ST-map library, startup fixups |
| `Content/Presets`, `Content/Profiles`, `Content/Materials` | generated assets, `/DynamicLens/...` in the editor |
| `Content/Python/dynamiclens_tools.py` | editor tools, `import dynamiclens_tools as dl` |
| `Tools/data/presets.json` | **source of truth** for every profile and preset |
| `Tools/data/raw`, `Tools/data/profiles` | measured grids and the fits built from them |
| `Tools/build_dynamiclens.ps1` | build, and install when the editor is closed |
| `Binaries/`, `Intermediate/` | build products, gitignored, never committed |

## Iterating

**Content, Python, presets, profiles: no build, no restart.** Edit and it takes effect in the live
editor. Preset and profile changes come from `Tools/data/presets.json` via `dl.import_presets()`.

**C++ changes need a build, and installing the result needs the editor closed:**

```powershell
Tools\build_dynamiclens.ps1              # safe while the editor runs; packages to %TEMP%\dlb
Tools\build_dynamiclens.ps1 -InstallOnly # only once Dylan has closed the editor himself
```

Then relaunch and re-run whatever the change depends on, commonly
`dl.build_image_circle_material(force=True)` and `dl.import_presets()`.

**Never close or restart the editor yourself.** See `.claude/rules/editor-restarts.md`.

## Editor Python

Run through `unreal-py` (`editor_run_python`) or a remote-exec helper. `import dynamiclens_tools as dl` first.

| Call | Does |
|---|---|
| `dl.status()` | what is installed, which cameras have components |
| `dl.import_presets()` | rewrite preset assets from `presets.json` (`only=[...]` for one) |
| `dl.import_profiles()` / `import_projection_profiles()` / `import_derived_profiles()` | rebuild profile assets |
| `dl.import_tiedtke()` | re-import the ST-map lenses from the tiedtke pack |
| `dl.build_image_circle_material(force=True)` | rebuild the image-circle material after an HLSL change |
| `dl.import_all()` | all of the above, in order |
| `dl.reset_asset(path)` | restore one asset to its shipped values |
| `dl.add_to_all_cameras(preset=...)` | bulk-add the component |

## Where new things go

| Kind of change | Goes in |
|---|---|
| A new lens, preset, or a tuned value | `Tools/data/presets.json`, then re-import. Never only in the editor. |
| A new control or behaviour | `Source/DynamicLens`, plus the material parameter if it is visual |
| How the plugin works | `.claude/refs/` |
| A behaviour rule for agents | `.claude/rules/` |
| Anything the public should read | `README.md` |
| How one *film* uses the plugin | that film project's own `.claude/`, never here |

## Version control

Plain **git**, public remote `origin`. Dylan's film projects use Diversion; this repo does not.
Commit the docs and `.claude/` along with the code. Never commit `Binaries/` or `Intermediate/`.
