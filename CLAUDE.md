# DynamicLens — measured lens character for Unreal CineCameras

A C++ UE 5.8 plugin by Dylan G (Mad Rice). Add a **Dynamic Lens** component to a CineCameraActor,
pick a preset, and the camera gets the distortion, image circle, vignette and bokeh of a real lens,
driven live by focal length, focus distance and f-stop. Profiles are fitted from measured cinema-lens
data, not invented. Open source, Apache-2.0, at https://github.com/Dylanyz/DynamicLens.

**This repo *is* the installed plugin.** `Engine\Plugins\Marketplace\DynamicLens` in the engine
install is a **directory junction to this folder**, not a copy. So editing a `.uasset` or a `.py`
here edits the live plugin, with no sync step. Only C++ needs a build.

## Hard rules

1. **Never restart, close or relaunch the Unreal editor without asking Dylan and getting a yes.**
   He is usually mid-shot with unsaved work. `.claude/rules/editor-restarts.md`.
2. **Never touch `Saved/` or `Intermediate/`**, here or in any Unreal project. They are not yours,
   they are not tracked, and deleting from them loses work that cannot be recovered.
3. **Ask before deleting anything**, with specifics on what and why. Never run a recursive delete
   on an assumption.
4. **Never put third-party lens data under this repo's licence.** `Content/Profiles/Tiedtke/**` and
   `Tools/data/raw/**` belong to tiedtke and Andy Davis. `.claude/rules/licensing-and-credits.md`.
5. **`Tools/data/presets.json` is the source of truth**, not the generated assets. An editor-only
   tweak dies at the next import. `.claude/rules/preset-data-flow.md`.

## Start here (progressive disclosure)

- How the effect is actually produced, and the Epic internals it works around → `.claude/refs/architecture.md`. **Read before changing any C++.**
- How overscan and the image circle are computed, and where that model breaks down → `.claude/refs/overscan-and-image-circle.md`
- Whether Unreal can render past 90° off-axis at all, and at what cost → `.claude/refs/wide-field-source.md`
- Every component control and what it is for → `.claude/refs/using-the-component.md`
- The preset/profile data model, the lens catalogue, adding a lens → `.claude/refs/presets-and-profiles.md`
- Where the Lanthimos lens numbers came from, measured vs assumed → `Tools/data/research/lanthimos-lenses.md`
- Whether a restart can be avoided, and the restructure that would help → `.claude/refs/live-coding.md`
- Installing a DLL under a live editor, and why we don't → `.claude/refs/hot-swap.md`
- Work that is researched and waiting on something → `.claude/refs/roadmap.md`. Check it when a
  big piece of work lands; an entry may have just become proposable. **It opens with a handoff
  block; read that first if you are picking this repo up cold.**
- Short jobs that are already decided and blocked on nobody → `.claude/refs/todo.md`. Unlike the
  roadmap, these are meant to be picked up and done, not proposed.
- Proving a look works (stills, per-frame checks, the traps) → `.claude/refs/visual-verification.md`
- Keeping these docs true → `.claude/refs/maintenance.md`
- Where the data and the *ideas* came from, and how each was used → `SOURCES.md` (public)

Behaviour rules auto-load from `.claude/rules/`: editor restarts, the build/install cycle, the
preset data flow, licensing and credits. Read them; they are the ones that bite.

## Layout

| Path | What |
|---|---|
| `Source/DynamicLens` | the C++ module: types and maths, the component, the ST-map library, startup fixups |
| `Content/Presets`, `Content/Profiles`, `Content/Kits`, `Content/Materials` | generated assets, `/DynamicLens/...` in the editor |
| `Content/Python/dynamiclens_tools.py` | editor tools, `import dynamiclens_tools as dl` |
| `Tools/data/presets.json` | **source of truth** for every profile and preset |
| `Tools/data/lens_catalogue.json` | **generated** index of every preset: who measured it, squeeze, focal range, whether it breathes or zooms, how hard it distorts. Read this before walking the asset registry. |
| `Tools/data/raw`, `Tools/data/profiles` | measured grids and the fits built from them |
| `Tools/read_lensfiles.py` | reads Unreal Lens File `.uasset` distortion tables without an editor |
| `Tools/build_dynamiclens.ps1` | build, and install when the editor is closed |
| `Binaries/`, `Intermediate/` | build products, gitignored, never committed |

## Using it on a camera

Nothing to install in a project beyond enabling the plugin; it is already in the engine.

1. Enable **Dynamic Lens** in the project's plugin list. It pulls in `CameraCalibrationCore` and
   `PythonScriptPlugin` itself.
2. Select a CineCameraActor, **Add Component → Dynamic Lens**.
3. Set **Preset**. The prefix says where the lens came from: `DL_AD_*` are Andy Davis's measured
   lenses - the ARRI/Zeiss grids are the clean baseline and breathe, and his spherical ST maps
   under `Profiles/AndyDavis` add 22 series of real character without breathing. `DL_T_*` are
   the tiedtke ST-map lenses with real character and real flaws, `DL_L_*` are the Lanthimos-film reconstructions, `DL_C_*` are Dylan's
   own looks. Catalogue: `.claude/refs/presets-and-profiles.md`. **Browse** beside Preset opens the
   Preset Browser, which filters by optics rather than prefix.
4. The component's **Camera** row drives filmback, focal length, aperture and focus without leaving
   the component. **A1/A2** step presets, **A3/A4** step focal length.
5. Per-camera tweaks go in the **Override** blocks, which never modify the preset asset.
   **Save As New Preset** promotes them to a new asset. Every control:
   `.claude/refs/using-the-component.md`.

Fisheyes are one preset per lens. **Image Circle > Scale** sizes the circle and, on a fisheye, the
whole picture with it (porthole ↔ filled frame); **Field** picks Fit to Circle or True Angles. The old
`_Fit` and `_Frame` variants are those two controls — `.claude/refs/using-the-component.md`.

## Iterating

**If Dylan says "update the plugin", follow `.claude/rules/updating-the-plugin.md` step by step.**
Start with the one call that tells you where things stand:

```powershell
Tools\build_dynamiclens.ps1 -Status      # read-only, safe with the editor open
```

**Content, Python, presets, profiles: no build, no restart.** Edit and it takes effect in the live
editor. Preset and profile changes come from `Tools/data/presets.json` via `dl.import_presets()`.
Most "updates" are this, and finish in one step.

**C++ changes need a build, and installing the result needs the editor closed:**

```powershell
Tools\build_dynamiclens.ps1              # safe while the editor runs; packages to %TEMP%\dlb
Tools\build_dynamiclens.ps1 -InstallOnly # you run it, after Dylan says the editor is free to restart
```

Then relaunch and re-run whatever the change depends on, commonly
`dl.build_image_circle_material(force=True)` and `dl.import_presets()`.

**Never close or restart the editor without Dylan's yes** - then do it yourself, never hand him the command. See `.claude/rules/editor-restarts.md`.

## Editor Python

Run through `unreal-py` (`editor_run_python`) or a remote-exec helper. `import dynamiclens_tools as dl` first.
The host project's own rules apply when driving its editor: read its `.claude/rules/ue-python-patterns.md`
and `ue-visual-verification.md` (e.g. CitySample's) - this repo does not load them. With no MCP, remote exec
works via the `/dyl-ue-setup` skill's `scripts/rexec.py`. Scratch assets go in the project's `/Game/Claude/`.

| Call | Does |
|---|---|
| `dl.status()` | what is installed, which cameras have components |
| `dl.import_presets()` | rewrite preset assets from `presets.json` (`only=[...]` for one) |
| `dl.import_profiles()` / `import_projection_profiles()` / `import_derived_profiles()` | rebuild profile assets |
| `dl.import_tiedtke()` | re-import the ST-map lenses from the tiedtke pack |
| `dl.import_andy_stmaps(root=...)` | import Andy Davis's spherical ST maps (prep with `Tools/prep_andy_stmaps.py`) |
| `dl.import_kits()` | rewrite the `DLK_*` lens kits from the `kits` section of `presets.json` |
| `dl.build_image_circle_material(force=True)` | rebuild the image-circle material after an HLSL change |
| `dl.import_all()` | all of the above, in order |
| `dl.export_catalogue()` | rewrite `Tools/data/lens_catalogue.json` - every preset, its provenance and its optics |
| `dl.resave_presets()` | re-save all 60 presets so their Asset Registry tags are rewritten. **Required after any C++ change to `GetAssetRegistryTags`**, or the Preset Browser filters go stale. `import_presets()` only covers the 19 in `presets.json`. |
| `dl.reset_asset(path)` | restore one asset to its shipped values |
| `dl.add_to_all_cameras(preset=...)` | bulk-add the component |

## Where new things go

| Kind of change | Goes in |
|---|---|
| A new lens, preset, or a tuned value | `Tools/data/presets.json`, then re-import. Never only in the editor. |
| A new control or behaviour | `Source/DynamicLens`, plus the material parameter if it is visual |
| How the plugin works | `.claude/refs/` |
| A behaviour rule for agents | `.claude/rules/` |
| Work worth doing but blocked on something else | `.claude/refs/roadmap.md`, with what it is gated on |
| A small job that is decided and blocked on nobody | `.claude/refs/todo.md`. Delete the entry when it is done. |
| Anything the public should read | `README.md` |
| A question like "what lenses are there / where did this one come from" | `Tools/data/lens_catalogue.json`, regenerated with `dl.export_catalogue()`. Never hand-edit it. |
| A new data source, paper, or borrowed idea | `SOURCES.md`, plus `NOTICE` if it is data |
| How one *film* uses the plugin | that film project's own `.claude/`, never here |

## Version control

Plain **git**, public remote `origin`. Dylan's film projects use Diversion; this repo does not.
Commit the docs and `.claude/` along with the code. Never commit `Binaries/` or `Intermediate/`.
