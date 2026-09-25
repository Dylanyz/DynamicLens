# Verifying a look — stills, frame-by-frame checks, and what lies

How to prove a lens change works, learned 2026-09-25 when every `DL_L_*` fisheye turned out to have
applied no distortion since v0.3 while its numbers looked fine. **Numbers off the component are not
proof. Look at pixels.**

## Stills you can send

- **`unreal.AutomationLibrary.take_high_res_screenshot(w, h, abs_path, camera_actor)`** renders from
  the camera with its post-process chain. It matched the piloted Lit viewport exactly. Size it to the
  filmback aspect (`w * SensorHeight / SensorWidth`) or the circle stretches into an oval that isn't
  really there. It writes wherever `abs_path` says; use the session scratchpad, never `Saved/`.
- **Pilot + Lit** for eyes: `LevelEditorSubsystem.pilot_level_actor(cam)` and `viewmode lit`.
  Computer-use screenshots see the editor even under the Claude window; script-side screen grabs don't.
- **Send JPEG contact sheets** (PIL, ~1200-1900 px wide, `display: attach`). PNG renders did not
  reach Dylan over Remote Control.
- **Overrides from Python: tick the override *before* writing the struct.** Ticking
  `override_image_circle` (or any `override_*`) copies the preset's values in and overwrites whatever
  you just set. Read the preset's values with `resolve_settings().image_circle`, edit, then:
  `set_editor_property("override_image_circle", True)` *then* `set_editor_property("image_circle", ic)`.

## Is the distortion actually on screen?

The distortion MID is on the camera's blendables and its outer is the lens handler. The map it
samples is `DistortionDisplacementMap`:

```python
mid = [b.get_editor_property("object") for b in cam.get_editor_property("post_process_settings")
       .get_editor_property("weighted_blendables").get_editor_property("array")][0]
rt = mid.get_texture_parameter_value("DistortionDisplacementMap")
unreal.RenderingLibrary.export_render_target(world, rt, out_dir, "disp.png")
```

- The export is a **16-bit PNG whatever the extension**, negatives clipped. A ~158 KB file for a 2048
  map is all zeros, meaning no distortion. A real map is several MB.
- `read_render_target_raw_uv` on the RG16F map returned a constant (1, 0): don't trust it.
- `mid.get_outer().get_undistortion_displacement_map()` is the other slot. If only one is filled,
  suspect `MapFormat` channels (`None` in one slot is what broke the fisheyes).

## Frame by frame

Transient bugs hide from any single screenshot. Register
`unreal.register_slate_post_tick_callback`, export the displacement map and log `cam.overscan` each
tick for ~12 ticks after the change, then unregister. This is how the ~2-frame wide flash after every
fisheye rebuild was found and proven fixed (`architecture.md`, gotchas).

## The raw render

To see what a fisheye is built from: component `enabled = False`, set `cam.overscan = O - 1` and
`crop_overscan = False`, still, then re-enable. On the 8 mm at Scale 1.1 only ~27% of the raw pixels
reach the frame. Most of the rest is rectilinear edge stretch, plus the top and bottom that a round
fisheye crops off a wide frame. That is a real-lens crop, not a bug.

## Traps

- **CitySample PIE smears with any CineCamera overscan > 0**, plugin or not. Use the editor viewport,
  stills, or Movie Render Graph.
- **The Windows Start menu** can pop over the editor during automation. Dismissing it needs a
  File Explorer (click-only) grant and a click on the editor title bar. The viewport stalls behind it,
  so wait a second before judging a frame.
- **Test level:** `/Game/DynamicLensTest/L_DLTest`, camera `DLTest_Cam`. The white dot grid in its sky
  is scene geometry, and a useful straight-line chart.
