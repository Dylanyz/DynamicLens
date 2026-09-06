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
* `Enabled`, `Preset` (asset dropdown), `Profile Info` (read-only: the lens, its coverage, and exactly what Match Camera
  To Profile would set), `Match Camera On Preset Change`, `Amount Multiplier` - keyable in Sequencer. Buttons:
  `Match Camera To Profile`, `Copy All From Preset`, `Save As New Preset`.
* Layers: `Apply Vignette`, `Apply Bokeh`, `Apply Image Circle`, `Vignette Multiplier`, `Swirl Multiplier` (keyable).
* **Overrides**: the preset's five blocks (Distortion, Image Circle, Vignette, Bokeh, Overscan) as collapsed groups,
  each with a checkbox. Ticking one copies the preset's values in and uses them for this camera only; the preset
  asset is never changed. `Copy All From Preset` fills every block without turning them on.
* **Save As Preset**: `New Preset Name` + folder (default `/Game/DynamicLens/Presets`) and the `Save As New Preset`
  button write the resolved look (preset + overrides) to a new asset, switch this camera to it and clear the overrides.
* Sensor: `Sensor Fit` - **Scale** (profile frame stretched to this sensor) or **Crop** (physical: a smaller sensor sees
  the centre of the lens grid). `Match Camera To Profile` sets filmback, squeeze, crop and (for primes) focal length to
  the profile's native format.
* Advanced: `Render Mode` (Post Process Material / Inside TSR), image-circle material.
* Debug: focal, focus, f-stop, effective sensor, K values, needed vs applied overscan, corner field angle, pupil
  visibility at the corner, barrel radius/length, image-circle radius, profile coverage, notes.

## Preset asset
Five blocks, all with tooltips and hard clamps:
* **Distortion**: profile, `Lock Focal Length` (primes hold the camera at the profile's nominal focal length; ST-map
  series snap to the nearest measured prime), amount, breathing, out-of-range clamp/extrapolate, wide boost (a
  creative layer: extra barrel below a focal length - off in measured presets).
* **Image Circle**: on/off, softness (rolloff band as a fraction of the radius) and the **Edge** block: falloff power,
  opacity; Geometry: centre offset, ellipticity, radius waviness (wobble / lobes / seed) and, separately, falloff-width
  waviness (the band gets wider and narrower around the circle: falloff wobble / lobes / seed); Optics: per-channel
  chromatic offsets (red / green / blue edge radius - blue rim, red in), radial scatter glow; Texture: breakup amount,
  scale, depth (how far inside the circle it reaches), blur, detail, optional full-frame mask texture you paint/scan.
  Every preset and profile has a `Reset To Shipped` button that re-imports it from `Tools/data`.
* **Vignette**: Physical (cos^4 + barrel clipping) / Manual curves.
* **Bokeh**: Physical (blades + barrel from specs, cat's-eye strength) / Manual, Iris (blade count from **Profile /
  Camera / Custom**, blade curvature override, bokeh squeeze from **Profile / Camera / Custom**, blade rotation),
  Swirl (Petzval amount, falloff, exclusion box, fade by f-stop), Accumulation DOF (drive on/off, spherical
  aberration, coma).
* **Overscan**: Dynamic (exact per frame, capped by max) or Fixed (constant for the shot - renders with zoom pulls,
  and fisheyes, which ship with Fixed 2.0). Beyond the available overscan the frame goes black at the edges.

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

## Asset prefixes
`DL_` presets and `DLP_` profiles carry a source tag: **AD** = Andy Davis (Imagery for Media) measured Lens Files,
**T** = tiedtke Real Cinema Lenses ST maps, **L** = Lanthimos-film reconstructions (The Favourite, Poor Things),
**C** = creative looks by the plugin author. Everything not from a data sheet is marked "assumed" in the Source field.

## Presets shipped
Master, Supreme (as measured), MasterHeavy, Subtle, Vintage, Lanthimos_Favourite_6mm (Nikkor 6mm 220°),
Lanthimos_Favourite_10mm (stereographic reconstruction) and _10mm_Rect (rectilinear reconstruction),
L_PoorThings_4mm_Porthole (OpTex 4mm S16 on 35), L_PoorThings_8mm and 8mm_Frame (Oppenheimer/Nikkor, full frame with
rolling corners), L_PoorThings_Petzval_58 / _85 (round Waterhouse iris, swirl), L_PoorThings_UltraPrime_10mm
(placeholder distortion), L_PoorThings_MasterZoom_16-110, L_PoorThings_Optimo_24-290, L_PoorThings_VistaVision_LeicaR
(the reanimation sequence; use a 1.5:1 filmback) - lens list from Noam Kroll's article on the film - plus one per tiedtke series
(`Presets/Tiedtke/DL_T_*`). Every number that is not from a data sheet is marked "assumed" in the profile's Source field.

## Film formats behind the Lanthimos presets
* *The Favourite*: Panavision Millennium XL2, 35 mm 4-perf, released 1.85:1 -> native gate 24.89 x 13.45 mm
  (Super 35 1.85 extraction). Lenses: Panavision Primo close-focus primes, Panavision-mount Nikkor 6 mm fisheye,
  the "10 mm" workhorse.
* *Poor Things*: ARRICAM LT/ST, 35 mm 4-perf, 1.66:1 -> native gate 24.89 x 15.0 mm. Lenses: OpTex 4 mm S16
  fisheye (the porthole), Nikkor 8 mm, Zeiss Master Zoom 16.5-110, Angenieux Optimo, Petzval 58/85.
Sources: Kodak and Cinematography World interviews with Robbie Ryan (links in the profiles' Source fields).

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

## Credits
* **tiedtke** - the ST-map profiles under `Content/Profiles/Tiedtke` (and their textures) are built from his free
  *Real Cinema Lenses* pack: real lens grids shot on an ARRI Mini and converted to ST maps in Nuke.
  https://tiedtke.gumroad.com/l/realcinemalenses?layout=profile
* **Andy Davis (Imagery for Media)** - ARRI/Zeiss Master Prime and Zeiss Supreme distortion grids (VFX RnD Lens Files).
