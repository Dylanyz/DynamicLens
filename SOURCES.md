# Sources

DynamicLens is a fitting and playback engine; almost nothing in it was invented here. This page
records what the plugin was built from, and what each source actually contributed. Licence terms
for the measured data are in [NOTICE](NOTICE) — this page is about provenance and ideas, not
licensing.

---

## Andy Davis — Imagery for Media

<https://imag4media.com/> · [VFX RnD](https://imag4media.com/vfx-rnd/) ·
[Fab](https://www.fab.com/sellers/Imagery%20for%20Media)

Andy Davis publishes lens research and datasets openly, out of what he describes as a Siggraph
spirit of sharing. The plugin takes both data and design from him.

**The core idea of the plugin comes from his "Dynamic lens models" post.** His argument: a real lens
model needs distortion measured at many witness marks between minimum focus and infinity, which
traditionally means a stack of heavy 32-bit EXRs — far too much to page through interactively.
Converted instead to *roughly a dozen float values per lens for spherical, a few more for
anamorphic*, the same model becomes cheap enough to evaluate live. That is precisely the plugin's
parametric path: Brown-Conrady coefficients per focal and focus distance, interpolated per frame.
See [architecture.md](.claude/refs/architecture.md).

**Lens breathing as a first-class parameter** is his observation too — that distortion (and, more
subtly, vignetting) changes continuously across a focus pull, and that a lens model which ignores
this is not a lens model. The component's `Breathing` control exists because of this.

**His working rules for ST maps** shaped how the ST-map path is written:

- Maps match the **sensor size, not the image resolution** — bigger images can come off smaller
  sensors, and resizing a map needs extreme caution.
- Distortion must be captured at **multiple focus distances** or the breathing is lost.
- **Anamorphics amplify every imperfection** across the squeeze; even a slight rotation mismatch
  between lens and body shows.
- Universal maps are for **look-dev only**. Production tracking needs grids shot on the actual lens;
  cinema lenses are largely handmade and copies of the same model differ.

That last point is why the plugin's presets are framed as *looks*, not as a substitute for a
tracking team's solve.

**Data used:** his distortion grids for ARRI/Zeiss Master Prime and Zeiss Supreme, fitted into the
`AD_*` profiles (`Tools/data/raw/`). These carry focus-distance samples, so they drive breathing.

**Data used, ST maps:** his freely published *creative lens maps*, spherical sets only —
22 series, 110 primes, under `Content/Profiles/AndyDavis` as the `DL_AD_*` ST-map presets.
Single-focus, so they do not breathe, but they carry real measured character. His anamorphic sets
are deliberately excluded; see below. Prepared by `Tools/prep_andy_stmaps.py`, imported by
`dl.import_andy_stmaps()`.

**Not yet used:** his wider preset release covers 100+ lenses and 20+ camera bodies in the
coefficient format — Cooke Panchro and TelePanchro, Hawk V-Lite 1.3x, Panavision MacroPanatar and
Sphero65. Those carry focus stacks, so unlike the ST maps they would breathe.

**Further reading he recommends,** all of it relevant to this plugin:

- [Steve Yedlin — Nerdy Film Tech Stuff](https://yedlin.net/NerdyFilmTechStuff/index.html)
- [Five Pillars of Anamorphic](https://vimeo.com/search?q=five%20pillars%20of%20anamorphic)
- [ShareGrid — lens sets](https://www.sharegrid.com/learn/lens-sets)
- [The Cine Lens Manual](https://www.cinelensmanual.com/)

---

## tiedtke — Real Cinema Lenses

<https://tiedtke.gumroad.com/l/realcinemalenses> ·
[YouTube](https://www.youtube.com/channel/UCVuRDikGULup2KloWP8qg-g)

Unreal-native LensFile assets and ST-map textures for 85 anamorphic primes across 19 series —
Panavision C/D/E/G/Primo/AutoPanatar, Cooke FFi and SFi, Atlas Orion, Lomo Round Front, Kowa Cine
Prominar, Iscorama Pre-36, Hawk V-Lite Vintage, Elite MK, Todd-AO, Cineovision, ARRI/Zeiss Master
Anamorphic, Angénieux Optimo, PS-Technik. Every `DL_T_*` preset is one of these.

These are single-focus maps — one per prime, no focus stack — so `DL_T_*` presets have real measured
character but no breathing.

### These are the same maps as Andy Davis's

The anamorphic ST maps in this pack and Andy Davis's freely published creative lens maps are not
merely similar lenses measured similarly. They are the same measurements.

Measured 2026-09-16. We sampled tiedtke's maps on a 32×32 grid and read the corresponding pixels
straight out of Andy's EXRs, for **18 lenses across 15 series** — Panavision C/E/G/Primo/
AutoPanatar, Cooke SFi and FFi, Atlas Orion, Lomo Round Front, Kowa Cine Prominar, Elite MK,
Hawk V-Lite Vintage, Iscorama Pre-36, Todd-AO, Cineovision.

| | |
|---|---|
| RMS difference | 0.000105 – 0.000109, every lens |
| Max difference | **0.000244, every lens** |
| One half-ulp of float16 at 0.5 | **0.000244** |

The entire discrepancy is our own grids being stored as half-floats. The underlying data is
identical. Peak displacement still varies correctly per lens (0.030 Cineovision → 0.056 Panavision C
75 mm), so these are genuinely different lenses, correctly paired — not one map compared against
itself.

Andy Davis describes assembling this set himself: grids accumulated over two decades, converted into
a coherent stMap set and published in August 2023. So he is the documented origin of the published
maps. How they reached the *Real Cinema Lenses* pack is not something the pixel data can answer, and
this note makes no claim about it. **Credit both, and do not describe them in docs as two
independent measurement sets.**

Practical consequence: re-importing Andy's anamorphic maps would add nothing at all — every one of
his 16 anamorphic sets is already shipping as a `DL_T_*`. His *spherical* sets are the real gap,
since every `DL_T_*` is anamorphic.

---

## H. H. Nasse — *Depth of Field and Bokeh*

Carl Zeiss Camera Lens Division, March 2010.
[PDF](https://diglloyd.com/articles/ZeissPDF/ZeissWhitePapers/Zeiss-DepthOfField-Bokeh.pdf)

A freely published Zeiss white paper. It supplied the **physics and the vocabulary** behind the
component's Bokeh block — no fitted numbers came from it.

| From the paper | Where it lives |
|---|---|
| Spherical aberration sets the *character* of a blur disc: under-corrected reads as a soft-edged, bright-core background blur; over-corrected as a bright rim, the soap-bubble look | `SphericalAberration`, driven into Epic's Accumulation DOF |
| Coma throws comet-shaped highlights toward the edges, typical of fast vintage glass | `Coma` |
| Mechanical vignetting: off-axis, the barrel clips the entrance pupil, so round highlights become cat's eyes toward the corners | `BarrelRadiusMm` / `BarrelLengthMm`, evaluated as a two-disc overlap against the pupil |
| Natural falloff follows cos⁴ of the field angle, separately from mechanical clipping | `Vignette` in Physical mode |
| The diaphragm alone does not make bokeh, but blade count and blade *rounding* set the highlight polygon | `Blades`, `BladeCurvature` |

Zeiss **lens data sheets** are a separate source, cited per-profile in the `Source` field of
`Tools/data/presets.json` — front diameter, image circle, minimum T-stop and iris construction.
Where a data sheet did not give a number, the profile says "assumed".

---

## Everything else

Values that came from neither measurement nor a data sheet — reconstructions of a look, or
judgement calls — are marked "assumed" in the relevant profile's `Source` field in
`Tools/data/presets.json`. That marking is how the honest numbers are told from the guesses; keep
doing it. See [.claude/rules/preset-data-flow.md](.claude/rules/preset-data-flow.md).
