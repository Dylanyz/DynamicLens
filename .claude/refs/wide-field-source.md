# Getting more than 90° off-axis out of Unreal

Researched 2026-09-19, two parallel investigations: a read of the UE 5.8 engine source, and a survey
of what the fisheye/fulldome community actually ships. Written down because it is expensive to
re-derive and it decides the shape of the `DL_L_*` fisheye rework.

**Outcome (2026-09-25):** Dylan rejected the cube capture ("it breaks too much"). The fisheyes stay
one-faced, with Scale and Field controls (`using-the-component.md`). This doc stays as the record of why
there is no cheap way past ~83 deg.

**The question.** DynamicLens bakes analytic fisheye by resampling one rectilinear scene render.
A rectilinear render's radius goes as `f·tan θ`, so it cannot supply 90° at any finite overscan, and
in practice the plugin tops out near 81°. Every `DL_L_*` preset is squeezed into that, which is why
a 4 mm 180° fisheye and a 10 mm look the same. See `.claude/refs/overscan-and-image-circle.md`.

## The short answer

**Nothing in UE 5.8 rasterises a curved projection.** `FSceneView` classifies a projection purely by
`ProjMatrix.M[3][3] < 1.0f` (`Engine\Source\Runtime\Engine\Private\SceneView.cpp:549`) — perspective
or orthographic, both 4×4 projective. One homogeneous divide means straight edges stay straight and
the field is strictly under 180°. Off-axis, oblique and asymmetric frusta are fully supported
(`FViewMatrices::InvertProjectionMatrix`, `SceneView.h:799-838`) and that is the entire freedom.

So every wide-field feature in the engine is one of two things:

1. **N rectilinear renders resampled** into a panorama — Movie Graph panoramic, PanoramicCapture,
   SceneCaptureCube, nDisplay.
2. **A post-pass UV remap of one rectilinear render** — Panini, MPCDI/EasyBlend/VIOSO warp, nDisplay
   OutputRemap. This cannot recover rays outside the frustum, and is exactly what DynamicLens
   already does.

The community survey reached the same conclusion independently: **nobody does it in one pass.** Every
working technique renders 4–6 rectilinear views and resamples. The plugin's architecture is not
wrong, it is *one-faced*. The upgrade is more faces, not a wider projection.

## The overscan ceiling is ours, not Epic's

Worth knowing on its own, separate from fisheye:

- `UCameraComponent::Overscan` carries `ClampMax="1.0"` (`Classes\Camera\CameraComponent.h:135-136`)
  but that is **UPROPERTY metadata, enforced by the details panel only**. `SetOverscan()` at `:138`
  assigns with no clamp.
- `FMinimalViewInfo::ApplyOverscan` (`Private\Camera\CameraStackTypes.cpp:517-546`) composes
  multiplicatively at `:526` and applies as `atan(scalar · tan(halfFOV))` at `:533-535`. **No upper
  clamp.** The only hard clamp is `OverscanResolutionFraction` → `[1,2]` when
  `bScaleResolutionWithOverscan` (`:539-542`).

DynamicLens clamps `Applied` to `[1,2]` in `ApplyToCamera` and `Cam->Overscan` to `[0,1]` in
`ApplyRendering`. **Both are self-imposed.** Going past 2 does not rescue fisheye — 85° on an 8 mm
needs O ≈ 7.3 and 90° needs infinity — but it may buy a few degrees of rim, and the limit should be
a deliberate choice rather than an accident. See the roadmap entry.

## `USceneCaptureComponentCube` — the only live-viewport path

The decisive finding, and it contradicts the community survey, which concluded that fisheye in UE is
always offline.

**It is one renderer, not six.** `r.SceneCapture.CubeSinglePass` defaults true
(`Renderer\Private\SceneCaptureRendering.cpp:70-75`). Six faces become six `FViewInfo`s in one
`FSceneViewFamily` and one `FSceneRenderer` (`:1875-2187`), rendered into a single 3×2 atlas of
`3N × 2N` and blitted to the cube slices. `FSceneRenderer::RenderCubemapFaces` and `bCaptureAllFaces`
do not exist in 5.8.

**Same-frame availability is confirmed**, which is what makes live preview possible at all.
`FRendererModule::BeginRenderingViewFamilies` (`Renderer\Private\SceneRendering.cpp:5359`) updates
deferred scene captures at `:5487-5496` *before* constructing the main view's renderers; renderers
execute in insertion order and the capture leaves the cube in `ERHIAccess::SRVMask` (`:2167`). So the
main view's post-process samples **this frame's** cube. `BeginRenderingViewFamily` is called from both
`Editor\UnrealEd\Private\EditorViewportClient.cpp:4979` and
`Runtime\Engine\Private\GameViewportClient.cpp:2071`, and `USceneCaptureComponentCube` sets
`bTickInEditor = true` (`SceneCaptureComponent.cpp:1307`) with `bCaptureEveryFrame` on by default.
**Level viewport, PIE and Movie Render Graph, one mechanism.**

A post-process material can sample a `TextureRenderTargetCube` (`MCT_TextureCube`,
`Engine\Private\TextureRenderTargetCube.cpp:154-157`). Positional nodes return 0 in an `MD_PostProcess`
material (`MaterialExpressions.cpp:12355-12358`), so the ray has to be rebuilt from `ViewportUV` and
the inverse view-projection.

### What it costs and what it breaks

Face FOV is hard-coded 90° (`SceneCaptureRendering.cpp:1759-1760`) and **all six faces always
render** — there is no front-hemisphere mode, so a 180° fisheye wastes roughly 1/6. A 2048 cube
allocates a 6144×4096 GBuffer, about 2.9× a 4K frame. The single pass saves scene update, visibility
and shadow setup; it does **not** save base pass, lighting, translucency, volumetric fog (6 froxel
volumes) or post.

| Feature | State in a cube capture | Evidence |
|---|---|---|
| Lumen GI + reflections | **off by default**, re-enablable via the component's PP settings | `SceneCaptureRendering.cpp:797-816`; ctor `SceneCaptureComponent.cpp:1317-1322` |
| Lumen **screen traces** | force-off — Epic's comment: they "don't blend well across face boundaries, creating major lighting seams" | `SceneCaptureRendering.cpp:811-814` |
| Lumen cost | a second, capture-private surface cache at `LumenSurfaceCacheResolution` (default 0.5) | `SceneCaptureRendering.cpp:2035-2047` |
| Nanite | **works unchanged**, only visibility filtering differs | `NaniteCullRaster.cpp:4110,7534` |
| TSR/TAA | runs, but **six independent histories**, one per face tile — expect tile-edge reprojection artefacts | `SceneCaptureRendering.cpp:755`; `SceneView.cpp:1162-1205` |
| SSR | **off** in every capture regardless of show flag | `ScreenSpaceRayTracing.cpp:165-193` |
| SSAO | works | `IndirectLightRendering.cpp:615-627` |
| DFAO history | **explicitly disabled for cube** | `DistanceFieldLightingPost.cpp:298-306` |
| Motion blur | **off** — "doesn't work correctly with scene captures" | `SceneCaptureComponent.cpp:186` |
| Vignette | forced to 0 for cube | `SceneCaptureRendering.cpp:809` |
| Auto exposure | **consistent across faces by design** — shared view state, histogram over the union of all six rects | `SceneView.cpp:1087-1091`; `PostProcessing.cpp:1394-1404` |
| Mips | generated per face with no cross-face taps — seams above mip 0 | `RenderCore\Private\GenerateMips.cpp:145-197` |
| Screen percentage | force-disabled | `SceneCaptureRendering.cpp:952-956` |

`bCaptureRotation` defaults false (`SceneCaptureComponentCube.h:26-28`), so faces stay world-axis
aligned and TSR histories stay reprojection-stable under a camera pan. Keep it that way.

### The source-format decision

The post chain runs only for the three "Final" capture sources (`SceneCaptureRendering.cpp:230-233`,
`:748`, `:1990`):

- **`SCS_SceneColorHDR`** — lit, un-tonemapped. The main view's post chain then applies bloom,
  exposure and tonemap **once** to the finished fisheye, which is correct. But `PostProcessing=0`
  forces `AAM_None` (`SceneView.cpp:1174-1179`), so the source is **fully aliased with no TSR**.
- **`SCS_FinalColorHDR`** — gets TSR and shared exposure, but runs the whole post chain six times and
  bakes tonemapping per face.

This is the real design decision, not a detail.

### Not available
`bRenderInMainRenderer` / `UserSceneTexture` (`SceneCaptureComponent2D.h:143,198-208`) is **2D-only**,
and useless anyway: `FCustomRenderPassBase::ERenderMode` is `{DepthPass, DepthAndBasePass}`
(`Public\Rendering\CustomRenderPass.h:57-63`) — unlit base pass, no lighting, no Lumen, no post.

## Movie Render Graph panoramic — ships, but unusable as-is

`MovieGraphDeferredPanoramicPassNode.h` / `Private\Graph\Renderers\MovieGraphDeferredPanoramicPass.cpp`.
This answers the open question from the MRG overscan work: panoramic **did** land in Graph, not only
Queue.

| Fact | Evidence |
|---|---|
| Pane FOV **hard-coded 90°×90°**, no override — "as we don't support stereo or allowing users to override the pane FOV" | `MovieGraphDeferredPanoramicPass.cpp:240-245` |
| Min 8×3 = **24 scene renders per output frame**, times spatial and temporal samples | `.h:98-102` |
| Pane resolution = `OutputX/4` square | `.cpp:247-261` |
| +6 more renders when auto-exposure is on | `.cpp:293-300` |
| Node is `final` — **cannot be subclassed** | `MovieGraphDeferredPanoramicPassNode.h:18` |
| Path tracer blacklisted in the UI | `.h:177-178` |
| Force-disables **Vignette, SceneColorFringe, PhysicalMaterialMasks, Depth of Field** | `.cpp:122-131` |
| Per-pane camera PP settings **are** applied, including blendables | `MovieGraphImagePassBase.cpp:148-156, 218-221` |

**That last row is the trap.** A DynamicLens blendable on the camera is applied *independently to
each of the 24 panes* before blending. The result is garbage. Any fisheye node must strip the
plugin's own post-process material from the panes.

Output is a CPU-side `TArray64<FLinearColor> OutputEquirectangularMap`
(`MoviePipelinePanoramicBlenderBase.h:154-155`) handed to the output merger and written to disk.
**No GPU texture, no render target**, and there is no image-remap node anywhere in
`Graph\Nodes\`, so equirect→fisheye cannot happen inside MRG without new C++.

**The good news:** the blender is a per-output-pixel *gather*, not a scatter
(`MoviePipelinePanoramicBlenderBase.cpp:364-370` derives `Theta/Phi` per output pixel; `:242-243`
projects through the pane matrix; `:373-380` weights by `dot(OutputDirection, SampleDirection)`).
Only the `(X,Y) → (Theta,Phi)` step and the bounds computation at `:246-267` are equirect-specific,
so **retargeting it to an equidistant/equisolid fisheye output is roughly a 50-line change**, with
filtering, weighting and projection maths free. `UMovieGraphImagePassBaseNode` is
`UCLASS(MinimalAPI, Abstract)` with `UE_API` methods and the blender base is
`MOVIERENDERPIPELINECORE_API`, so a **parallel node living in DynamicLens is viable** — it has to be
parallel, because the stock node is `final`.

## Path tracer — no

One camera model, taken from the 4×4. `CreatePrimaryRay(UV)`
(`Engine\Shaders\Private\RayTracing\RayTracingCommon.ush:510-526`) is an inverse transform of
`View.ClipToTranslatedWorld`, called from `PathTracingCore.ush:1521`. There is no
`PathTracingCamera.ush`, no `PATH_TRACER_CAMERA_*` permutation, and no `r.PathTracing.Camera` cvar.
`ApplyPetzval` warps the **lens aperture sample**, not the ray direction — a bokeh model, not a
projection.

This would be the *correct* answer — analytic fisheye rays, no stitching, no seams — and it is
unavailable without editing `PathTracingCore.ush` and `RayTracingCommon.ush` in a **source-built
engine**. Not a plugin-shaped problem.

## What else exists, and why it loses

- **nDisplay** — hard-capped at 89° per half-angle
  (`DisplayClusterViewport_Math.cpp:247-249`), one full scene render per viewport
  (`DisplayClusterRenderFrameManager.cpp:82-125`), all curvature a post-pass warp. Per-frame drive is
  C++ only (no `UFUNCTION`s). The only in-engine texture is the preview RT, throttled to one viewport
  per frame at ≤2048 with post and TSR off. Proven in venues at 60 fps, 5.4K², five frame-locked
  machines — but it is a *display* path, not a deliverable. Strictly dominated by SceneCaptureCube.
- **PanoramicCapture** (`Engine\Plugins\Experimental\PanoramicCapture`, still the old
  `StereoPanorama*` sources) — SceneCapture2D slit-scan, 360×3 = 1080 steps per eye
  (`SceneCapturer.cpp:122-123`), stalls the game for many frames, PNG/EXR to disk. Stills only.
- **Panini** — now `r.LensDistortion.Panini.*` (`Renderer\Private\PostProcess\LensDistortion.cpp:20-49`).
  A UV-displacement LUT over a rectilinear render; Epic's own cvar help says it auto-disables past
  ~140° and `:154` notes it is "undefined at/near a 180 degree FOV".
- **`IStereoRendering`** with `GetDesiredNumberOfViews` up to 32 views
  (`StereoRendering.h:66,156,162`; `UnrealEngine.cpp:4304`) — arbitrary per-view 4×4s in one family,
  which is how nDisplay works. **Game-viewport only**: called solely from
  `GameViewportClient.cpp:1777`, and `EditorViewportClient.cpp:1744` gates stereo on an XR system.
- **Does not exist in 5.8:** `r.Fisheye`, `r.ODS`, `r.PanoramicRendering`, `r.StereoPanorama`. The
  only "Fisheye" in the engine is OpenCV's *calibration* model.

## What the community ships

Ranked, from the survey. Useful mostly as proof the cube approach works and as a source of seam
technique.

- **Off World Live** (commercial, most mature) — "a cubemap of cameras", Domemaster/fisheye output,
  documented in use at **210°** (SAT Montreal). Live render-target preview at 4K/20–30 fps with DLSS,
  plus an MRQ pass. The only product that has genuinely solved seams.
- **zero-noise-lab/DomeProjection** (free, GitHub, UE 5.3+) — five-camera rig → fisheye → Paul
  Bourke distortion mesh. **Up to 240°**, real-time in PIE, with noticeable input lag. Hobby project,
  but readable and the right maths to lift.
- **Camera Render Studio / Camera 360 v3** (commercial) — claims single-camera shader warp, fulldome
  among its projections. **No motion vectors, so no motion blur and no TSR history**; users report
  seam artefacts. Vendor claims, not independently verified.
- **ImmVis Fisheye Render Setup** (free) — eight MRQ sequences stitched, 165°/180°. The most honest
  public write-up of the seam problem.

### Seam technique worth stealing

1. **Overscan each face 2–5% and cross-blend the overlap.** Cheap, and the one that actually works.
2. **Rotate the cube so the seam lands away from the subject.**
3. **Kill per-view effects and redo them globally** — auto-exposure locked, bloom on the stitched
   frame rather than per face.
4. **Hardware ray tracing for Lumen.** Screen traces literally cannot see into the neighbouring face,
   so reflections disagree across a seam. This is structural, not a tuning problem.
5. **TSR survives a stitch but wants less history** — Off World Live runs 100% where UE defaults to
   200%.
6. **Motion blur and DOF have no good answer anywhere.** Both are screen-space and per-view. Every
   source either disables them or moves them to comp from depth and motion-vector AOVs.

## Ranking, for "a Sequencer cinema camera that renders a true 180° fisheye, viewport preview and MRG final"

1. **`USceneCaptureComponentCube` + a fisheye remap post-process material on the main camera.** The
   only mechanism that satisfies viewport, PIE *and* MRG. Costs one extra `FSceneRenderer` of six
   views; loses SSR, DFAO history and motion blur; Lumen needs care.
2. **A custom Movie Graph node reusing `FMoviePipelinePanoramicBlenderBase` with a fisheye output
   map.** Offline only, but the highest quality path and a modest amount of work. 24+ renders/frame.
   Must suppress the DynamicLens blendable on the panes.
3. **Stock MRG panoramic → equirect → remap downstream** in Resolve/Nuke/`ffmpeg -vf v360`. Zero
   engine work, exact resampling, but no DOF, no vignette, no in-engine CA, a disk round-trip and no
   preview. If this route is taken, bake the *measured* Nikkor/OpTex mapping into the remap rather
   than using a generic fisheye — the profiles already exist, and the remap is an ST map at a later
   stage of the pipeline.
4. nDisplay, PanoramicCapture, path tracer — all lose for the reasons above.

**Not viable at all:** custom projection matrices (rectilinear by construction), Panini (fails past
~140°), Custom Render Pass / `UserSceneTexture` (unlit base pass only), raising overscan alone
(90° needs O → ∞).
