# The Dynamic Lens component — every control

Added to a CineCameraActor. Everything below lives in the **Dynamic Lens** category on the component.

## Top level

| Control | What it does |
|---|---|
| **Enabled** | Master switch. Off restores the camera untouched. |
| **Preset** | The lens. See `presets-and-profiles.md` for the catalogue. Presets hidden in the Preset Browser are left out of this dropdown. |
| **Kit** | A case of lenses (`DLK_*`, `/DynamicLens/Kits`). Set, it picks **Preset** from the camera's focal length every frame, nearest in log-focal, so in Sequencer you key only the CineCamera's focal (Constant interpolation) and the lens follows. Overrides a Preset track and the preset buttons; Notes names the lens it picked. Keyable, to change cases between scenes. Empty = Preset as usual. |
| **Kit Snaps Focal** | With a kit, hold the camera at the picked lens's focal. Off lets the camera sit between, though a prime preset still locks its own focal. |
| **Profile Info** | Read-only. Lists the profile's measured focal lengths with the current one marked `[current]`. Useful for knowing whether you are inside measured data or extrapolating. |
| **Amount Multiplier** | Scales the whole effect. 0 = off, 1 = as measured, >1 = exaggerated. |
| **Apply Distortion / Vignette / Bokeh / Image Circle** | Layer toggles. Turning distortion off also clears the camera's distortion rendering, so the frame goes back to undistorted rather than freezing. |
| **Force Bokeh Quality** (advanced, default on) | Below Cinematic scalability Unreal drops the DOF bokeh simulation, so swirl, cat's eye and blade shape vanish with no error. This raises the four DOF cvars that matter while the camera applies bokeh, and restores them afterwards. Turn it off to leave scalability alone; Notes then warns when the look is being lost. |
| **Preset in Sequencer** | Keyable: add the Dynamic Lens component to a sequence and key **Preset**; each key is a lens change on that frame (stepped). It swaps the lens only - Match Camera is not run and nothing is dirtied. Key focal length, focus and aperture on the CineCamera itself; the component's Camera row cannot create keys. On a locked (prime or ST-map series) preset a focal curve jumps between primes; Notes says so when it happens. `.claude/refs/sequencer-integration.md` |
| **Sensor Fit** | `Crop` keeps an ST map at the lens's physical scale and crops the camera's sensor out of it. `Scale` stretches the map to the sensor. Crop is correct; Scale is the fallback when the sensor is larger than the profile's. |

## Buttons

Details panels sort `CallInEditor` buttons alphabetically, hence the prefixes.

| Button | Does |
|---|---|
| **A1 Previous Preset** / **A2 Next Preset** | Step through preset assets alphabetically, skipping any hidden in the Preset Browser. |
| **A3 Previous Focal** / **A4 Next Focal** | Step to the profile's next measured focal length. Snaps to real data instead of guessing. |
| **Match Camera To Profile** | Applies the profile's filmback, squeeze, crop and focal length to the camera. |
| **Copy All From Preset** | Refreshes every Override block with the preset's current values, leaving the toggles off. |
| **Save As New Preset** | Writes the current resolved settings, overrides included, to a new preset asset. |
| **Resolve Settings** / **Update Profile Info** / **Clear Effect** | Diagnostics and reset. |

## Camera (quick settings)

A flat row on the component so you do not have to jump to the camera: focal length, aperture, focus
method, manual focus distance, actor to track, focus offset, cropped aspect ratio, filmback and
squeeze factor. Writes straight through to the CineCamera.

**Filmback is how you change the size of the image circle relative to the frame.** A smaller filmback
makes the circle relatively larger. That is exactly what the `_Frame` preset variants do.

## Match Camera

A set of checkboxes controlling what **Match Camera To Profile** and automatic matching touch:
on preset change, filmback, squeeze, crop, focal length, and **Refresh Overrides** (which re-copies
the preset into the Override blocks so they always show the current lens).

## Override blocks

`Distortion`, `Image Circle`, `Vignette`, `Bokeh`, `Overscan`. Each has an inline toggle. Ticking one
copies the preset's block in as a starting point, then that block is yours for this camera only. The
preset asset is never modified. Untick to go back to the preset.

### Distortion
Profile, **Lock Focal Length** (for primes, pins the camera to the profile's nominal focal),
Amount, Breathing, Out Of Range mode, Wide Boost.

*Out Of Range*: `Clamp` rescales the coefficients to the new focal and is right almost always;
`Clamp Raw` is the un-rescaled legacy behaviour kept only because `DL_C_Vintage_Raw` depends on it;
`Extrapolate` continues the fit.

*Wide Boost* increases distortion as focal length drops below the measured range, so an ultra-wide
made from a normal lens's data still bends convincingly.

### Image Circle
**Enabled**, **Scale Relative To**, **Scale**, **Field**, **Softness**, and an **Edge** group.
One **Scale** slider shows; what 1 means depends on **Scale Relative To** (2026-09-25):

- **Lens** (`SizeMode = Physical`): 1 = the lens's own circle. A smaller filmback makes it bigger in
  frame, as on a real camera.
- **Frame** (`SizeMode = Coverage`, the `CircleCoverage` property): circle diameter / frame diagonal
  (sensor before desqueeze, crop applied), whatever the filmback, focal or crop. 0.6 porthole, 1 the
  circle touches the corners, 1.1–1.3 fills the frame. The fisheye presets open at 1.1.
- **On a fisheye, Scale scales the whole picture**, not just the mask: past the lens's own field the
  fisheye image is enlarged with the circle, as a smaller filmback would. This replaced the `_Frame`
  presets (and their small filmbacks).
- **Field** (fisheyes only): *Fit to Circle* (default) compresses the field only when the render runs
  out, so the circle is round and the right size; *True Angles* keeps the lens's exact bend and the
  picture ends early in an oval. This replaced the `_Fit` presets.
- Fisheye overscan is Dynamic up to 4: it renders only what the circle-and-frame needs, so a filled
  frame renders less and stays sharper. Notes report centre sharpness for the viewport and for Movie
  Render Graph. Debug shows **Circle Coverage** and **Fit Field Scale** (1 = the true lens).
- A profile with no image circle uses its sensor diagonal.
- On an anamorphic camera the lens circle is drawn as an ellipse, round on the squeezed sensor.

- **Falloff Power**, **Opacity** — how hard and how black the rim is. Opacity below 1 leaves a faint
  image, like light leaking round a gate.
- **Fade** (Reach, Amount, Curve) — a vignette-like darkening starting a fraction of the radius
  inside the circle and reaching Amount at the edge, underneath the rim's own rolloff.
- **Center Offset**, **Ellipticity**, **Wobble** / lobes / seed, **Falloff Wobble** / lobes / seed —
  the imperfection controls. A real gate is not a perfect circle.
- **Chromatic Amount** with per-channel **Red / Green / Blue** offsets under a Channels sub-group.
  Amount is the slider you reach for; the channels default to red in, blue out.
- **Scatter** — an 8-tap radial glow at the rim.
- **Edge Noise**, Noise Scale / Depth / Blur / Detail / Stretch / Seed / Contrast — grain in the
  rim itself.
- **Mask Texture** and **Mask Strength** — hand-painted gate shapes.

### Vignette
Standard falloff darkening, re-evaluated against wherever the mask actually lands so the two do not
double up.

### Bokeh
Blade count and **Blade Source** (profile or camera), **Blade Curvature**, **Squeeze** and its
source, **Drive Accumulation DOF**, **Spherical Aberration**, **Coma**, **Blade Rotation**.

Blades and squeeze are pushed through the camera's `LensSettings`, because a CineCamera overwrites
the post-process values every frame. Squeeze visibly changes the verticality of the bokeh; Dylan
confirmed this is working as intended.

### Overscan
`Dynamic` (recommended) or `Fixed`, with Max Overscan, Dynamic Step and Scale Resolution With
Overscan. Dynamic computes what the distortion needs, quantised to 2% with hysteresis, held constant
across the focus range so a focus pull never resizes the render target.
