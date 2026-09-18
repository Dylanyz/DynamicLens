# Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
# The lens maps this script reads belong to Andy Davis (Imagery for Media); see NOTICE and SOURCES.md.

"""Andy Davis's spherical creative lens maps -> half-res EXRs + a manifest for the editor importer.

Run this once, outside the editor, then run `dl.import_andy_stmaps()` inside it.

    python Tools/prep_andy_stmaps.py <folder of his zips> <output folder>

His maps are a free release from https://imag4media.com/vfx-rnd/ and are NOT redistributed with
this plugin, so both folders live outside the repo. Requires numpy and OpenEXR (`pip install
OpenEXR`).

Why half res: these are very smooth displacement fields. Box-downsampling 2x and sampling back
bilinearly costs a worst case of 0.0045 source pixels of displacement error on a 4448 px wide map
(mean 0.0014), measured across Canon K-35, Cooke S4i, Nikon AI-S, Tribe7 and Zeiss Supreme. It
quarters both the disk footprint and the RGBA16F video memory each map costs.

Only the `distort` maps are used, which is the direction the plugin's ST-map path expects and the
same one the tiedtke profiles already use.
"""
import collections
import json
import os
import re
import sys
import tempfile
import zipfile

import numpy as np
import OpenEXR

# Gate -> sensor mm. The two ARRI LF gates are the 8.25 um photosite pitch, and both are confirmed
# by Andy's own lens files already in Tools/data/raw (ZEISS_Supreme carries 36.7 x 25.54 and
# 31.68 x 17.82). 2048 x 1556 is a Super 35 full-aperture scan.
GATES = {(4448, 3096): (36.70, 25.54), (3840, 2160): (31.68, 17.82), (2048, 1556): (24.89, 18.66)}

# Sets we deliberately do not import.
SKIP = {
    # Already shipping as DLP_AD_ZEISS_Supreme: parametric, with a real focus stack, so it breathes.
    # A single-focus ST map would be a downgrade.
    "zeiss_supreme",
}


def box2(a):
    h, w, c = a.shape
    h2, w2 = h // 2 * 2, w // 2 * 2
    return a[:h2, :w2].reshape(h2 // 2, 2, w2 // 2, 2, c).mean((1, 3))


def prep(src_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    manifest, skipped = {}, []
    # his anamorphic sets are the same measurements we already ship as DL_T_*, so they are excluded
    # by name here rather than imported and then thrown away
    zips = sorted(z for z in os.listdir(src_dir) if z.endswith(".zip") and "_ana_" not in z)
    if not zips:
        raise SystemExit(f"no spherical zips found in {src_dir}")
    for zi, zn in enumerate(zips, 1):
        series = zn[:-4]
        if series in SKIP:
            skipped.append(series)
            print(f"[{zi}/{len(zips)}] SKIP {series} (already in the plugin)", flush=True)
            continue
        zf = zipfile.ZipFile(os.path.join(src_dir, zn))
        lenses = []
        for n in sorted(x for x in zf.namelist() if os.path.basename(x).startswith("distort")):
            b = os.path.basename(n)
            m = re.match(r"distort_(.+?)_(\d+)mm(?:_(T[\d.]+))?\.exr$", b)
            if not m:
                print(f"   ?? could not parse {b}", flush=True)
                continue
            focal, variant = int(m.group(2)), m.group(3) or ""
            with tempfile.NamedTemporaryFile(suffix=".exr", delete=False) as tf:
                tf.write(zf.read(n))
                tmp = tf.name
            try:
                with OpenEXR.File(tmp) as x:
                    px = x.channels()["RGB"].pixels
                h, w, _ = px.shape
                half = box2(px.astype(np.float32))
                out_name = f"{series}_{focal:03d}mm{('_' + variant) if variant else ''}.exr"
                OpenEXR.File(
                    {"compression": OpenEXR.ZIP_COMPRESSION, "type": OpenEXR.scanlineimage},
                    {"R": np.ascontiguousarray(half[..., 0], np.float16),
                     "G": np.ascontiguousarray(half[..., 1], np.float16),
                     "B": np.zeros(half.shape[:2], np.float16)},
                ).write(os.path.join(out_dir, out_name))
                lenses.append({"focal_mm": focal, "variant": variant, "file": out_name,
                               "src_dims": [w, h], "half_dims": [half.shape[1], half.shape[0]]})
                print(f"[{zi}/{len(zips)}] {series} {focal}mm {w}x{h} -> "
                      f"{half.shape[1]}x{half.shape[0]}", flush=True)
            finally:
                os.unlink(tmp)
        if not lenses:
            continue
        # majority gate: arri_signature mixes a 3840x2160 crop in with 4448x3096 open gate
        gate = collections.Counter(tuple(l["src_dims"]) for l in lenses).most_common(1)[0][0]
        manifest[series] = {"sensor_mm": list(GATES.get(gate, (36.70, 25.54))),
                            "sensor_assumed": gate not in GATES,
                            "gate_px": list(gate), "squeeze": 1.0, "lenses": lenses}
    # NOTE: needed_overscan is deliberately NOT computed here. It must come from the engine's own
    # sampler inside the editor (dl._andy_overscan), because computing it from the source EXR gives
    # subtly different values - 1.1159 against the correct 1.1250 on Atlas Orion 50 mm.
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump({"_doc": "Andy Davis creative lens maps (spherical), downsampled 2x. Generated by "
                           "Tools/prep_andy_stmaps.py. Consumed by dl.import_andy_stmaps().",
                   "skipped_already_in_plugin": skipped, "sets": manifest}, f, indent=1)
    n = sum(len(v["lenses"]) for v in manifest.values())
    print(f"\nDONE: {len(manifest)} sets, {n} lenses -> {out_dir}", flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    prep(sys.argv[1], sys.argv[2])
