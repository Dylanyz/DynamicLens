# Lens flares 03 — what already exists

Survey of Unreal lens-flare plugins and implementations (2024-2026), plus Unity, Godot and
standalone physically based flare work for comparison. Research only, gathered 2026-09-25 by web
search. Fab listing pages return HTTP 403 to automated fetches, so Fab details below come from
search-result snippets, seller sites and videos: prices and exact version ranges on Fab are
**unverified** unless stated.

Legend for *Technique*:
- **Engine** — Unreal's stock image-based flare (threshold, mirrored bokeh copies, 8 tints)
- **SS** — screen-space / image-based: threshold the frame, mirror and scale it into ghosts, halo, glare
- **Sprite** — per-light billboards (Niagara, UMG or material) placed along the source-to-centre axis
- **RT** — ghosts ray traced through a real lens prescription (Hullin et al. 2011 and successors)
- **FFT** — starburst or bloom from a Fourier transform of the aperture or a kernel image

## Comparison

| Name | Author | Price / licence | UE | Technique | Occlusion | Off-screen source | Anamorphic | Per-lens presets | MRQ / MRG | Last seen update |
|---|---|---|---|---|---|---|---|---|---|---|
| Stock UE lens flare | Epic | engine | all | Engine (SS) | implicit, image-based | no | no | no (one bokeh texture, 8 tints) | yes, but reported pixelation/flicker in MRQ threads | unchanged in 5.x as far as found |
| Convolution Bloom | Epic | engine | 4.16+ | FFT of a kernel image | implicit | no | via a streak kernel | kernel per look | yes, cinematic-grade | stock feature |
| Froyok custom lens flare + bloom articles | Léna Piquet (Froyok) | free articles with full C++/HLSL; no explicit licence found | written on 4.25, notes for 4.26/4.27/UE5 EA | SS: 13-tap threshold, Dual Kawase blur, mirrored ghosts, halo, glare star, 3-tap chromatic shift | implicit | no | discussed, not implemented; 2024 Mastodon post shows anamorphic bloom experiments | data asset per look | not discussed | articles 2021; tweaks shown Feb 2024 |
| PrettyPostProcess | Escape Entertainment | open, **BSD-3-Clause-Clear** | UE5 (5.1 assets), 4.26+; source-built engine only | Froyok SS flare + bloom | implicit | no | no | console vars | unverified | 9 commits, low activity |
| CustomLensFlaresUE | singinwhale (fork: shortnamesalex) | open, **licence not stated** | patch for 5.7.4; tiny engine mod | Froyok SS via global SceneViewExtension | implicit | no | glare "leaves" scalable per axis | data asset | unverified | 18 commits |
| UE5-CompositeLensFlare-Plugin | RoastedKaju | open, **Apache-2.0** | UE5 binary (no engine mod) | Froyok SS via FSceneViewExtension, own layer on top of stock flare | implicit | no | no | data-asset presets | unverified | 3 stars, small |
| Image Space Lens Flares | Petyu (itch.io) | $30+; licence unverified | 5.3-5.6 | Froyok SS + compute bloom, triple halo, starburst | implicit | no | unverified | shipped presets | unverified | v1.0.3.7 |
| Cinematic Lens Flares | Fab seller, name unverified | paid, price unverified | UE5 | SS core; v1.1 moved flare logic into Niagara at low res ("100x" claim); v3 layered, v5 "unlimited layers"; glints, ghosts, glint ring, halo | implicit | no | "realistic anamorphic" ghosts | layer-based looks | seller says rasteriser, forward renderer, limited Path Tracer | V5 seen on listing |
| Optic Lens Flares | 3DBrushwork | Fab, price unverified | 5.4-5.6 | Sprite (Niagara + HDR textures); halos, ghosts, stars, spikes, rings; Kelvin tint; 5-step blur quality to "Offline" | claims "high-quality occlusion" | sprite-based, so likely yes (unverified) | anamorphic primary + reflected variants | save/load custom presets; Golden Hour, Sci-Fi etc. | Sequencer-keyable; MRQ unverified | Jul 2025, Aug 2025 update |
| LENS FLARE VFX (advanced sprite based) | Fab seller, name unverified | paid, price unverified | 4.27, 5.0-5.7 (per a mirror listing) | Sprite, HDR element library: chromatic ghosts, starbursts, rings, spectral streaks | unverified | unverified | anamorphic streaks | example flares | unverified | v1.1: up to 8 textured ghosts, FOV correction |
| Lens Flares (Fab 2e1d900a) | unverified | paid | UE5 | material, 2 world-space + 1 screen-space | smooth fade behind objects | fades at screen edge | unverified | material instances | unverified | unverified |
| Sun Flares for Unreal | unverified | paid | unverified | procedural PP material: sun disk, corona rays, diffraction, ghosts, CA halo | unverified | no (sun only) | no | 8 preset materials | PP material, so should render | unverified |
| Custom Lens Flare Tool | Alexander Dracott | paid; 3.6/5 from 10 ratings | 4.26-era | Sprite via screen-space UMG widgets | unverified | unverified | no | 4 example flares | **UMG does not render in MRQ by default** (inference) | old |
| Custom Lens Flares Material / Custom Len Flare With Niagara | Marketplace sellers | paid | UE4/5 | material / Niagara sprite | claims correct overlap handling | unverified | claims anamorphic | unverified | unverified | legacy listings |
| Retro Particles & Lens Flares | Marcis | $29.99+ | 4.26-5.5 | 15 PP flares, stylised 90s | implicit | no | no | 15 presets | PP, should render | active seller |
| Prism | T. Boons (student blog) | licence/source not found | UE5 | instanced quad "bokehs" from structured buffers, game-thread line-trace occlusion | line trace | unverified | no | editor for authoring | unverified | undated; bokeh limited to rectangles |
| Ultra Dynamic Sky sun flare | Everett Gunther (UDS) | paid (part of UDS) | UE5 | material-driven sun flare, separate from engine flare; custom parent MI | unverified | sun only | no | several Lens Flare Types + custom instance | renders with sky | documented through v9.7 |
| Post Process Toolkit | StraySpark | $49.99+ | UE5 | stackable PP passes, has an "Anamorphic" preset | implicit | no | streak preset | yes | unverified | 2025-26 blog |

### Outside Unreal, for design comparison

| Name | Author | Licence | Technique | Notes |
|---|---|---|---|---|
| Unity Lens Flare (SRP) component + data asset | Unity | Unity Companion | Sprite, data-driven | Best-documented design. Elements: Image, Circle, Polygon, Ring, nested Lens Flare Data (16 levels). Per element: axis position, auto-rotate, gradient, light-colour modulate, radial distortion, multiple copies by uniform/curve/random. Occlusion: depth sampled in an occlusion radius with a sample count, plus clouds, volumetric clouds and water, and a remap curve. **Allow Off Screen** toggle, attenuation by light shape (spot seen from behind gets none). |
| Unity HDRP Screen Space Lens Flare | Unity | Unity Companion | SS from bloom mips | Regular / reversed / warped flare copies, samples 1-3, **streaks with length, orientation, threshold, resolution** (anamorphic), spectral-LUT chromatic aberration. Works alongside the component. |
| ProFlares | ProFlares (Unity Asset Store) | paid | Sprite, batched atlas | One draw call for many flares, 120+ elements, raycast occlusion that fades over time rather than popping. |
| Moonflow Lensflare System | Reguluz | GitHub, licence unverified | Sprite atlas, multi-light | Unity, finished project. |
| SIsilicon Godot Lens Flare | SIsilicon | **MIT** | SS (bloom-like) | Godot 3.2; ghosts, halo, streaks, dirt, starburst. |
| realflare | Beat Reichenbach | **GPL-3.0** | RT, spectral, GPU (OpenGL 4.3 compute), Hullin-based plus sparse-polynomial references | Standalone GUI, CLI and a Nuke plugin; EXR out; lens data via OpticsExplorer database. Active (2026 copyright). |
| LensFlareFramework | A. Bodonyi, R. Kunkli et al. | **BSD-2-Clause** | RT: Hullin grid baseline, tile-based ghost rasterisation (C&G 2023), sparse-polynomial ray transfer (Visual Computer 2025) | DX11 / GL 4.3 compute reference code; lens prescriptions in `Assets/Lenses`. |
| PhysicallyBasedLensFlare (Bitsquid/Stingray blog) | Jean-Philippe Grenier | **no licence found** (all rights reserved by default) | RT patches + FFT starburst from an SDF aperture, spectral 350-700 nm, quarter-wave AR coating | ~12 ms for a Nikon 28-75 (352 ghosts). Blog notes the original paper carried a patent claim. |
| Capturing Light with Robots | Vincent Maurer, Filmakademie (SIGGRAPH Asia 2024 TC) | partial dataset CC BY-SA 4.0 | measured: motion-control grid of real flares per lens, interpolated or CNN-reproduced in Nuke | Closest in spirit to cineflares: measured, per lens, not simulated. |
| Video Copilot Optical Flares, Red Giant Knoll Light Factory, Boris FX Sapphire | commercial | proprietary | Sprite/element compositors | **No Unreal integration found** for any of the three. AE/Nuke/OFX only. |

## Notes

**Everything sold for Unreal is either screen-space or sprites.** Nothing on Fab, the old
Marketplace, GitHub or itch.io derives ghosts from a lens prescription. The physically based
work (Hullin 2011, Bitsquid 2017, Bodonyi 2023/2025, realflare) all lives outside the engine.

**Froyok is the common ancestor of the open-source Unreal work.** Every GitHub plugin found
(PrettyPostProcess, CustomLensFlaresUE, CompositeLensFlare) and the itch.io pack implement her
2021 articles. The split between them is only *how they hook the renderer*: engine patch
(PrettyPostProcess, and a very small one in CustomLensFlaresUE) vs a pure
`FSceneViewExtension` that works on a launcher build (CompositeLensFlare). The last is the only one
that suits an installed-engine plugin like ours.

**The two families fail in opposite ways.**
- Screen-space flares get occlusion free and render wherever post-process renders, including MRQ,
  but cannot see a source that is off frame, and every bright pixel flares (a lit window flares
  like the sun). Ghost shape is a blurred copy of the image, not the aperture.
- Sprite flares know their light, so they can do off-screen sources, per-light looks and real
  aperture shapes, but occlusion must be faked (line traces, depth taps) and Niagara/UMG sprites
  have a history of not appearing or popping in MRQ renders (forum threads 2022-2023). UMG-based
  tools are the weakest here.

**Anamorphic is handled cosmetically everywhere.** Streaks come from a horizontal kernel
(convolution bloom), a directional blur, or a stretched sprite. Nobody ties streak colour, length
or ghost ovality to a lens's squeeze or coating.

**Nobody maps flare to a named real lens.** Presets are moods ("Golden Hour", "Sci-Fi") not
"Cooke Anamorphic/i 50mm". Nothing reacts to focal length, f-stop or focus the way a real
flare does (ghosts scale with focal length; aperture blades set ghost polygon and star points).

**MRQ/MRG is the least documented axis.** No product page found states Movie Render Graph
support. Epic's own docs note High Resolution (tiled) rendering breaks screen-space effects,
lens flares included. Cinematic Lens Flares is the only one to discuss the Path Tracer.

**Patent status of Hullin et al.** The US application (US20140210844A1) is shown as abandoned
in 2016 and EP2702565 as withdrawn, per Google Patents. So the ray-traced-ghost method is not
patent-encumbered as far as can be seen; still worth a one-line check before shipping.

## Licence compatibility with Apache-2.0

| Source | Use as code | Use as idea |
|---|---|---|
| UE5-CompositeLensFlare-Plugin (Apache-2.0) | yes, keep its NOTICE/attribution | yes |
| LensFlareFramework (BSD-2-Clause) | yes, keep copyright notice | yes |
| SIsilicon Godot flare (MIT) | yes, keep notice | yes |
| PrettyPostProcess (BSD-3-Clause-Clear) | yes, but the Clear variant disclaims patent grants; keep notice | yes |
| realflare (GPL-3.0) | **no** — would force GPL on the plugin | yes, read and reimplement |
| Froyok articles, CustomLensFlaresUE, Bitsquid repo, Prism | **no** — no licence found, so all rights reserved | yes, credit in `SOURCES.md` |
| Hullin et al. 2011, Bodonyi 2023/2025 papers | n/a | yes, credit in `SOURCES.md` |
| Unity SRP flare design | no (Unity Companion licence) | yes, the data model is a good template |
| Maurer capture dataset (CC BY-SA 4.0) | ShareAlike on the data; keep it out of Apache paths, like tiedtke/Andy Davis | yes |
| Fab / Marketplace products | no | only as feature benchmarks |
| cineflares.com imagery | no, reference only; the site's licence could not be read | yes, as a look target |

## Implications for DynamicLens

**Borrow**
- Froyok's pass structure (threshold, Dual Kawase, ghosts, halo, glare) as the screen-space base.
  Reimplement from the article; do not copy the unlicensed forks. The CompositeLensFlare plugin
  (Apache-2.0) shows the `FSceneViewExtension` route works on a launcher engine with no patch, which
  matters because DynamicLens is installed into a stock engine and today renders only through
  post-process blendables.
- Unity's data model for the per-lens asset: element list, axis position, distortion, occlusion
  radius + sample count + remap curve, **Allow Off Screen**, attenuation by light shape. Map it to
  a `flare` block per preset in `presets.json`.
- Unity HDRP's streak parameters (length, orientation, threshold, resolution) as the anamorphic
  controls, with orientation and colour coming from the preset rather than the artist.
- ProFlares' time-smoothed occlusion so a flare fades rather than pops behind a passing object.
- From the RT literature (Hullin, Bodonyi, Bitsquid): compute ghosts **offline** per lens from a
  prescription, bake each ghost's position scale, size, shape and colour as a function of source
  angle, focal length and f-stop, and play the table back cheaply at runtime. That is the same
  "fit once, evaluate live" approach DynamicLens already takes with distortion.
- FFT of the aperture polygon for the starburst, so blade count and f-stop drive the star, which
  links naturally to the bokeh the plugin already models.

**Gaps nobody fills**
1. Flare tied to a **named, real lens**, per preset, the way cineflares catalogues them.
2. Flare that responds live to **focal length, f-stop and focus** on a CineCamera.
3. **Physically derived ghosts** in Unreal at all; every UE product is artist-placed.
4. Anamorphic flare driven by the lens's actual squeeze and coating colour, not a generic streak.
5. A hybrid that uses **sprites for known lights** (off-screen, sun) and **screen-space for
   everything else**, with one look shared between them.
6. Stated, tested **Movie Render Graph** compatibility. DynamicLens already has MRG verification
   habits and a TSR render mode, so it can make this a real claim.
7. Measured data as a source: the Maurer grid-capture method is the only published route to
   measured-per-lens flares, and it targets Nuke, not real-time.

## Sources

- Froyok, custom lens flare: https://www.froyok.fr/blog/2021-09-ue4-custom-lens-flare/
- Froyok, custom bloom: https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
- Froyok, fixing UE4 flare settings: https://www.froyok.fr/blog/2021-04-fixing-ue4-flares/
- Froyok, 2024 anamorphic bloom post: https://mastodon.gamedev.place/@froyok/111910284142231962
- PrettyPostProcess: https://github.com/EscapeEntertainment/PrettyPostProcess
- CustomLensFlaresUE: https://github.com/singinwhale/CustomLensFlaresUE , fork https://github.com/shortnamesalex/CustomLensFlaresUE
- UE5-CompositeLensFlare-Plugin: https://github.com/RoastedKaju/UE5-CompositeLensFlare-Plugin
- Image Space Lens Flares: https://petyu.itch.io/image-space-lens-flares
- Cinematic Lens Flares: https://www.fab.com/listings/74825eda-ba0f-4e0e-abcc-133c6cec9c3c , v3 video https://www.youtube.com/watch?v=Uvp9i59aVSs
- Optic Lens Flares: https://www.fab.com/listings/89697951-111b-4549-9a06-c4099bdfd1a8 , https://3dbrushwork.com/optic-flares , https://3dbrushwork.com/optic-release
- LENS FLARE VFX: https://www.fab.com/listings/0e920fbc-fb78-4331-a4e1-878dc3504bad
- Lens Flares (Fab): https://www.fab.com/listings/2e1d900a-77cd-4f82-be5e-936b75e2cae3
- Sun Flares for Unreal: https://www.fab.com/listings/044562a6-c4b3-4f73-a40e-cfe119b20393
- Custom Lens Flare Tool: https://www.unrealengine.com/marketplace/en-US/product/custom-lens-flare-tool
- Custom Lens Flares Material: https://www.unrealengine.com/marketplace/en-US/product/custom-lens-flares-material
- Custom Len Flare With Niagara: https://www.unrealengine.com/marketplace/en-US/product/custom-len-flare-with-niagara
- Retro Particles & Lens Flares: https://marcis.itch.io/retro-particles-lens-flares
- Prism: https://t-boons.github.io/PrismBlog/
- Ultra Dynamic Sky docs: https://www.ultradynamicsky.com/Documentation/V9/9-7
- StraySpark anamorphic article: https://www.strayspark.studio/blog/anamorphic-film-look-ue5
- UE lens flare docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/lens-flare?application_version=4.27
- UE bloom docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/bloom-in-unreal-engine
- MRQ flare threads: https://forums.unrealengine.com/t/how-to-fix-bad-pixelated-lens-flare-in-movie-render-queue-in-ue-5-2-1/1230859 , https://forums.unrealengine.com/t/niagara-system-not-rendering-in-movie-render-queue-ue-5-2/1209650
- Unity Lens Flare (SRP): https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/shared/lens-flare/lens-flare-component.html , data asset https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/shared/lens-flare/lens-flare-asset.html
- Unity Screen Space Lens Flare: https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/shared/lens-flare/reference-screen-space-lens-flare.html
- ProFlares: http://www.proflares.com/
- Moonflow Lensflare System: https://github.com/Reguluz/Moonflow-Lensflare-System
- Godot Lens Flare Plugin: https://github.com/SIsilicon/Godot-Lens-Flare-Plugin
- realflare: https://github.com/beatreichenbach/realflare
- LensFlareFramework: https://github.com/bodonyiandi94/LensFlareFramework
- Bitsquid physically based lens flare: https://bitsquid.blogspot.com/2017/07/physically-based-lens-flare.html , code https://github.com/greje656/PhysicallyBasedLensFlare
- Hullin et al. 2011: https://resources.mpi-inf.mpg.de/lensflareRendering/
- Patent status: https://patents.google.com/patent/US20140210844A1/en , https://patents.google.com/patent/EP2702565A1/en
- Maurer, Capturing Light with Robots: https://vincent-maurer.github.io/lens-flare-capture/
- John Chapman, screen space lens flare: https://john-chapman.github.io/2017/11/05/pseudo-lens-flare.html
