# Dynamic Lens (UE 5.8 plugin)

Lens character for CineCameras that follows **focal length, focus distance and f-stop** every frame: distortion,
vignette, image circle and bokeh, in the editor viewport, PIE and Movie Render Queue / Graph.

Add a **Dynamic Lens** component to a camera, pick a **preset asset**, done. Three kinds of lens data:

| Profile type | What it is | Zoomable | Source |
|---|---|---|---|
| **Parametric** | K1..P2 on a focal × focus grid | yes, any focal / focus | measured Lens Files (Andy Davis) |
| **ST Map** | one measured UV map per prime | nearest prime is used | tiedtke's grids |
| **Projection** | ideal fisheye maths (equidistant, stereographic, equisolid, orthographic) | yes | data sheets |

Everything else is derived from physics and data-sheet numbers, not tuned by eye (see *Maths* below).

## Install (this folder is the source of truth)
```powershell
# engine-level: every 5.8 project can enable it
New-Item -ItemType Junction -Path "C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\Marketplace\DynamicLens" -Target "C:\Users\DYLPC\Desktop\Coding\DynamicLens"
```
Enable **Dynamic Lens** in the project (pulls in Camera Calibration Core). Binaries are prebuilt in `Binaries/Win64`.

First time in a project, Python console:
```python
import dynamiclens_tools as dl
dl.import_all()          # image-circle material, profiles, fisheye profiles, presets, tiedtke ST maps (if the pack is in /Game/CinematicTemplate/Lenses)
dl.add_to_all_cameras("DL_Master")
```
Assets land in the plugin's own content (`/DynamicLens/Profiles`, `/DynamicLens/Presets`, `/DynamicLens/Materials`).

## Component (on the camera)
* `Enabled`, `Preset` (asset dropdown), `Amount Multiplier` — keyable in Sequencer.
* Layers: `Apply Vignette`, `Apply Bokeh`, `Apply Image Circle`, `Vignette Multiplier`, `Swirl Multiplier` (keyable).
* Sensor: `Sensor Fit` — **Scale** (profile frame stretched to this sensor) or **Crop** (physical: a smaller sensor sees
  the centre of the lens grid). `Match Camera To Profile` button sets filmback, squeeze and crop to the profile's native format.
* Overscan: **Dynamic** (exact per frame, capped by `Max Overscan`) or **Fixed** (constant for the shot — use this for
  renders with zoom pulls: MRQ/MRG read the camera's overscan once per shot). Beyond the available overscan the frame goes
  black at the edges (image circle), like a real lens.
* Advanced: `Render Mode` (Post Process Material / Inside TSR), image-circle material.
* Debug: focal, focus, f-stop, effective sensor, K values, needed vs applied overscan, corner field angle, pupil
  visibility at the corner, barrel radius/length, image-circle radius, profile coverage, notes.

## Preset asset
Distortion (profile, amount, breathing, out-of-range clamp/extrapolate, wide boost), Image Circle (on/off, edge softness, and an
**Edge** block for a real-looking rim: falloff power, opacity, centre offset, ellipticity, waviness, fine breakup, chromatic
aberration (blue rim), radial scatter, optional full-frame mask texture you paint/scan yourself),
Vignette (Physical: cos⁴ + barrel clipping / Manual curves), Bokeh (Physical: blades + barrel from specs, cat's-eye
strength / Manual), Iris (blade count from **Profile / Camera / Custom**, blade curvature override, bokeh squeeze from
**Profile / Camera / Custom**, blade rotation), Swirl (Petzval amount, falloff, exclusion box, fade by f-stop), Accumulation
DOF (drive on/off, spherical aberration, coma). Tooltips and hard clamps everywhere.

`DL_Lanthimos_Favourite_6mm_Frame` reproduces the film's framing (full frame, only the corners roll off): a 6 mm
equidistant image only reaches the corners of a 1.85:1 gate if the corner sits within ~78° off-axis (the most a
rectilinear Unreal render can source at overscan 2), so the profile's native gate is 14.4 x 7.78 mm (the 35 mm scan
blown up ~1.7x) - click Match Camera To Profile and set Overscan Fixed 2.0. The plain `DL_Lanthimos_Favourite_6mm`
shows the whole 23 mm circle on a 35 mm gate.

The Poor Things 4 mm and The Favourite presets carry edge values measured from stills (see `Tools/data/presets.json`
"notes"): the 4 mm rolls off over ~25% of the circle radius with a blue rim at the very edge and sits 5% left / 3% high of
centre; the 6 mm has no rim at all, only corners rolling off over ~45% of the radius, top darker than bottom.

## Profile asset
Type, coverage summary (read-only), native sensor + squeeze + image circle, the data, and physical specs:
front diameter, iris blades, max aperture, **pupil visibility at the image-circle edge**.

## Maths
* **Distortion**: Brown-Conrady radial (Unreal normalized convention, same as Lens Files) or ST maps through Epic's
  displacement-map pipeline; fisheyes as procedurally generated undistortion maps from r = f·g(θ).
* **Overscan**: exact — dense border inversion of the (monotonic) radial model; ST maps measured at import from the map
  border; fisheyes from the source-frame limits. Coefficients are scaled down if the mapping would fold over.
* **Image circle**: profile's data-sheet circle, and the circle beyond which the overscan provides no source pixels.
* **Cat's eye**: UE's barrel model (cylinder of radius R, length L in front of the entrance pupil of diameter f/N).
  R = front diameter / 2. L is solved so that the pupil is `PupilVisibleAtImageCircle` visible at the image-circle edge
  wide open (disc-overlap geometry), then the same geometry gives clipping at any focal, f-stop and sensor.
* **Vignette**: natural cos⁴(θ) falloff at the visible frame edge × `NaturalFalloff` + mechanical loss (1 − pupil
  visible) × `MechanicalStrength`. Evaluated at the image-circle edge when the circle is inside the frame.
* **Swirl** (Petzval): not derivable; 0 for modern primes, manual for vintage looks, fades with the iris.

## Presets shipped
Master, Supreme (as measured), MasterHeavy, Subtle, Vintage, Lanthimos_Favourite_6mm (Nikkor 6mm 220°),
Lanthimos_Favourite_10mm (stereographic reconstruction) and _10mm_Rect (rectilinear reconstruction),
PoorThings_Porthole_4mm (OpTex 4mm S16 on 35), PoorThings_Lab_8mm, PoorThings_Petzval, plus one per tiedtke series
(`Presets/Tiedtke/DL_T_*`). Every number that is not from a data sheet is marked "assumed" in the profile's Source field.

## Known limits
* Fisheyes beyond ~80° off-axis can't be rendered by a rectilinear source; the image circle is black there. A 16:9
  source runs out vertically first — use a 4:3 / open-gate filmback (Match Camera To Profile) for the biggest circle.
* Editor viewport needs Realtime on (Ctrl+R) for the component to tick; renders always tick.
* Black Eye cameras: their actors are Cine Camera actors and the dynamic FOV is written to the cine camera's focal length,
  so add the component to the Black Eye camera blueprint like any other camera. The component ticks after its owning
  actor, so the distortion follows the FOV in the same frame.
* Accumulation DOF: if the camera actor also has an `AccumulationDOF` component (5.8 plugin), the component drives its
  bokeh texture with a procedural N-gon iris (blade count + rotation from the profile/preset) and passes spherical
  aberration / coma through; the user's own values are restored when the effect is disabled. Cat's-eye clipping is
  not reproduced there yet (Accumulation DOF has no barrel model), only in DiaphragmDOF.

## Rebuild
```
RunUAT.bat BuildPlugin -Plugin="<repo>\DynamicLens.uplugin" -Package="%TEMP%\dlb" -TargetPlatforms=Win64 -Rocket
```
Short package path (MAX_PATH). Needs the .NET Framework 4.8 SDK (VS Installer) or the `UE_SDKS_ROOT` stub trick.
Copy `Binaries` + `Intermediate` back, restart the editor.

## Layout
```
Source/DynamicLens/          component, types (profiles/presets/maths), library
Content/Python/              dynamiclens_tools.py (importers, material builder, helpers)
Content/Profiles|Presets|Materials  generated assets (travel with the plugin)
Tools/data/raw               dump of Andy Davis's 31 Lens Files
Tools/data/profiles          fitted grids (Tools/build_profiles.py)
Tools/data/presets.json      profile specs, fisheye profiles, presets (with sources)
```
