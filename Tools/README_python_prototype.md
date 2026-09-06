# DynamicLens (UE 5.8)

Engine-wide lens distortion + bokeh that follows the active camera's **focal length, focus distance and f-stop**
continuously. Profiles are fitted from Andy Davis's measured ARRI Master / Zeiss Supreme lens files; presets layer
"vibes" on top (amount, breathing, wide-end fisheye boost, vignette, blade count, Petzval swirl, cat's-eye barrel).

No Lens File assets, no Lens Component on cameras: a transient Epic distortion handler + camera overscan is driven
from Python every frame. Works on the piloted camera, the free viewport, and MRQ/MRG renders (PIE view target).

## Install in another project
1. Copy this whole `DynamicLens/` folder (keep it under `Content/CinematicTemplate/Lenses/` or update the paths below).
2. Enable plugins: **Camera Calibration**, **Python Editor Script Plugin**.
3. `Config/DefaultEngine.ini`:
   ```ini
   [/Script/PythonScriptPlugin.PythonScriptPluginSettings]
   +AdditionalPaths=(Path="Content/CinematicTemplate/Lenses/DynamicLens/Python")
   ```
   (or Project Settings → Plugins → Python → Additional Paths). Restart: `init_unreal.py` starts the rig.
4. If the folder moved, update `SETTINGS_CLASS` in `Python/lensrig/rig.py` (path of `BP_DynamicLens`).

## Use
* Drop **`BP_DynamicLens`** into the level. Its Details panel is the UI: `Enabled`, `Preset` (name from
  `presets.json`), `Bokeh`, `Vignette`, `FreeViewport`, `ViewportFocusCm`. One per level; keyable in Sequencer.
* Or from the Python console: `import lensrig; lensrig.on("Master")`, `lensrig.set_preset("Vintage")`,
  `lensrig.off()`, `lensrig.status()`, `lensrig.reload()` after editing presets.
* Viewport must be **Realtime** (Ctrl+R) to see it update live while you zoom; with Realtime off it refreshes on
  each change the rig makes.

## Presets (`Python/lensrig/data/presets.json`)
`Master`, `Supreme` (as measured), `MasterHeavy` (x1.8 + fisheye under 24 mm, The Favourite vibe), `Subtle`,
`Vintage` (Petzval swirl, heavy cat's eye). Edit the JSON → `lensrig.reload()`.

## Data
* `data/raw/andy_davis_lensfiles.json` — dump of all 31 lens files (distortion / focal length / image centre points).
* `data/profiles/*.json` — regular grids (focal × focus cm → K1..P2) built by `build_profiles.py`.
* Focus column = UE focus distance in cm (confirmed: LensComponent feeds `CurrentFocusDistance` straight in).

## Notes
* Rendering path is Epic's post-process distortion material (camera blendable) + native camera overscan
  (`CameraComponent.overscan`, crop off). MRG crops camera overscan automatically (5.6+).
* The rig only touches the active camera; it restores the camera's overscan and post-process overrides when it
  moves on or is disabled. Do not save a level while a camera carries the transient blendable? — safe: the MID is
  transient and simply drops on reload; the rig re-adds it.
* Anamorphic model support is wired (`_MODEL["anamorphic"]`) but no anamorphic profiles exist yet (tiedtke ST maps
  could be fitted; their texture refs point at `/Game/Lenses/...` and are currently broken in this project).
