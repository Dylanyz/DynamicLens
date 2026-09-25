# Lens flares 04 — physically based methods, what they need, what they cost

Research notes, 2026-09-25. Background for the roadmap item "Simulated lens flares matched to the
lens" (`.claude/refs/roadmap.md`). No implementation here.

Conventions: numbers are as reported by each source, on the hardware it names. Anything
not checked against a primary source is marked **(unverified)**. Quotes are kept under 15 words;
everything else is paraphrase.

---

## 1. What a lens flare is made of

A flare from one bright source is the sum of separate physical effects. Each has its own cause, its
own per-lens input and its own rendering method, so it helps to treat them separately:

| Component | Physical cause | Looks like | Depends on |
|---|---|---|---|
| **Ghosts** | light reflected an even number of times between glass surfaces (2 is dominant), then reaching the sensor | iris-shaped polygons/discs along a line through the image centre, each with its own size, colour and defocus | full optical prescription, AR coatings, iris shape and f-stop, light angle, zoom/focus position |
| **Starburst** | Fraunhofer diffraction at the iris edges | spikes centred on the source | iris blade count, blade curvature, f-stop, wavelength |
| **Ringing / fringes** | Fresnel (near-field) diffraction on the ghost outlines | fine rings on ghost edges | aperture shape, propagation distance |
| **Veiling glare** | scatter from surface roughness, dust, coatings, barrel walls, internal edges | low, wide haze lifting blacks around and away from the source | lens build, cleanliness, baffling; measured, not derived |
| **Scatter streaks** | dust, scratches, grease on elements | rainbow speckle, thin streaks through the source | condition of the actual lens copy |
| **Sensor-stack ghosts** | reflections between the sensor, its cover glass / IR-cut / OLPF and the rear element | a ghost near the source, often a red dot grid on some digital cameras | camera body, not lens |
| **Anamorphic streak** | cylindrical elements have power in one axis only, so reflections involving them defocus asymmetrically into lines | long horizontal line through the source, often blue | cylinder prescription, coatings |

The ghosts are what make a flare identifiably *a particular lens*; the starburst identifies the
iris. Everything else is texture.

---

## 2. Methods

### 2.1 Ray-traced ray bundles — Hullin, Eisemann, Seidel, Lee 2011

M. B. Hullin, E. Eisemann, H.-P. Seidel, S. Lee. *Physically-Based Real-Time Lens Flare Rendering.*
ACM Transactions on Graphics 30(4), Article 108 (SIGGRAPH 2011).
DOI https://doi.org/10.1145/2010324.1965003 · project page https://publications.graphics.tudelft.nl/papers/508
· video https://vimeo.com/23687553

**How it works.**
- **Ghost enumeration.** Each ghost is one fixed sequence of two reflections. With *n* surfaces there
  are n(n−1)/2 of them. Because the order of intersections is known in advance, each ghost is traced
  without any search for the next surface.
- **Aperture culling.** Paths that cross the iris three times are mostly blocked at small apertures,
  so they are dropped. That leaves (f(f−1) + b(b−1))/2 ghosts, where f and b count the surfaces in
  front of and behind the iris.
- **Bundle tracing.** A sparse uniform grid of parallel rays is traced from the front element for
  each ghost. It lands on the sensor as a deformed grid, and the cells are rasterised as quads. Cell
  area gives irradiance, and the ray's position at the iris looks up an aperture texture, which is
  what gives the ghost its iris shape and blocked edges.
- **Adaptive resolution.** A precompute pass measures how much each ghost's grid deforms and assigns
  one of six grid sizes, **16² to 512²**. Well-behaved ghosts look fine at very low resolutions,
  even 4×4.
- **Spectral.** Sellmeier dispersion (coefficients from glass catalogues such as Schott). They
  render at 3 (RGB) or 7 wavelengths with an image-space blur between bands. A few ghosts, about 3
  of 140, would need up to 60 wavelengths for smooth results.
- **Coatings.** Single-layer quarter-wave AR model evaluated per ray, per wavelength and per angle.
  The paper notes that real multi-layer coatings are trade secrets, so this is an estimate.
- **Starburst.** The Fourier power spectrum of the aperture (Fraunhofer), scaled linearly per
  wavelength and summed into an RGB texture. Ghost-edge ringing comes from a fractional Fourier
  transform. Dust and scratches are drawn into the aperture before the FFT.
- **Symmetry.** Rotationally symmetric lenses let them trace half the rays and mirror the result.
  **Anamorphic lenses were explicitly not supported.**

**Cost** (GTX 285, 2011). Reported figures run from under 1 fps for the Canon 70–200 zoom at high
quality to about 228 fps for a simple Tessar at standard quality. Their quality comparison shows
6.1 fps (7 bands, filtering, supersampling) against 20.6 fps (RGB, 40% of the darkest ghosts culled).
Culling the weakest 20% of ghosts gave about a 20% speedup with no visible change. Precompute takes
under 0.1 s for a 9-ghost Tessar, 5 min for a 142-ghost Nikon zoom, and 20 min for the 312-ghost
Canon zoom (over 90 light directions × 20 zoom steps × 8 stops). Against a dense reference render
(159 s on a GTX 580), the sparse version took 29.8 ms per frame.

**Quality.** The reference for everything that followed. It handles nonlinear ghost deformation and
caustics, which the linear methods cannot.

**Per-lens input.** Full prescription (radii, thicknesses/spacings, glass per element, clear
apertures), iris position, iris shape, and a coating assumption. The lens prescriptions came from
Smith's *Modern Lens Design* and a patent (Ogawa 1996).

### 2.2 Paraxial matrix method — Lee & Eisemann 2013

S. Lee, E. Eisemann. *Practical Real-Time Lens-Flare Rendering.* Computer Graphics Forum 32(4)
(EGSR 2013). DOI https://doi.org/10.1111/cgf.12145 · preprint
http://cg.skku.edu/pub/papers/2013-lee-egsr-matrixflare-cam.pdf

**How it works.** First-order (paraxial) ray-transfer matrices. Each translation, refraction and
reflection is a 2×2 matrix, and a ghost's path is their product, split into entrance-to-iris (Ma) and
iris-to-sensor (Ms). Four corner rays give one textured quad (sprite) per ghost; the aperture
texture supplies the shape. Refinements:
- clip the quad to rays that actually pass the iris (1.7–5.2× less rasterisation)
- colour: the AR coating is evaluated for one central ray per ghost, so each ghost gets a single colour
- one pass for all three wavelengths using a slightly enlarged quad (about 2× faster)
- intensity culling (about 30% more)
- the starburst is a precomputed spectral FFT, as in 2011

**Cost** (GTX 680, 1280×720):

| Lens | This method | Hullin 2011 at 512² grid |
|---|---|---|
| Canon zoom 70–200, f/11 | 3.83 ms (261 fps) | 261.55 ms |
| Nikon zoom 80–200, f/8 | 1.36 ms | 78.82 ms |
| Angenieux, f/22 | 0.62 ms | 16.62 ms |
| Heliar Tronnier, f/22 | 0.67 ms | 7.60 ms |

That is a 10–70× speedup and no preprocessing.

**Quality.** Near-identical on nearly linear lenses (Heliar), with SSIM 0.98 against the reference.
Weaker on complex zooms (SSIM 0.77 on the Nikon), because deformation, folding and aberration are
not captured. About 24 of the Canon's 312 ghosts behave strongly nonlinearly. A 3D deformation
lookup texture fixes them, but costs about 96 MB per ghost, so the authors did not use it.
Paraxial means it degrades at large off-axis angles. It is the wrong model for fisheyes.

**Per-lens input.** Radii, spacings and refractive indices (plus Sellmeier/Abbe for dispersion),
entrance pupil and iris heights. Other clear apertures are ignored.

**Patent warning.** The method is covered by US patents assigned to Sungkyunkwan University,
inventor Sungkil Lee:
- US 9,595,132 — Google Patents lists it as expired, fee-related
- US 10,074,195 — Google Patents lists it as **active, anticipated expiry 2034-06-18**

Both cover linear-paraxial-approximation flare rendering and blending in non-linear patterns.
https://patents.google.com/patent/US10074195 · https://patents.google.com/patent/US9595132
**(Legal status is from Google Patents, not verified. This is not legal advice. Apache-2.0's
patent grant covers only contributors' own patents, not third-party ones.)**

A game-oriented write-up of the matrix method, with patent-sourced zooms (Nikon 28–75 US5835272,
Canon 28–80 US5576890, Canon 36–135 US4629294) and aspheres treated as spheres:
*Implementation Notes: Physically Based Lens Flares*, Placeholder Art, 2015,
https://placeholderart.wordpress.com/2015/01/19/implementation-notes-physically-based-lens-flares/

### 2.3 Polynomial optics — Hullin, Hanika, Heidrich 2012, and successors

M. B. Hullin, J. Hanika, W. Heidrich. *Polynomial Optics: A Construction Kit for Efficient
Ray-Tracing of Lens Systems.* Computer Graphics Forum 31(4) (EGSR 2012).
DOI https://doi.org/10.1111/j.1467-8659.2012.03132.x

**How it works.** Each element is replaced by a truncated Taylor expansion of its analytic ray
transfer. Elements are composed into whole-path polynomials, so evaluating a ray costs the same
however many elements the lens has. It is a strict generalisation of the matrix method, which is
the degree-1 case, and it captures aberrations. It is exact enough for production.

**Successors:**
- **Schrade, Hanika, Dachsbacher 2016.** *Sparse high-degree polynomials for wide-angle lenses.*
  CGF 35(4), EGSR 2016. https://doi.org/10.1111/cgf.12952 · code https://github.com/hanatos/lensoptics
  and https://github.com/lcrs/sparsepolyoptics. Fits sparse terms up to degree 15. **Handles
  fisheyes and aspheres**, which Taylor expansions do not.
- **Bodonyi, Csoba, Kunkli 2024.** *Real-time ray transfer for lens flare rendering using sparse
  polynomials.* The Visual Computer. https://doi.org/10.1007/s00371-024-03625-7 . Ray transfer
  takes 0.43–0.83 ms, and the **whole flare 1.84–2.01 ms at 1920×1080 on a TITAN Xp**.
- **Bodonyi & Kunkli 2023.** *Efficient tile-based rendering of lens flare ghosts.* Computers &
  Graphics 115, 472–483. https://www.sciencedirect.com/science/article/pii/S0097849323001486 .
  Tiled software rasterisation of ghost quads, which scales better with many lights and complex
  lenses.
  - Reference implementation for both Bodonyi papers: **BSD-2-Clause**
    https://github.com/bodonyiandi94/LensFlareFramework (D3D11 / GL 4.3 compute).

**Production use.** E. Pekkarinen, M. Balzer (Animal Logic). *Physically Based Lens Flare Rendering
in "The Lego Movie 2".* DigiPro 2019. https://doi.org/10.1145/3329715.3338881 · PDF
https://animallogic.com/wp-content/uploads/2023/06/Physical-Based-Lens-Flare-Rendering.pdf
- Built on the Polynomial Optics Toolkit inside their path tracer, Glimpse. Monte Carlo splatting,
  with ghost paths and front-lens cells sampled adaptively. That raised the sensor hit rate from
  about 15% to about 90%.
- Two-reflection paths only: 29 interfaces give 406 paths. Fresnel per path, with a Schlick angle
  term at the front element.
- Coating presets, a front-lens dirt map, and occlusion by scene, housing and iris.
- **Supported anamorphic, wide-angle and zoom systems.**
- Cost: 720p preview under 10 s, 2K final under 15 min. About 50 shots.
- **No diffraction**, because polynomial optics is geometric. The starburst has to come from
  somewhere else.

### 2.4 Newer: learned / precomputed transport

Y. Chen et al. *Precomputed Lens Transport Maps.* arXiv 2605.04017, 2026.
https://arxiv.org/abs/2605.04017
- Small MLPs per light path, with an occlusion classifier, replace ray tracing.
- About **10–15× faster than brute-force tracing**, but offline: 74 s against 1180 s for a flare
  render.
- About 4 GB of training samples per path. One fixed zoom and aperture per model. Rotationally
  symmetric only.
- Not a real-time candidate today. Noted as the direction of travel.

### 2.5 Measurement-fitted ghosts — no prescription at all

A. Walch, C. Luksch, A. Szabó, H. Steinlechner, G. Haaser, M. Schwärzler, S. Maierhofer. *Lens
flare prediction based on measurements with real-time visualization.* The Visual Computer 34 (2018)
1155–1164. https://doi.org/10.1007/s00371-018-1552-4 · PDF
https://www.vrvis.at/publications/pdfs/PB-VRVis-2018-014.pdf

**How it works:**
- Capture HDR brackets of a point source stepped across the field along one axis.
- Describe each ghost with a compact parametric shape (Bézier-segment outline, intensity falloff
  from the edge, colour).
- Optimise the parameters against each capture on the GPU, using an edge-aware cost function
  because plain MSE fails on real footage.
- Fit a low-order polynomial to each parameter over the light angle.
- At runtime, evaluate the polynomials and rotate the result to the light's azimuth, which is valid
  for rotationally symmetric lenses.

**Why it matters here.** The authors' motivation is exactly our problem: coatings and internals are
secret, so measure instead. This is the path for lenses with no prescription. The catch is that it
is only valid at the captured settings (f-stop, focal length, focus), unless you capture a grid of
them.

### 2.6 Commercial / open tools, for comparison

- **realflare** (Beat Reichenbach). Spectral GPU ray tracing after Hullin 2011, as a Nuke-style
  standalone tool. **GPLv3**. Takes prescriptions from the OpticsExplorer database.
  https://github.com/beatreichenbach/realflare
- **Jean-Philippe Grenier / Bitsquid (Autodesk Stingray) 2017** **(author unverified)**. Hullin-style
  bundles on compute.
  - Nikon 28–75 (27 surfaces): **352 ghosts, 32×32 grid, about 12 ms** (3 ms trace, 9 ms raster).
  - Iris: an SDF polygon with curved-blade offset.
  - Starburst: FFT (Intel D3D11 FFT), summing about 4 wavelengths, with spiral filtering against
    ringing.
  - Coating: quarter-wave, with a thickness-offset control.
  - Prescription: parsed from the patent.
  - https://bitsquid.blogspot.com/2017/07/physically-based-lens-flare.html · code (no licence
    stated) https://github.com/greje656/PhysicallyBasedLensFlare
- **Screen-space / image-based** flares are not lens-specific. They copy bright pixels, flipped,
  scaled and tinted, towards the screen centre:
  - Unreal's built-in post-process Lens Flares (bokeh-shape texture, per-ghost tints, image-based)
  - Unity HDRP/URP Screen Space Lens Flare (2023.1+)
    https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/shared/lens-flare/Override-Screen-Space-Lens-Flare.html
  - J. Chapman, *Pseudo Lens Flare* (2013)
    http://john-chapman-graphics.blogspot.com/2013/02/pseudo-lens-flare.html
  - Froyok's UE custom lens flare, which needs a small engine patch
    https://www.froyok.fr/blog/2021-09-ue4-custom-lens-flare/
- **Element / artist systems.** Knoll Light Factory (John Knoll, ILM; built from lens "primitives"),
  Video Copilot Optical Flares, Nuke's Flare node. These are hand-built stacks of ghosts, glows and
  streaks, with no optical model. The per-lens look comes from an artist matching reference.
  https://www.maxon.net/en/product-detail/red-giant/universe/knoll-light-factory-ez
- **Studio talks.** No public GDC or SIGGRAPH talk describing prescription-driven flares in
  Frostbite, Call of Duty, Decima or Star Citizen was found. Those engines should not be cited for
  this without a source **(searched, nothing found)**.

### 2.7 Diffraction starburst, glare and the rest

- **Starburst = |FFT(aperture)|², scaled by wavelength.**
  - Fraunhofer: the far-field pattern is the power spectrum of the aperture transmission. Image-space
    scale grows linearly with λ, so a white-light starburst is the sum of scaled copies, one per
    wavelength (Hullin 2011; Ritschel et al. 2009).
  - Ritschel et al. 2009: T. Ritschel, M. Ihrke, J. R. Frisvad, J. Coppens, K. Myszkowski,
    H.-P. Seidel. *Temporal Glare: Real-Time Dynamic Simulation of the Scattering in the Human Eye.*
    CGF 28(2). https://doi.org/10.1111/j.1467-8659.2009.01357.x
  - Supersample the aperture before the FFT to avoid aliasing.
  - Rule of thumb: straight blades give N spikes for even N and 2N for odd N **(standard optics
    result, not re-derived here)**. Rounded blades weaken and blur the spikes. Stopping down
    lengthens them.
  - Spike spacing and length scale with 1/aperture diameter.
  - Cost: one offline FFT per iris shape and stop. Nothing at runtime beyond a texture.
- **Dust and scratches in the pupil.** Y. Wu et al. *How to Train Neural Networks for Flare
  Removal.* ICCV 2021. https://arxiv.org/abs/2011.12485 . Synthesises scattering flare by adding
  random dots and streaks to the aperture function, then running a wave-optics PSF. It is a
  physically grounded recipe for the rainbow-streak "dirty lens" component.
- **Veiling glare.** Standardised as ISO 9358 (veiling glare index, glare spread function).
  https://www.iso.org/standard/17042.html . In practice it is a very wide, low-amplitude PSF. It is
  measured, not derived from a prescription, so model it as a convolution kernel skirt.
- **Sensor-stack ghosts.** The sensor, cover glass, IR-cut filter and OLPF are extra flat, coated
  surfaces behind the last element. Adding them as planar interfaces gives the camera-body ghosts.
  - The digital "red dot" pattern is usually explained as light reflected off the sensor's periodic
    microlens array (a diffraction grating) and back off the rear element
    **(mechanism widely stated in photography press, not verified against a primary source)**.
- **Anamorphic streaks.** Cylindrical elements focus in one axis only. Ghosts formed by reflections
  at or across cylinder surfaces are defocused differently in x and y and smear into lines.
  - The de-squeeze also stretches every ordinary round ghost horizontally, a partial explanation
    given by B. Wronski, https://bartwronski.com/2015/03/09/anamorphic-lens-flares-and-visual-effects/ .
    He also notes film-style extreme streaks are not reproducible with a 2:1 stretch alone.
  - The **blue** colour is usually attributed to the residual reflectance colour of the coatings
    used on classic anamorphics **(folk explanation, unverified; some sets flare amber or white)**.
  - A physical model needs non-rotationally-symmetric tracing. Hullin 2011 does not support it;
    Animal Logic's polynomial pipeline does.

---

## 3. Per-lens data requirements

| Input | Needed by | Where it comes from | Usually available? |
|---|---|---|---|
| Surface radii, axial spacings | ghosts (all physical methods) | patent numerical example | yes, if a patent matches the product |
| Glass per element (nd, Vd, or catalogue name) | ghosts, dispersion | patent (nd/νd), glass catalogues for Sellmeier (Schott, Ohara, Hoya) | nd/νd yes; exact glass often has to be inferred |
| Aspheric coefficients | ghosts on modern lenses | patent | yes, but matrix/Taylor methods ignore them; needs ray tracing or sparse polynomials |
| Clear apertures (element diameters) | vignetting of ghosts, which ghosts survive | patent (often missing), or estimated from the lens's physical size | **often missing** |
| Iris position | ghost enumeration, clipping | patent (often omitted) | **often missing**, can be placed at the pupil |
| Variable spacings per focus/zoom position | ghosts that move with focus and zoom | patent tables for zooms and floating-focus designs | usually for zooms, sometimes for focus |
| AR coating (layers, indices, thicknesses) | ghost colour | **never published**; single-layer quarter-wave or multi-layer guess, fitted to reference | **no**, assume or fit |
| Iris blade count, curvature, rotation | ghost shape, starburst | spec sheets, photographs, CINEFLARES footage | yes |
| Sensor stack (cover glass / IR / OLPF) | camera ghosts | camera body; roughly estimated | rough |
| Veiling glare, dirt | haze, streaks | measured, or artistic | no |

**What can be approximated without a prescription:**
- **Iris and starburst: fully**, from blade count and shape alone.
- **Ghost count, colours and rough sizes: by fitting to reference footage**, as Walch 2018 does.
- **Ghost motion vs light angle.** For a symmetric lens every ghost lies on the line through the
  source and the centre, at a fixed ratio that varies slowly with angle. A per-ghost polynomial in
  field angle captures it.
- **What you lose:** exact behaviour away from the captured settings, correct change with focus and
  f-stop, and nonlinear caustic ghosts.

A middle path, **prescription-shaped fitting**: take a *similar* published design, such as the patent
behind the same optical family, trace it, then fit coatings and a few scale factors to reference
footage of the real lens. **(Proposed, not taken from a paper.)**

---

## 4. Where prescriptions come from, and licence caveats

- **Patents (primary source).** US patent text and drawings are generally not subject to copyright
  restrictions (37 CFR 1.71(d)/(e) exceptions aside). https://www.uspto.gov/terms-use-uspto-websites .
  - Other offices (JP, DE, EP) are broadly similar **(unverified per office)**.
  - Caveats:
    - A patent's numerical example is often not the production lens.
    - Diameters and iris position are frequently omitted.
    - Matching a cine lens to its patent is detective work.
  - **Record the patent number and example number as the `Source`,** mirroring the preset data
    convention.
- **Cine lenses we ship:**
  - No prescriptions for the ARRI/Zeiss Master Prime or Ultra Prime, Cooke S4 or Canon K35 turned
    up in this search. Zeiss did patent the Master Prime focusing designs, per CinemaTechnic
    https://cinematechnic.com/optics/arri_zeiss_master_primes/ , but the patent numbers were not
    located **(unverified)**.
  - Canon K35s are reportedly derived from FD-era designs with aspheres **(unverified)**, so FD
    patents are a plausible starting family.
  - Andy Davis's grids and the tiedtke ST maps are distortion data. **They contain nothing about
    internal reflections.**
- **lens-designs.com** (D. Reiley). A collaborative library of designs from patents and
  publications. It states that all files are public domain, and it includes 11 cine zoom designs from
  Iain Neill. https://www.lens-designs.com/
- **nzhagen/LensLibrary.** Patent designs with Zemax files, **MIT licence**.
  https://github.com/nzhagen/LensLibrary
- **PhotonsToPhotos Optical Bench / Hub** (W. J. Claff). About 10,000 patent prescriptions, around
  1,280 in the curated Hub matched to production lenses.
  https://www.photonstophotos.net/GeneralTopics/Lenses/OpticalBench/OpticalBenchHub.htm
  - The site is marked **"All Rights Reserved"** and has no download.
  - Use it to **find** the patent, then transcribe from the patent itself. **Do not copy the
    compilation.**
- **OpticsExplorer.** Free lens simulator/database, used by realflare. Licence terms were not found
  **(unverified)**. Treat it like PhotonsToPhotos.
- **Zemax LensVIEW.** Commercial: over 5,000 patents and 21,000 examples, licensed with OpticStudio.
  **Not redistributable.**
- **Books** (Smith *Modern Lens Design*, Cox, Kingslake). The individual numbers come from patents,
  but the book is a copyrighted compilation. Cite the underlying patent, not the book.
- **Glass data.** Sellmeier coefficients from manufacturer catalogues (Schott, Ohara, Hoya), or
  refractiveindex.info. Check each source's terms before shipping a table **(unverified)**.
- **CINEFLARES** (Markus Förderer). Footage of hundreds of cine lenses shot under controlled
  motion-control conditions. https://www.cinelensflares.com/ ·
  https://www.cined.com/cineflares-com-launched-worlds-most-complete-lens-flare-library-by-markus-forderer-asc/
  - It is excellent **reference to fit against and judge by eye**, but it is a commercial
    product. Using its frames as fitting data, or shipping anything derived from them, needs
    permission. It goes in `SOURCES.md` as an idea source only unless licensed.

---

## 5. Recommended technique tiers

| Tier | What | Per-lens data | Runtime cost | Quality | Notes |
|---|---|---|---|---|---|
| **A. Starburst + glare** | offline FFT of the iris (blade count, curvature, stop); veiling-glare skirt | blade count/shape only | a texture | physically right for diffraction | can drive UE Convolution Bloom's kernel texture; one kernel per view, so per camera |
| **B. Artist-fitted ghost table** | per preset, a list of ghosts: offset ratio vs field angle (polynomial), size, iris-shape texture, colour, falloff; fitted to reference (Walch 2018 style) | reference footage or photos per lens | sprites, well under 1 ms for tens of ghosts **(estimate)** | matches reference at the captured settings; approximate elsewhere | no prescription needed; covers anamorphics by using line sprites |
| **C. Offline-traced, baked ghosts** | offline Hullin-style bundle trace or polynomial optics over a grid of (field angle × f-stop × focus/zoom), baked into Tier B's table, keeping only the strongest N ghosts | full prescription + coating guess | same as B | physically derived, including colour and shape changes with stop | tracing runs in `Tools/` Python/C++, no editor; the runtime format is shared with B |
| **D. Runtime ghost tracing** | Hullin bundles, or sparse polynomial ray transfer (Bodonyi 2024) in a compute pass | full prescription | about 2 ms (sparse polynomials, 1080p, TITAN Xp) to about 12 ms (bundles, 352 ghosts) | best, including nonlinear caustics and live focus/zoom | needs a C++ render pass; BSD-2 reference code exists |
| **(avoid) Runtime paraxial matrix** | Lee 2013 | prescription | under 4 ms (GTX 680) | good on simple lenses, poor on zooms and fisheyes | **US 10,074,195 may be in force until 2034**; get advice before shipping |

Use the screen-space pseudo-flare (Unreal's built-in or Chapman-style) only as a fallback for
emissive surfaces and specular hits that are not modelled as lights. It is not lens-specific.

---

## Implications for DynamicLens

1. **Start with the two pieces that need no prescription.**
   - **Tier A** is fully physical from data we already hold or can read off a photo: blade count and
     blade curvature for each preset. The FFT is offline Python in `Tools/`, and it ships as a
     texture per preset (or per iris kit).
   - **Tier B** gives every preset a ghost table fitted to reference. That covers the tiedtke,
     Lanthimos and custom lenses, where no prescription will ever exist.
2. **Make B and C share one runtime format.** A traced lens (Tier C) and a fitted lens (Tier B)
   then render identically, and upgrading a lens is a data change. It fits the existing rule:
   `presets.json` stays the source of truth, a new `flare` block per profile carries a `Source`
   ("patent USxxxx example N", "fitted to reference", or "assumed"), and the importer bakes the
   assets.
3. **Prescriptions need their own provenance discipline.**
   - Transcribe from patents only.
   - Cite the patent in `SOURCES.md`.
   - Never import from PhotonsToPhotos, LensVIEW or books.
   - Coatings are always "assumed" or "fitted". None of the ARRI/Zeiss, Cooke or K35 prescriptions
     are in hand yet.
4. **Fisheyes and anamorphics rule out the cheap physics.** Paraxial matrices fail at large field
   angles. Hullin 2011 cannot do anamorphics. Sparse high-degree polynomials (Schrade 2016), or an
   offline full trace baked to a table (Tier C), handle both.
5. **The rendering path is a design decision.**
   - Ghost sprites positioned from light sources are awkward in a post-process material. They fit a
     C++ SceneViewExtension/RDG pass, which means a build and an editor restart per change.
   - Convolution Bloom can carry the starburst and glare kernel through the existing CineCamera
     post-process settings, with no new render pass.
   - Movie Render Graph behaviour must be checked, given the known overscan issues.
6. **Light sources.** Physical methods need a source's direction and intensity, not bright pixels.
   Use directional/point lights projected through the lens's own distortion so the ghosts line up.
   Animal Logic applies the scene's distortion to flare samples for exactly this reason. Add
   occlusion testing, and keep screen-space flare as the fallback for everything else.
7. **Legal.** Avoid Lee & Eisemann's runtime matrix method unless someone confirms the status of
   US 10,074,195. Offline tracing plus baked sprites, or runtime bundle/polynomial tracing, sidesteps
   it **(not legal advice)**. Anything derived from CINEFLARES footage needs permission, and a
   `NOTICE` entry, before it ships.
