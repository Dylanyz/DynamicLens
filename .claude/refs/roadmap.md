# Roadmap — work that is ready but deliberately waiting

Things that have been investigated, are worth doing, and are **not** being done yet, each with
what it is gated on. This is not a wish list: an entry only belongs here once the research is
done and someone could pick it up and build it.

**How to use it.** When the gate on an entry has cleared, *propose it to Dylan* — do not just
start. When you finish an entry, delete it from this file rather than marking it done; git history
is the record.

---

## Anamorphic parametric distortion (3DE4 Anamorphic Standard Degree 4)

**Gated on:** the preset browser landing. Both touch `Source/DynamicLens`, and doing them at once
means two agents fighting over the same C++ and two rebuild/restart cycles. Once the browser is
merged and building, propose this.

**What it unlocks.** Six Cooke FFi ANA 1.8x lenses (32, 40, 50, 75, 100, 135 mm) from Andy Davis's
Cinelens release, already extracted to `Tools/data/raw/andy_davis_cinelens.json`. They would be the
first anamorphic the plugin can **zoom continuously** instead of snapping between measured primes.

**Be honest about the size of the prize.** These same lenses already ship as `DL_T_Cooke_FFi`
tiedtke ST maps, which are exact at their measured focals. The gain is free focal length between
them, nothing else. They are all single-focus, so they do **not** breathe, and the 85 mm macro's
coefficients are all zero. Six lenses, one new capability. Do not oversell it, and do not let it
grow into "import the whole Cinelens release" - the rest of that release has no distortion data at
all (see `SOURCES.md`).

**What the work is:**

1. A params struct mirroring UE's `FAnamorphicDistortionParameters` - 14 floats: `PixelAspect`,
   `CX02 CX04 CX22 CX24 CX44`, `CY02 CY04 CY22 CY24 CY44`, `SqueezeX`, `SqueezeY`, `LensRotation`.
   The order in `Tools/data/raw/andy_davis_cinelens.json` is exactly that.
2. A model selector on `UDynamicLensProfile`. `FDynamicLensParams` is Brown-Conrady only and the
   parametric path hardcodes `USphericalLensModel::StaticClass()` in two places in
   `DynamicLensComponent.cpp`. Both need to follow the profile's model.
3. An importer path in `dynamiclens_tools.py`, and a `presets.json` section for the editorial
   layer (label, note, physical specs), following how `andy_stmap_sets` is laid out.
4. `dl.export_catalogue()` needs `_parametric_edge_shift` to handle the anamorphic model - its
   K1+K2+K3 assumption is meaningless for these coefficients.

**Watch out for:** the 50 mm's solve is an outlier against its neighbours (CX22 0.88 and CY24 3.64,
where 40 mm and 75 mm are around 0.1-0.3). Interpolating focal length straight through it will
lurch at 50 mm. Check it against the tiedtke ST map for the same lens before shipping, and if it is
genuinely bad, say so in the profile's `Source` rather than quietly smoothing it.

**Needs a build and therefore a restart.** Ask Dylan; see `.claude/rules/editor-restarts.md`.

---

## The DL_L_* fisheye rework

**Gated on:** two things, in order. (1) The wide-field-source research launched 2026-09-19 - whether
UE can supply more than ~81 deg off-axis per frame (panoramic MRG pass, SceneCaptureCube, fulldome
techniques, path-traced camera rays). That answer changes the whole design, because the current plan
is a workaround for a limit that may not be real. (2) Dylan's choice between fitting the projection
to the circle or keeping the physically stated field - explained under "the choice" below.

**Why.** Measured 2026-09-19: every projection preset shows 65-81 deg of field and 1.25-1.48x
angular compression regardless of focal length, so a 4 mm 180 deg fisheye and a 10 mm look the same.
The full diagnosis, with numbers, is in `.claude/refs/overscan-and-image-circle.md`.

**The choice.** Unreal can source only ~81 deg off-axis from one rectilinear render at the 2.0
overscan ceiling. Either (a) put those 81 deg where the real lens would put them, which leaves the
picture stopping well short of the image circle - today's behaviour, porthole 23% small and
elliptical - or (b) fit the projection so 81 deg fills the circle at its real physical size, which
makes the circle correct and round at the cost of bending harder than the real lens does. Recommended
(b): the thing being matched is a film frame, not a test chart. Moot if the research finds a real
>90 deg source.

**The work, once unblocked:**

1. **A one-parameter projection family** on `UDynamicLensProfile`, replacing the four-way
   `EDynamicLensProjection` enum with a continuous `k`: `r = (f/k)tan(k*theta)` for k>0,
   `r = f*theta` at k=0, `r = (f/|k|)sin(|k|*theta)` for k<0. k=1 rectilinear, 0.5 stereographic,
   0 equidistant, -0.5 equisolid, -1 orthographic. Keep the enum as a preset-authoring convenience
   that writes k. This is what makes a tunable "how fisheye" possible at all.
2. **Make `Distortion > Amount` reach `DriveProjection`.** It currently only multiplies
   Brown-Conrady coefficients (`FDynamicLensSettings::Evaluate`), so on every `DL_L_*` ultra-wide
   the Amount slider, the multiplier and the override block all do nothing. Blend k from 1
   (rectilinear) toward the profile's k, and allow past it for exaggeration.
3. **Image Circle > Coverage**, circle diameter over frame diagonal: 0.3 a tight porthole, 1.0
   exactly kissing the corners, above ~1.2 invisible. Replaces `Scale`, which currently appears to
   do nothing on these presets because it only moves the loser of the two masks.
4. **Report which mask is active** in the details panel. Today you cannot tell whether you are
   looking at the lens's image circle or the data limit, which is why this took a session to find.
5. **`DL_L_Favourite_10mm_Rect`**: fixed overscan 1.5, needs 1.62, so its frame edges are sourced
   from outside the render and smear. Independent of everything above - just raise it.

**Do not touch `DL_L_PoorThings_Petzval_58` or `_85`.** Dylan likes the swirl. They are parametric,
not projection, so nothing here reaches them - keep it that way.

**Change the presets in place, not as _v2 variants** (Dylan, 2026-09-19). Restore points for the
revert: DynamicLens `aa13e04`, CitySample Diversion `dv.commit.48`.
