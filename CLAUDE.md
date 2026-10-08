# DynamicLens — measured lens character for Unreal CineCameras

A C++ UE 5.8 plugin by Dylan Gitalis (Mad Rice). Add a **Dynamic Lens** component to a CineCameraActor,
pick a preset, and the camera gets the distortion, image circle, vignette and bokeh of a real lens,
driven live by focal length, focus distance and f-stop. Profiles are fitted from measured cinema-lens
data, not invented. Source-available (CPAL-1.0 + Commons Clause), at https://github.com/Dylanyz/DynamicLens.

**This repo *is* the installed plugin.** `Engine\Plugins\Marketplace\DynamicLens` in the engine
install is a **directory junction to this folder**, not a copy. So editing a `.uasset` or a `.py`
here edits the live plugin, with no sync step. Only C++ needs a build.

Building, installing, Live Coding, the licensing pattern and the wrap-up checklist are shared by all of
Dylan's plugins and live in the plugin hub (`../CLAUDE.md`, loaded automatically, and `../.claude/refs/`);
closing the editor for an install is `/ue-agent-control`. This file holds only what is DynamicLens's.

## Hard rules

1. **Never put third-party lens data under this repo's licence.** `Content/Profiles/Tiedtke/**` and
   `Tools/data/raw/**` belong to tiedtke and Andy Davis. `.claude/rules/licensing-and-credits.md`.
2. **`Tools/data/presets.json` is the source of truth**, not the generated assets. An editor-only
   tweak dies at the next import. `.claude/rules/preset-data-flow.md`.

## Start here (progressive disclosure)

- How the effect is actually produced, and the Epic internals it works around → `.claude/refs/architecture.md`. **Read before changing any C++.**
- How overscan and the image circle are computed, and where that model breaks down → `.claude/refs/overscan-and-image-circle.md`
- Whether Unreal can render past 90° off-axis at all, and at what cost → `.claude/refs/wide-field-source.md`
- Every component control and what it is for → `.claude/refs/using-the-component.md`
- The preset/profile data model, the lens catalogue, adding a lens → `.claude/refs/presets-and-profiles.md`
- Where the Lanthimos lens numbers came from, measured vs assumed → `Tools/data/research/lanthimos-lenses.md`
- Whether a restart can be avoided, and the restructure that would help → `.claude/refs/live-coding.md`
  (measured here); installing a DLL under a live editor, and why we don't → plugin hub `refs/live-coding.md`
- Work that is researched and waiting on something → `.claude/refs/roadmap.md`. Check it when a
  big piece of work lands; an entry may have just become proposable. **It opens with a handoff
  block; read that first if you are picking this repo up cold.**
- Short jobs that are already decided and blocked on nobody → `.claude/refs/todo.md`. Unlike the
  roadmap, these are meant to be picked up and done, not proposed.
- Proving a look works (stills, per-frame checks, the traps) → `.claude/refs/visual-verification.md`
- Keeping these docs true → plugin hub `refs/maintenance.md`, then this repo's `.claude/refs/maintenance.md`
- Where the data and the *ideas* came from, and how each was used → `SOURCES.md` (public)

Behaviour rules auto-load from `.claude/rules/`: the preset data flow, licensing and credits. Read them;
they are the ones that bite.

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

## Iterating ("update the plugin")

Start with `Tools\build_dynamiclens.ps1 -Status` (read-only, safe with the editor open). Then say which
kind of change it is; most "updates" are not C++ and finish in one step with nobody closing anything:

| If the change is | Then |
|---|---|
| A preset or profile value, a new lens | edit `Tools/data/presets.json`, then `dl.import_presets()`. Live, no restart. |
| The image-circle look (HLSL) | `dl.build_image_circle_material(force=True)`. Live, no restart. |
| Editor tooling in `dynamiclens_tools.py` | re-import the module in the editor. Live. |
| A new control, new maths, anything in `Source/` | build, then install (plugin hub `refs/build-install.md`; closing the editor: `/ue-agent-control`). Package dir `%TEMP%\dlb`. |

**After an install,** re-run whatever the change depends on, then confirm it landed:

```python
import dynamiclens_tools as dl
dl.build_image_circle_material(force=True)   # only if the material HLSL changed
dl.import_presets()                          # REQUIRED if any preset field was added or renamed
dl.status()
```

**The re-import is not optional when a field was added.** A new field reads as zero in existing assets,
so without it every shipped preset silently loses the new behaviour (exactly what happened when chromatic
aberration became an amount over per-channel offsets). `dl.import_presets()` **overwrites** preset
assets: if Dylan has hand-tweaked one in the editor, ask first and get the tweak into `presets.json`.
Then verify in the editor, at more than one focal length, that the change does what he asked.

| Failure (DynamicLens only; shared rows: plugin hub `build-install.md`) | Cause | Fix |
|---|---|---|
| New property exists but does nothing | preset assets predate it | `dl.import_presets()` |
| Post-process chain goes blank after a material rebuild | orphaned parentless MID | `ClearEffect()`, or reselect the camera |
| Neon speckle in a band around the rim, only in Movie Render Graph renders | image-circle HLSL sampled the scene with viewport UV instead of buffer UV | fixed 2026-09-18; any new scene sample in that HLSL needs `ViewportUVToSceneTextureUV` + `ClampSceneTextureUV` |
| Stutter, and the lens popping on, at Sequencer cuts | each cut spawns the camera; ST-map and fisheye maps + lens files were rebuilt per camera | fixed 2026-10-08 by the shared cache (`architecture.md`); the first cut to a lens per session: Prewarm Lenses. `DynamicLens.Cache.Status` shows what is held |
| Focal length creeps down on a Black Eye camera after preset changes | `ClearEffect` dropped the overscan guard's seed | fixed 2026-10-08 (Apply, and an edit of the camera's focal, seed it); a focal *keyed in Sequencer* still sits one factor short after a key change on zoom presets (reproduced, roadmap) |
| Movie Render Graph render is ~Overscan x tighter than the viewport | Post Process Material render mode: MRG applies camera overscan twice on the `bCropOverscan == false` path | set the component's **Render Mode** to Temporal Super Resolution; see the overscan section in `.claude/refs/architecture.md` |

## Editor Python

Run through `unreal-py` (`editor_run_python`) or remote exec (`/ue-agent-control`). `import dynamiclens_tools as dl`
first. Scratch assets go in the project's `/Game/Claude/`.

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
| `dl.prewarm(sequence=None)` | ready every lens the focused edit's shots use (= Sequencer toolbar ▸ Prewarm Lenses, `DynamicLens.Prewarm`) |

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
