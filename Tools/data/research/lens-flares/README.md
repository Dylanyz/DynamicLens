# Lens flares — research (2026-09-25)

Research only; nothing implemented. Answers the open questions in `.claude/refs/roadmap.md`
("Simulated lens flares matched to the lens").

| File | Covers |
|---|---|
| `01-cineflares.md` | CINEFLARES Lens Lab: what it is, how it is shot, terms, which of our lenses it has |
| `02-unreal-builtin.md` | UE 5.8's own flare, convolution bloom, hook points, sprite flares, MRQ/MRG, read from engine source |
| `03-existing-plugins.md` | Every UE flare plugin/pack found, plus Unity/open-source/academic implementations, with licences |
| `04-physically-based-methods.md` | Hullin 2011, Lee & Eisemann 2013, polynomial optics, FFT starbursts, fitted models, prescription sources |

## The short version

- **Nobody does per-lens flares in Unreal.** Every plugin is screen-space or Niagara sprites with
  mood presets. None reacts to focal length, f-stop or focus. That gap is the opportunity. (03)
- **CINEFLARES is photographed, not simulated**: a point light shot through ~120 real lenses at
  every stop. No downloads, and its terms forbid use as data. It is a **visual reference** to tune
  against by eye, and the rig (collimated point light, stops to T4) is easy to reproduce in UE. (01)
- **Epic's built-in flare can't be the base.** Fixed 8 mirrored ghosts, no lens knowledge, off
  below High scalability, and a plugin cannot replace the pass without an engine change. (02)
- **Convolution bloom can carry the per-lens starburst and veiling glare** via each camera's
  `BloomConvolutionTexture`. Kernel must be a no-mip, never-stream texture; swap textures rather
  than rewriting pixels, or the cache won't refresh. (02)
- **Ghosts need our own pass**: a `FSceneViewExtension` on `EPostProcessingPass::MotionBlur`
  (linear HDR, pre-bloom). RoastedKaju's CompositeLensFlare (Apache-2.0) already proves this route
  without an engine patch. (02, 03)
- **Physically based ghosts need a lens prescription**, and none turned up for the lenses we ship.
  Patents are the realistic source; coatings are never published. Starbursts need only iris blade
  count and shape. (04)
- **MRQ/MRG high-res tiling breaks screen-space flares** (ghosts mirror per tile). Needs a real
  test before claiming render support. (02)

## Recommended direction (for when this is proposed)

Mirrors how distortion already works: bake offline, evaluate live, JSON as source of truth.

| Tier | What | Needs |
|---|---|---|
| A | Starburst + glare kernel from an FFT of the iris, per blade count / stop bucket, via convolution bloom | blade count, blade shape; no prescription |
| B | Per-preset ghost table (position along the axis, size, colour, softness), fitted by eye to CINEFLARES/own reference | reference footage |
| C | Same table format, baked from an offline trace of a patent prescription | a prescription |
| D | Runtime ray tracing | not worth it yet |

- B and C share one `flare` block per profile in `presets.json`, with a `Source` field marking
  fitted vs traced vs assumed, same as the rest.
- Sprites for known lights (sun, practicals) handle off-frame sources; screen-space handles the rest.
- Ghosts should follow the preset's distortion so they sit where the lens would put them.

## Traps to remember

- Lee & Eisemann's matrix method appears patented (US 10,074,195, reported active to 2034;
  unverified). Hullin's ray bundles appear unencumbered (applications abandoned/withdrawn;
  check again before shipping).
- realflare is GPL-3.0 and several UE flare repos have no licence: ideas only, not code.
- PhotonsToPhotos prescriptions are all rights reserved; lens-designs.com and nzhagen/LensLibrary
  are the permissive ones. Maurer 2024's dataset is partly CC BY-SA: keep it out of Apache paths.
- Any source used goes into `SOURCES.md`; anything used as data into `NOTICE`.
