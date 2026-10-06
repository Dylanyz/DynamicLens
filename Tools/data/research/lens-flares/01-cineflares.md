# CINEFLARES Lens Lab — what it is and what it means for DynamicLens

Researched 2026-09-25 for the roadmap item "Simulated lens flares matched to the lens". Only the public
pages were read: no account was created and nothing was downloaded. The lens library and the Scene
Preview tool sit behind a login, so everything about individual clips comes from the site's public
News posts, its FAQ, its About page and press coverage. Anything not confirmed from one of those is
marked **unverified**.

## 1. What the site is

- **CINEFLARES | LENS LAB** is a browser app for comparing cine lenses by how they flare. Its main
  tool is a side-by-side player with synced timecode. https://lenses.cineflares.com/ (FAQ, "What is
  CINEFLARES?")
- `cineflares.com` redirects (302) to `lenses.cineflares.com`. An older marketing page is at
  https://www.cinelensflares.com/.
- **Who makes it:** cinematographer **Markus Förderer ASC BVK**, whose credits include *September 5*,
  *Red Notice* and *I Origins*. The footer credits him with conceiving and performing the tests
  (https://lenses.cineflares.com/about). The legal owner is **TrueLens Productions, Inc. dba
  CINEFLARES** (https://lenses.cineflares.com/termsofuse). The web app was built by Arbyte
  (http://arbyte.solutions/, linked from the footer).
- **History:** the idea began about ten years before launch, during prep for the film *Hell*
  (https://www.cined.com/cineflares-com-launched-worlds-most-complete-lens-flare-library-by-markus-forderer-asc/).
  The beta was shown at Camerimage in late 2023, and PRO launched in April 2024 at NAB
  (https://lenses.cineflares.com/news, https://www.cined.com/cineflares-pro-launched-with-markus-forderer-asc/).
  Scene Preview arrived in June 2024. The site passed 100 lens sets in June 2025
  (https://www.cinegearexpo.com/la-expo/news-info/cineflares-pro-reaches-milestone-of-100-lens-sets-profiled-in-interactive-lens-flare-library/).
- **Size:** the About page says the PRO library holds "over 115 lens sets"
  (https://lenses.cineflares.com/about). The September 2026 CP.3 news post says "over 130 lens sets"
  (https://lenses.cineflares.com/news). The FAQ counts **1250+ individual test clips**, each one a
  focal length at a T-stop. The free tier has 15 lenses (https://lenses.cineflares.com/pricing).

## 2. Photographed or simulated? Photographed.

All the flares are **real footage shot through each lens**. No flare comes from a ray tracer or a
lens prescription.

- **Method:** each lens films a calibrated point light source against deep black. The camera body,
  the light and the conditions are the same for every lens, and camera movement and exposure are
  motion-controlled (https://lenses.cineflares.com/about and the FAQ answer on the testing method).
- **Published test spec** (https://lenses.cineflares.com/about):
  - Light: full-spectrum daylight LED, 5600 K, 12 ft away, collimated to near infinity.
  - Cameras: RED V-Raptor 8K VV and RED Monstro 8K VV. Spherical lenses are shot in 8K raw,
    anamorphics in 5.2K. The camera is balanced to 5600 K.
  - Playback: 1080p, 10-bit, H.265, 24 fps. The FAQ adds that the 8K capture is downsampled for the
    web.
  - T-stops: every stop from wide open to T4 (https://lenses.cineflares.com/pricing). Some lenses go
    further. Zone T1 was shot T1 to T5.6, and a Cooke S4 clip pulls the iris from T22 to T2
    (https://lenses.cineflares.com/news).
  - Tests are shot in Open Gate VistaVision, so Super35 lenses vignette in the clips. Scene Preview
    has a Super35 crop to compensate (Xelmus Aura post, https://lenses.cineflares.com/news).
- **Motion path, unverified:** the clips are motion-controlled and timecode-synced, which strongly
  suggests the light sweeps through and around the frame on a repeatable move. The exact path has not
  been published, and the clips were not viewed.
- **Scene Preview is the only simulated part, and it is a composite.** The optically captured flare
  plates are overlaid on clean, flare-free scene plates (desert, interior, night) in a "photometrically
  accurate" way. The lens's field of view is simulated, and so is its distortion where they have a
  distortion chart. They checked it against real anamorphic and vintage lenses shot in the desert. The
  site itself says bokeh, resolution and chromatic aberration cannot be judged in Scene Preview. It
  shows flare, contrast and colour only (News, "Lens SCENE PREVIEW", 2024-06-22,
  https://lenses.cineflares.com/news; the pricing page describes it as "Simulated with optically
  captured flares and scene plates").
- **No stated methodology beyond this.** There is no published lens model, no prescriptions and no
  parametric flare data. The PRO tier advertises "Advanced lens data" without saying what it contains
  (**unverified**).

## 3. Formats, downloads, API, pricing, licence

| Item | Finding | Source |
|---|---|---|
| Format | Streaming 1080p H.265 video in the web player. No stills or data files are offered. | FAQ |
| Downloads | None. Licensing high-resolution or raw files is possible by emailing contact@cineflares.com. | FAQ, "Can I download test videos?" |
| API | No public API. The app talks to a private backend (`api-v2.cineflares.com`) that needs a login. It was not probed. | app bundle, observed |
| Free tier | $0, basic library of 15 lenses, basic player | /pricing |
| PRO | $29.99 billed monthly, or yearly at $59.88 ($4.99/mo, labelled "early adopter"). Launch price was reported as $5/mo. Educational discount by email. | /pricing, CineD PRO article, FAQ |
| Terms | Personal, non-transferable, non-exclusive licence. Content may be used for education and research in personal and commercial work. It may **not** be sold, distributed, sublicensed or **incorporated into separate products**, and test files may not be redistributed, including on stock sites. The terms were last updated 2023-11-09, while the site was still in beta. | /termsofuse |

**Using it as data in DynamicLens:** the terms rule it out without a separate written licence. The
repo is public, and shipping flare plates, or anything fitted from them, would count
as "incorporating the contents in separate products". The route to that is a raw-file licence
through contact@cineflares.com, which would need explicit permission to redistribute. Even with
permission, the data would follow the same carve-out as the tiedtke and Andy Davis data: its own
terms, not this repo's licence (see `.claude/rules/licensing-and-credits.md`).

**Using it as visual reference** means a PRO subscriber watches the clips and hand-tunes our own
parametric model to match. This fits "educational and research purposes" and ships none of their
pixels. The tuned values would be our own work but inspired by their footage, so credit it in
`SOURCES.md` as a reference, the same way ideas are credited. This is the realistic path.

## 4. Lenses: which overlap with ours

The full lens list is behind the login. The table below comes from public News posts
(https://lenses.cineflares.com/news, eight pages, 2023-12 to 2026-09) plus a few secondary sources.
Our lenses come from `Tools/data/lens_catalogue.json`.

| Our preset(s) | On CINEFLARES? | Evidence |
|---|---|---|
| `DL_AD_Master` (ARRI/Zeiss Master Prime) | **Yes** | News 2024-08-10, "ARRI/Zeiss Master Primes" |
| `DL_AD_ARRI_Signature` | **Yes** (named as a comparison) | same Master Prime post |
| `DL_AD_Supreme`, `DL_AD_Zeiss_SupremeRadiance`, `DL_AD_Zeiss_CP3` | **Yes** | News 2026-09-13 lists CP.3, Supreme Primes, Supreme Prime Radiance, Supreme Zoom Radiance, Standard and Super Speeds |
| `DL_AD_Zeiss_CP2` | unverified | not in the news posts |
| `DL_AD_Cooke_S4i` | **Yes** | News 2025-07-27, "Cooke S4" (the S4s are in the *free* tier) |
| `DL_AD_Cooke_S7i` | unverified | only mentioned in passing |
| `DL_AD_Sigma_CineFF_Classic` | **Yes** | News 2024-05-13, "Sigma Classic Primes", compared with the Sigma Cine Primes |
| `DL_AD_Sigma_FF_HighSpeed` | probably (the "Sigma Cine Primes" in the same post), unverified | News 2024-05-13 |
| `DL_AD_Canon_K35` | **Yes** | News 2025-08-12, the K-35 zoom post, refers to the K-35 primes already in the library |
| `DL_AD_Canon_FD` | **Yes** | News 2024-04-06 "Canon FD", and a 2024-07-11 FD 35-105 zoom |
| `DL_AD_Leica_R` | **Yes** | News 2024-07-18 "Leica R" |
| `DL_AD_Leitz_Summilux-C`, `DL_AD_Leitz_Thalia` | **Yes** | News 2026-02-11 (Summilux-C and Summicron-C), 2025-11-14 (Thalia 65) |
| `DL_AD_Nikon_AI-S` | partial | vintage Nikkor 50 mm set from the Nikon Museum (2026-06-09); AI-S itself unverified |
| `DL_AD_Tokina_Vista` / `VistaOne` | partial | Tokina Vista-C (2025-09-23); Vista / Vista One unverified |
| `DL_AD_Canon_CN-E`, `Sumire`, `Rokinon_Xeen`, `Schneider_XenonFF`, `Camtech_FalconFF`, `Tribe7_Blackwing7` | unverified | not in the news posts |
| Ultra Prime (`DL_L_PoorThings_UltraPrime_10mm`) | unverified | not found |
| Petzval 58/85 | unverified | not found (a Helios 44, a Biotar derivative, is on the site) |
| Master Zoom, Angenieux Optimo 24-290 | unverified | only an Angenieux Type R2 18.5 mm is posted (2025-10-08) |
| Fisheyes (Nikkor 6/8 mm, OpTex 4 mm) | **No evidence** | Tegea 9.8 mm (rectilinear) and the Xelmus Aura 16 mm 150° anamorphic are the widest found |
| Anamorphics: `DL_T_Panavision_C/E/G_Series`, `DL_T_Panavision_Primo` | **Yes** | News: C Series 28 mm (2025-04-27), E Series (2026-01-18), G Series (2024-11-02), Primo Anamorphic (2025-04-17) |
| `DL_T_Kowa_CineProminar` | **Yes** | Kowa Anamorphic and Kowa Cine Prominar (2024-01-22, 2024-07-08) |
| `DL_T_Elite_MK` | **Yes** | JDC Optica Elite MKV (2025-07-01) |
| `DL_T_Todd_AO_HighSpeed` | partial | "Todd AO 35 vintage anamorphic" (2024-07-02); High Speed variant unverified |
| `DL_T_Atlas_Orion` | likely | an Atlas Orion 32 mm T2 clip is mentioned in launch press, per a search-result summary (unverified) |
| `DL_T_Lomo_RoundFront` | partial | Lomo *Square* Front (2024-08-01) and Lomo Foton-A are posted |
| `DL_T_Hawk_V-Lite_Vintage` | partial | Hawk Class-X and VantageOne (spherical T1) posted; V-Lite unverified |
| `DL_T_Cooke_FFi/SFi`, `Arri-Zeiss_MasterAnamorphic`, `Cineovision`, `Iscorama_Pre36`, `PS_Technik`, `Panavision_AutoPanatar/D_Series`, `AngenieuxOptimo_44-440` | unverified | not in the news posts |

The library also covers Panavision Ultra Panatar I/II, Ultra Vista, Primo 70, T Series and T Series
zooms, the Laowa Proteus Flex (swappable blue, amber, silver and clear anamorphic flare elements),
Laowa Nanomorph, Viltrox EPIC 1.33x, Neo-AO, Ancient Optics Statera, Lensworks Legacy 1.8x, Brevet
Supermatic, Xelmus Apollo and Aura, Caldwell Chameleon, Super Baltars, Cooke Speed and Panchro
Classic FF, Canon Rangefinder, Konica Hexanon, Minolta Rokkor, Fuji EBC, Helios 44, Kinoptik Tegea,
Pentax 6x7, Todd-AO 65, Leitz Hugo, Elsie, Hektor, Prime and zooms, Fujinon Premista 19-45, several
Tokina zooms, and Zhongyi Zone T1 (all from https://lenses.cineflares.com/news).

**Coverage summary:** the major spherical sets we ship are almost all there, including the Master
Prime, which is our baseline. Coverage of our anamorphics is solid, with the Panavision series, Kowa
and Elite. Fisheyes and the Petzval appear to be missing.

## 5. What differs per lens: the parameters a flare model needs

These are the traits the site itself calls out in its lens write-ups. All are from
https://lenses.cineflares.com/news unless another URL is given.

| Trait | Examples from the site | Model parameter it implies |
|---|---|---|
| **Ghost colour from coatings** | Kowa golden flares from the coating, with neutral glass; Fuji EBC purple, red and gold; Cooke S4 purple-red; Speed Panchro warm; Todd-AO 65 soft veiling glare | a colour (RGB tint, or a spectral curve) per ghost or per coating |
| **Ghost shape and edge** | K-35 zoom: large round spot flares with soft falloff, where modern zooms give sharp-edged discs; Fuji EBC 50 mm golden caustic pattern | ghost size, edge softness, ring or caustic structure, placement along the optical axis |
| **Veiling glare / contrast loss** | Todd-AO 65 veiling glare desaturates the whole frame; Speed Panchros lift the blacks; Sigma Classic softens the image with internal reflections even with no source in frame; Fujinon Premista coatings suppress it | a global low-frequency glare amount and tint, with contrast lift driven by total scene brightness, not only point sources |
| **Iris geometry and starburst** | the S4's eight-blade iris gives a starburst stopped down that vanishes wide open; the Premista has 13 blades and round bokeh; Panavision uses an oval iris | blade count, roundness, rotation, and a starburst amount that rises as the lens stops down |
| **T-stop dependence** | Hawk VantageOne rainbow flare wide open that changes with stop; Tegea flares tighten stopped down | flare and ghost parameters as a function of f-stop, which our component already drives |
| **Focal-length dependence** | Master Primes flare gently from 35 mm up; Kowa 25 mm unusually golden; Fujinon quieter at the wide end | per-focal-length variation, the same as our distortion profiles |
| **Anamorphic streak** | Laowa Proteus Flex in blue, amber, silver or clear; Ultra Panatar II defined streaks with a high-contrast coating that suppresses the spherical ghosts; Xelmus Aura classic streaks with contrast held | streak colour, length, sharpness, and the balance between streaks and spherical ghosts |
| **Overall strength** | Sigma Classic has among the strongest flares they have seen; Master Primes subtle and naturalistic | one master intensity, the most basic per-lens control |
| **Light source** | a 5600 K daylight point source at about infinity | calibrate our model to a white point source at infinity, so it compares directly with their clips |

The site also calls a lens's flare its "fingerprint". By its account, a flare shows dynamic contrast,
colour reproduction, flare pattern and iris geometry (FAQ, "Why is the focus primarily on flares?").

## 6. Related products and channels

- Instagram @cinelensflares, a YouTube channel and Vimeo user 160332965, all linked from the site
  footer. These show short preview clips.
- High-resolution or raw clips can be licensed by email (FAQ). No stock-footage flare pack, AE/Nuke
  plugin or UE product was found, and the terms specifically forbid putting their files on stock
  sites. Nothing like that seems to exist (**unverified**; searches found only press about the
  library).
- Markus Förderer's own site: https://markusforderer.com/.

## Implications for DynamicLens

1. **It gives us no data.** CINEFLARES is photographed reference footage, not a simulation. It has
   no prescriptions, no ghost parameters and no downloadable plates, and its terms forbid putting the
   content into another product. Nothing from it can ship in this public repo without a written
   licence from TrueLens Productions that explicitly allows redistribution. Even then, that data would
   sit outside this repo's licence in a `NOTICE` carve-out, like the tiedtke and Andy Davis data.
2. **It is an excellent matching target.** Their setup is a white point source at infinity, at every
   stop from wide open to T4, one camera, repeatable motion. That is easy to reproduce in UE: a small
   emissive light on black, the same focal length and T-stop, swept across the frame. One PRO
   subscription at $30 for a month would be enough for side-by-side tuning against about 20 of our
   presets, Master Prime, S4, Sigma Classic, K-35, the Panavision series, Kowa and Elite among them.
3. **The model needs these per-lens parameters** (from section 5): overall intensity, ghost count,
   positions along the axis, sizes, edge softness and per-ghost tint (coating colour), a global
   veiling-glare amount and tint, iris blade count, roundness and starburst strength against f-stop,
   and for anamorphics the streak colour, length and sharpness plus the streak-to-ghost balance. All
   of it should vary with focal length and f-stop, which the component already tracks. These belong
   as a `flare` block in `presets.json`, tuned by eye, with each value marked as "matched to
   reference", never "measured".
4. **Borrow Scene Preview's approach.** It composites captured flare over clean plates and matches the
   lens's FOV and distortion. That is essentially our post-process pass with procedural ghosts in
   place of captured plates. It confirms that screen-space ghosts plus veiling glare, placed through
   our existing distortion model, read correctly to cinematographers.
5. **The gaps in their catalogue** are fisheyes, the Petzval, Ultra Primes and several of our
   anamorphics (Master Anamorphic, Cooke SFi and FFi, Iscorama, Cineovision). Those presets will need
   another reference source or an honest "assumed" look.
6. **If Dylan wants real data later**, the one channel is contact@cineflares.com (raw-file
   licensing). Whatever we use needs a `SOURCES.md` credit, and a `NOTICE` entry if any of their
   data ships.
