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
