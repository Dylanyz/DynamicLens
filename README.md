# Dynamic Lens (UE 5.8 plugin)

Focal-length / focus / f-stop driven lens distortion, vignette and bokeh character for CineCameras.
Add a **Dynamic Lens** component to a camera, pick a **preset asset**, done. Works in the editor viewport
(piloting, Realtime on), PIE, and Movie Render Queue / Movie Render Graph.

This folder is the single source of truth. Projects get it through a directory junction into `Plugins/DynamicLens`,
so editing here updates every project that links it.

## Install into a project
```powershell
# from PowerShell, once per project (path of the project's Plugins folder)
New-Item -ItemType Junction -Path "C:\Path\To\Project\Plugins\DynamicLens" -Target "C:\Users\DYLPC\Documents\ProjectHub\UE Assets\Lenses\DynamicLens"
```
Then enable **Dynamic Lens** in the project's Plugins (it pulls in Camera Calibration Core), restart.
Binaries are prebuilt (`Binaries/Win64`) so Blueprint-only projects load it without compiling.

First time in a project, in the Python console:
```python
import dynamiclens_tools as dl
dl.import_profiles(); dl.import_presets()     # creates /DynamicLens/Profiles and /DynamicLens/Presets assets
dl.add_to_all_cameras("DL_Master")            # or add the component by hand: camera > Add > Dynamic Lens
```
(The assets live inside the plugin's Content folder, so after the first import they travel with the plugin.)

## Using it
* **Component** (on the camera): `Enabled` (keyable), `Preset` (asset dropdown), `Amount Multiplier` (keyable),
  Layers: `Apply Vignette`, `Apply Bokeh`; Advanced: `Render Mode` (Post Process Material / Inside TSR),
  `Overscan Multiplier`, `Scale Resolution With Overscan`; Debug: what was evaluated this frame.
* **Preset asset** (`/DynamicLens/Presets`): Distortion (Profile, Amount, Breathing, Wide Boost), Vignette, Bokeh
  (Iris blades, Cat's Eye barrel, Petzval swirl). Every field has a tooltip and hard-limit clamps. Duplicate a preset
  to make your own; the component's dropdown lists every preset asset in the project.
* **Profile asset** (`/DynamicLens/Profiles`): the measured lens series (focal × focus grid of K1..P2). Rebuild from
  `Tools/data/raw` with `Tools/build_profiles.py` → `dl.import_profiles()`.

## Aspect ratios, crops, anamorphic
Distortion is evaluated in normalized sensor space, so it is correct for any filmback. The component uses the
**effective** sensor: filmback × squeeze factor (desqueezed width), then trimmed by the camera's **Crop** preset
(aspect ratio). A 2.39 crop of a 16:9 sensor therefore gets the distortion of the centre of the full frame, as a real
crop would. Output resolution / letterboxing in MRG doesn't change it (the effect lives in the camera's view).
Anamorphic *lens models* (oval distortion, 3DE parameters) are not wired yet: spherical profiles are applied to
anamorphic filmbacks as-is.

## Overscan
The component computes the exact overscan so distorted frames have no empty corners and writes it to the camera's
native `Overscan` (MRG crops it automatically since 5.6). GPU cost grows with overscan²: about 10–15% extra pixels on
a 14–18 mm at minimum focus, ~4% at 35 mm, nothing past 65 mm.

## Rendering modes
* **Post Process Material** (default): camera blendable, one resample. Works everywhere.
* **Inside TSR**: distortion folded into Temporal Super Resolution, no extra resample (sharpest). Needs TSR as the
  AA method; `bCropOverscan` is switched on automatically.

## Rebuilding after code changes
```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<this folder>\DynamicLens.uplugin" -Package="<temp dir>" -TargetPlatforms=Win64 -Rocket
```
then copy `<temp dir>\Binaries` and `<temp dir>\Intermediate` back over this folder and restart the editor.

## Layout
```
DynamicLens.uplugin
Source/DynamicLens/         C++ (component, preset/profile data assets, helper library)
Content/Python/             dynamiclens_tools.py (importer + helpers)
Content/Profiles, Presets/  data assets (created by the importer, travel with the plugin)
Tools/data/raw              dump of the source Lens Files (andy_davis_lensfiles.json)
Tools/data/profiles         fitted grids (JSON)   Tools/data/presets.json  preset definitions
Tools/build_profiles.py     raw → profiles        Tools/lensrig_py_prototype  the first Python-only version (reference)
```
