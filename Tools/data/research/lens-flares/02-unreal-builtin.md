# Lens flares 02: what Unreal 5.8 itself offers

Research for the roadmap item "Simulated lens flares matched to the lens" (`.claude/refs/roadmap.md`).
The question is how per-lens flares could be rendered in UE 5.8, so this covers what the engine already
does and where a plugin can hook in.

Written 2026-09-25 against the UE 5.8 installed build, with 5.6 and 5.7 installs diffed for changes.
Engine paths are engine-relative. Line numbers are UE 5.8.

**Evidence tags**

- **[src]**: read in the engine source.
- **[src-inferred]**: follows from the source, but not run or measured.
- **[docs]**: from Epic's documentation.
- **[community]**: third-party writeups or plugins.
- **[unverified]**: plausible, not checked.

---

## 1. The built-in "Image Based Lens Flare"

### What it actually does [src]

Files:

- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessLensFlares.cpp`
- `Engine/Shaders/Private/PostProcessLensFlares.usf`
- The call site is `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp:1567-1574`.

The pass runs in two stages:

1. **Blur, or "bokeh scatter"** (`PostProcessLensFlares.cpp:194-276`, `LensFlareBlurVS/PS` in the usf).
   - It takes one level of a **downsampled pre-tonemap scene-colour chain** and emits one instanced
     quad per texel (`DrawPrimitive(... TileCount.X*TileCount.Y / 4)`, `.cpp:268`).
   - Each quad is `KernelSize = LensFlareBokehSize% × view width` pixels across, textured with the
     **Bokeh Shape** texture and additively blended. It is a sprite-scatter lens blur.
   - Texels below threshold collapse to a zero-size quad (`.usf:77-83`). The threshold test is
     `dot(rgb, 1) < Threshold`, a sum of the three channels rather than luminance.
   - The blur target is drawn at half scale, a "guard band" (`GuardBandScale = 2`, `.cpp:170`), so
     the bokeh does not clip. Each texel is also multiplied by `DiscMask(ScreenPos)`, which fades
     sources toward the frame edge (`.usf:75`).
2. **Ghosts** (`.cpp:314-382`).
   - The blurred texture is redrawn **8 times** (`LensFlareCountMax = 8`,
     `PostProcessLensFlares.h:20`) as a screen quad centred on the **view centre**, additively.
   - The ghost's scale comes from the tint's alpha:
     `scale = (A × 7 − 3.5) × 2` (`.cpp:319-340`). A negative scale gives a point reflection through
     the centre, which is the classic "ghost opposite the sun".
   - The defaults run A = 0.6 … 0.15 (`Engine/Source/Runtime/Engine/Private/Scene.cpp:637-644`),
     which gives scales from +1.4 to −4.9.
   - The ghost colour is `LensFlareTint × LensFlareIntensity × BloomIntensity / 4 × Tints[i].rgb`.
   - Each ghost is masked by `DiscMask(p) × DiscMask(0.8p)`, a round vignette (`.usf:27-32`).
   - The result is **added into the bloom texture** (after copying the bloom into it,
     `.cpp:294-307`). The tonemapper then adds bloom to scene colour before the tone curve, and the
     dirt mask multiplies it (`Engine/Shaders/Private/PostProcessTonemap.usf:379-396`). So flares
     are HDR, pre-tonemap, and they pick up the Bloom Dirt Mask.

Two more details of the pipeline:

- **The source image depends on the bloom method** (`PostProcessing.cpp:1478-1562`).
  - With **Convolution** bloom, the flare reads the plain scene downsample chain.
  - With **Standard (Gaussian)** bloom and `BloomThreshold > -1`, it reads the *bloom-thresholded*
    chain, so the bloom threshold also gates the flare.
  - The chain starts at half resolution (`PostProcessing.cpp:1418-1426`). The stage used is
    `3 − r.LensFlareQuality` (`.cpp:1570`): quality 3 is half res, 2 is quarter res and 1 is eighth
    res [src-inferred from the index maths].
- **Enable conditions** (`PostProcessLensFlares.cpp:387-397` and `PostProcessing.cpp:862-867`).
  Bloom must be on: `BloomIntensity > 0` and the active method's intensity must be above 0.
  `r.LensFlareQuality` must be above 0. The tint must not be black, and BokehSize and Intensity must
  be above 0.

### Parameters (`FPostProcessSettings`, `Engine/Source/Runtime/Engine/Classes/Engine/Scene.h:2117-2139`) [src]

| Property | Default (`Scene.cpp:537-540`) | Notes |
|---|---|---|
| `LensFlareIntensity` | 1.0 | also multiplied by `BloomIntensity` |
| `LensFlareTint` | white | global tint |
| `LensFlareBokehSize` | 3.0 | percent of **view width**. "Performance cost is radius²" |
| `LensFlareThreshold` | 8.0 | pre-exposed HDR, compared against the sum of R+G+B |
| `LensFlareBokehShape` | `/Engine/EngineMaterials/DefaultBokeh` | any `UTexture`. **Cannot be blended**, it is assigned (`SceneView.cpp:1964-1966`) |
| `LensFlareTints[8]` | see `Scene.cpp:637-644` | RGB is colour, **A is ghost position/scale**. Header comment: "This is a temporary solution". `EditAnywhere` only, **not** `BlueprintReadWrite` (`Scene.h:2138`), so drive it from C++ |

Engine-level gates [src]:

- `r.DefaultFeature.LensFlare` defaults to **0**, and 0 zeroes the intensity unless a post-process
  volume or camera overrides it (`Engine/Source/Runtime/Engine/Private/SceneView.cpp:227-232`,
  `2096-2099`). The project setting is "Lens Flares (Image based)" (`RendererSettings.h:851-854`).
- The show flag `LensFlares` (`SceneView.cpp:2255-2258`) and the show flag `Bloom` (`2199-2202`,
  which zeroes bloom and so the flare) both switch it off.
- **Scalability** (`Engine/Config/BaseScalability.ini`): `sg.PostProcessQuality` 0 and 1 set
  `r.LensFlareQuality=0`, which is off. Levels 2 and 3 give quality 2 (quarter res), and Cine gives
  quality 3 (half res). This is the same class of trap as the bokeh cvars in
  `.claude/refs/architecture.md`: below High, the flare silently vanishes.

### Limitations

- **Screen space only** [src]. It can only flare what is inside the view rect and above threshold.
  Sources off-screen or occluded produce nothing, and a source partly hidden behind an object is
  flared by exactly its visible pixels. That part is correct.
- **Not a lens model** [src]:
  - The ghosts are 8 uniformly scaled copies of the **whole thresholded image** about the view
    centre.
  - There is no per-ghost aperture shape; one bokeh texture is shared by all ghosts.
  - There is no per-ghost blur size, no chromatic separation within a ghost, and no dependence on
    f-stop, focal length or focus.
  - There are no streaks or starbursts, and no veiling glare.
  - The ghost axis is always the view centre, which is right for a centred lens.
- **Sizes are relative to the view** [src-inferred]. BokehSize is a percentage of the view width, and
  the ghost scale is relative to the view rect. Under camera overscan the "view" is the overscanned
  image, so on DynamicLens's post-process path the bokeh grows with `1+Overscan`. The ghost *mapping*
  is overscan-invariant, because it is a linear scale about the centre. The bokeh would step in size
  whenever dynamic overscan steps.
- Everything is capped at 8 ghosts, which is hard-coded.
- **Changes 5.6 → 5.8** [src, diffed]:
  - 5.7 removed the optional compute-shader blur path (`r.LensFlareBlurComputeShader`).
  - 5.8 changed only includes and a comment.
  - The algorithm is otherwise unchanged since UE4, so the UE 4.27 docs still describe it
    ([Lens Flare, 4.27](https://dev.epicgames.com/documentation/en-us/unreal-engine/lens-flare?application_version=4.27)).

---

## 2. Convolution (FFT) bloom

Files:

- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessFFTBloom.cpp`
- `Engine/Shaders/Private/Bloom/*.usf`
- Docs: [Bloom in Unreal Engine (5.8)](https://dev.epicgames.com/documentation/en-us/unreal-engine/bloom-in-unreal-engine).

### How it works [src]

- It is used when `BloomMethod == BM_FFT` ("Convolution", `Scene.h:54-61`), the view **has a
  ViewState**, and the platform supports it (`PostProcessFFTBloom.cpp:234-248`).
- The kernel is `BloomConvolutionTexture`, a **`UTexture2D`** (not `UTexture`, so not a render
  target; `Scene.h:1723-1724`). It falls back to `/Engine/EngineMaterials/DefaultBloomKernel`.
- If the texture is not fully streamed in, it logs "texture is not streamed in" and **silently falls
  back to Gaussian bloom** for that frame (`Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp:2725-2755`,
  `2975-3001`).
- The kernel is resized to `BloomConvolutionSize × the major axis of the image`, then FFT'd.
  - The resulting spectrum is **cached on the ViewState** (`.cpp:338-360`, `695-709`, cvar
    `r.Bloom.CacheKernel`).
  - The cache is keyed on the texture's **RHI pointer**, the size, the image size and the mip count.
    **Rewriting the pixels of the same texture in place does not invalidate it** [src-inferred].
    A live-changing kernel needs a new texture object, a changed Size, or
    `r.Bloom.CacheKernel 0`.
  - `BloomConvolutionBufferScale` is deliberately not blended because it would thrash the cache
    (`SceneView.cpp:1933-1937`).
- The scene is FFT'd at `r.Bloom.ScreenPercentage` (25 / 35 / 50 / 50 / **100 at Cine**). It is
  padded to a power of two, multiplied by the kernel spectrum, and inverse FFT'd, on async compute
  by default (`r.Bloom.AsyncCompute`).
- **Energy conserving since 5.3-ish** [src; version unverified]:
  - The kernel's centre energy and its scatter energy are surveyed on the GPU.
  - The tonemapper scales scene colour by the centre fraction (`SceneColorApplyParamaters`), and
    the FFT result carries the scatter fraction (`Engine/Shaders/Private/Bloom/BloomFinalizeApplyConstants.usf`).
  - `BloomConvolutionScatterDispersion` rescales the scatter energy.
  - **5.7 fix**: `BloomConvolutionIntensity` now actually multiplies the scatter
    (`PostProcessFFTBloom.cpp:874-877`, absent in 5.6).
- **The kernel is RGB.** A chromatic PSF, for example a coloured diffraction starburst, is supported
  natively.
- **Boost** (`PreFilterMin/Max/Mult`, defaults 7 / 15000 / 15) amplifies hot pixels before the
  convolution.

### Feeding a measured or simulated per-lens PSF

- **What fits.** The kernel is **one shift-invariant PSF for the whole frame**: every pixel gets the
  same kernel. That is exactly right for aperture diffraction:
  - starburst or spikes, which depend on blade count and f-stop;
  - the veiling-glare skirt;
  - the halo.
- **What does not fit.** It **cannot produce ghosts**, because a ghost's position depends on where
  the source sits relative to the optical axis, which is a shift-*variant* effect. The same goes for
  field-dependent coma or flare shape.
- **Kernel texture setup** [docs]: a centred hot point much brighter than the rest (EXR, HDR
  compression), **No Mipmaps**, **Never Stream**. Otherwise a low-res mip is used "and will result in
  serious degradation".
- **Per camera** [src]. The setting lives in `FPostProcessSettings`, which is on every
  `UCineCameraComponent`, and the cache is per ViewState, which is per view. So each camera can carry
  its own lens's kernel.
  - Switching kernels costs one kernel FFT on the frame it changes.
  - A kernel that varies with f-stop needs a small bank of pre-baked textures (say one per stop, or
    per blade-count / stop pair) that are swapped, not blended.
- **Cost** [docs + src-inferred]. Epic positions it as for "in-game or offline cinematics or high-end
  hardware".
  - Per frame, the cost is a 2D FFT of the padded image at `r.Bloom.ScreenPercentage` (full res at
    Cine) plus one spectral multiply and an inverse FFT. The kernel FFT is cached.
  - Not measured here; **measure before relying on it** at 4K Cine.

---

## 3. Can a plugin replace or hook the flare and bloom?

### Replacing `AddLensFlaresPass` itself: no, not without an engine change [src]

It is a free function in the private Renderer module with no delegate. Froyok's well-known custom
flare needed an engine patch that adds a delegate inside `PostProcessLensFlares.cpp`
([Froyok, 2021](https://www.froyok.fr/blog/2021-09-ue4-custom-lens-flare/)). A plugin can only
**turn the built-in one off** (`LensFlareIntensity = 0` on the camera) and draw its own.

### SceneViewExtension hook points in 5.8 [src]

The enum is `ISceneViewExtension::EPostProcessingPass`, at
`Engine/Source/Runtime/Engine/Public/SceneViewExtension.h:122-138`:
`BeforeDOF, AfterDOF, TranslucencyAfterDOF, SSRInput, ReplacingTonemapper, MotionBlur, Tonemap, FXAA, SMAA, VisualizeDepthOfField`.
SMAA was added in 5.7. The 5.8 changes are a `TDelegate` typedef and
`ESceneViewExtensionFlags::RequiresHardwareInlineRayTracing`; nothing flare-relevant.

| Hook | Colour it sees | Resolution | Use for flares |
|---|---|---|---|
| `PrePostProcessPass_RenderThread` | scene textures, linear HDR, pre-TSR, jittered | render res | could raster flares into SceneColor before TSR; TSR then stabilises them |
| `BeforeDOF` / `AfterDOF` / `TranslucencyAfterDOF` | linear HDR | render res | before or after DOF, pre-TSR |
| `SSRInput` | TSR output | display res | wrong purpose, because it feeds next frame's SSR |
| **`MotionBlur`** (after motion blur = `BL_SceneColorBeforeBloom`) | **linear HDR, post-TSR, pre-bloom** | display res | **the best point.** A new colour returned here invalidates the half/quarter chains (`PostProcessing.cpp:1290-1305`), so **bloom, FFT bloom and the built-in flare all run on top of it**. This is the hook the Prism flare editor uses ([Prism](https://t-boons.github.io/PrismBlog/)) |
| `ReplacingTonemapper` | scene colour **plus `CombinedBloom`** (`PostProcessing.cpp:1582-1593`) | display res | the only hook that sees the bloom/flare texture, but it *replaces* Epic's tonemapper, so it is not viable |
| `Tonemap` (after) | post-tonemap, display-referred | display res | too late for HDR flares. Note that CameraCalibrationCore's `PassAfterTonemap` distortion mode hooks here (`LensDistortionSceneViewExtension.cpp:694-712`) |

No hook exposes the bloom texture or the scene downsample chain *before* the tonemapper, short of
replacing it. A custom flare therefore builds its own downsample and threshold from the `MotionBlur`
after-pass colour. That is cheap: one half-res downsample plus a few mips.

### Post-process materials [src + docs]

`BL_SceneColorBeforeBloom` is the material equivalent of the `MotionBlur` hook: linear HDR at display
resolution (`Engine/Source/Runtime/Engine/Classes/Engine/BlendableInterface.h:51-57`). A material
there can gather-sample `PostProcessInput0` at ghost-mapped UVs (`c − sᵢ·(uv − c)`) with a
threshold. That gives N ghosts at N taps per pixel, drivable per camera through a MID, the way
DynamicLens already drives `M_DL_ImageCircle`.

The limits:

- There are no mips or half-res inputs, so a blurred, aperture-shaped ghost needs many taps.
- `CombinedBloom` is only available to `BL_ReplacingTonemapper`
  (`Engine/Source/Runtime/Renderer/Public/PostProcess/PostProcessMaterialInputs.h:29-30`).

It is good for a prototype, and weak for many soft ghosts.

### Existing third-party SVE flares [community]

- `CustomLensFlaresUE` (singinwhale/shortnamesalex) and `UE5-CompositeLensFlare-Plugin`
  (RoastedKaju) port Froyok's ghosts, halo and glare to a global SceneViewExtension, so they work on
  binary engines.
- Prism uses the `MotionBlur` pass with instanced bokeh quads and a game-thread line trace for
  occlusion.

These show the SVE route works on a stock 5.x engine. None of them is lens-physical.

---

## 4. Sprite / Niagara flares at light sources

- The engine ships no flare actor [src]. `find` over `Engine/Source`, `Plugins` and `Shaders` turns
  up only the post pass and some sample Niagara content.
- The common pattern [community]:
  1. Project the light's world position, for example the sun direction from the DirectionalLight, to
     the screen.
  2. Place additive screen-facing sprites (a Niagara system or a material on a camera-attached
     plane) along the line from that point through the screen centre.
  3. Fade by an **occlusion factor**.
- **Niagara has this built in**: the Occlusion data interface, with `QueryOcclusionFactorWithCircle`
  and `QueryOcclusionFactorWithRectangle` (`Engine/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraDataInterfaceOcclusion.cpp:22-25`),
  samples scene depth over a disc or rectangle on the GPU
  ([CGHOW occlusion query](https://cghow.com/occlusion-query-in-ue5-niagara-tutorial/),
  [CGHOW lens flare](https://cghow.com/lens-flare-in-ue5-3-niagara-tutorial/)).
  - Line traces are the CPU alternative, used by Prism.
  - Distance-field traces are another option.
- **What this gains**: sources can be **off-screen**, for example a sun just outside frame, because
  the analytic position still exists. It also gives explicit per-ghost control: shape, size,
  position along the axis, colour, and dependence on f-stop and focal length. That is exactly what a
  per-lens flare simulation outputs.
- **What it loses**: it only flares *registered* sources, not arbitrary bright pixels like
  speculars, emissive signs or explosions. Scene-depth occlusion has no information about what lies
  behind an off-screen source.
- A hybrid is common: sprites for the key light, screen-space for everything else.

---

## 5. Movie Render Queue / Graph

- **High-resolution tiling breaks screen-space flares and bloom.**
  - [src] MRQ's own validation says "Tiling does not support all rendering features (bloom, some
    screen-space effects)" (`Engine/Plugins/MovieScene/MovieRenderPipeline/Source/MovieRenderPipelineCore/Public/MoviePipelineHighResSetting.h:50`).
  - [src-inferred] Each tile is its own view, so the built-in ghosts mirror about **each tile's**
    centre. BokehSize is a percentage of the tile width, and FFT bloom convolves each tile
    separately, so its energy cannot cross tiles and the kernel is scaled to the tile.
  - MRG has tiling too (`MovieGraphDeferredPassNode.h:219-266`, "High Resolution Tiling"), with the
    same caveat. Any custom flare that is centred on the view has the same problem unless it knows
    the tile's offset in the full frame.
- **Spatial and temporal samples are fine** [src-inferred]. Each sample runs the full post chain, and
  the result is accumulated. Threshold popping on a moving source averages out.
- **`bDisableMultisampleEffects`** (MRQ deferred pass) sets the Bloom show flag off
  (`MoviePipelineDeferredPasses.cpp:206-216`), which **kills the flare too**.
- **TSR**: every hook from `MotionBlur` onward is post-TSR, so flares are not temporally smeared.
  `PrePostProcessPass` flares would go through TSR. That gives stability, but fast-moving ghosts could
  ghost temporally [unverified].
- **Overscan and buffer UVs**: under MRG, camera overscan makes the buffer larger than the view. Any
  custom flare shader that samples the scene must use buffer UVs. This is the same neon-speckle trap
  documented in `.claude/refs/architecture.md`.
- **Convolution bloom needs a ViewState** [src], which MRQ/MRG views have. The kernel cache is per
  view state, so it is per camera and per tile.
- Forum threads report flares and bloom missing from MRQ renders; the causes above cover the usual
  reasons ([forum](https://forums.unrealengine.com/t/how-to-render-bloom-lens-flare-in-movie-render/262698)) [community].

---

## 6. New in 5.6–5.8 that matters here [src, diffed 5.6 / 5.7 / 5.8 installs]

- Lens flare: 5.7 dropped the compute-shader blur variant. Otherwise the algorithm is unchanged.
- FFT bloom: 5.7 made `BloomConvolutionIntensity` scale the scatter correctly. 5.8 changed only
  logging.
- SceneViewExtension: 5.7 added the `SMAA` after-pass. 5.8 changed only the delegate typedef and a
  flags enum.
- There is **no new flare or physical-lens feature in 5.6–5.8** in the source. Epic's 5.8 release
  notes were searched and turned up no lens-flare entry [docs; not exhaustive].

---

## Implications for DynamicLens

1. **Epic's built-in flare cannot be made per-lens, beyond cosmetic tuning.** DynamicLens *could*
   drive `LensFlareTints[8]`, `BokehShape`, `BokehSize` and `Threshold` per preset through
   `Cam->PostProcessSettings`, which is the same mechanism as the vignette. But the model is 8
   scaled copies of the whole frame about the centre, so it cannot represent a real lens's ghost set.
   - It needs `r.DefaultFeature.LensFlare` or a camera override, and Bloom on.
   - It vanishes below High scalability. It would need a guard like `bForceBokehQuality`.
   - Its bokeh size would scale with dynamic overscan on the post-process path.

   It is fine as a free "cheap preset look". It is not the physical answer.
2. **Convolution bloom is the right home for the shift-invariant half of a flare**: the aperture
   starburst, the veiling-glare skirt and chromatic diffraction.
   - Ship one EXR kernel per lens, or per lens × f-stop bucket, set No Mipmaps and Never Stream.
   - Drive it per camera by swapping `BloomConvolutionTexture`, never by editing a texture in place,
     because of the RHI-pointer cache.
   - It is energy conserving, pre-tonemap, per camera, and needs no C++ render code.
   - Cost at Cine / 4K is unmeasured and has to be measured.
   - It silently falls back to Gaussian bloom if the kernel is not resident.
3. **Ghosts, the shift-variant half, need our own pass.** The clean stock-engine route is an
   **`FSceneViewExtensionBase` subscribing to `EPostProcessingPass::MotionBlur`**:
   - That hook sees linear HDR at display resolution before bloom, and it works on binary engines.
   - Draw it by thresholding a half-res downsample, then drawing per-ghost quads or gathers. Each
     ghost gets the lens's own scale, offset, aperture shape, colour and blur from the flare
     simulation, all as functions of f-stop and focal length.
   - Bloom then runs over the result.

   This is a new C++ module piece, so it follows the build → ask → restart cycle. A
   `BL_SceneColorBeforeBloom` material prototype of 2–4 ghosts could validate the look first with
   no build.
4. **For key lights, add the sprite path.** A sun or practical flared from its projected position,
   with Niagara's Occlusion data interface or a trace, is the only way to flare **off-frame**
   sources. DynamicLens's overscan margin already gives the screen-space passes a little off-frame
   reach on the post-process-material path.
5. **Order relative to distortion.**
   - In Post Process Material mode, distortion is applied *after* tonemapping, so flares computed at
     `MotionBlur` get distorted and cropped by the image circle along with the image. Their axis is
     the optical centre, which is correct.
   - In TSR mode, distortion happens inside TSR, so flares are computed on the already-distorted
     image. The two modes will differ slightly; decide which one is canonical.
   - Any flare HLSL must use `ViewportUVToSceneTextureUV` for MRG.
6. **MRQ/MRG**: document that high-res tiling is unsupported for flares and bloom. Warn if
   `bDisableMultisampleEffects` or a tiled pass is active. Anything view-centred must use the full
   frame's centre, not the tile's.
