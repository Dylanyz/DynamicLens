"""Experiment: fit polynomial distortion models to tiedtke's ST maps (Tools/data/raw/tiedtke_stmap_samples.json,
dumped from the editor with DynamicLensLibrary.ReadSTMapSamples).

Result on 2026-09-06: neither a radial Brown-Conrady (K1, K2) nor a separable x/y quartic fits the maps well
(10–35 px RMS residual at 1920 wide against 20–70 px raw displacement; coefficients drift wildly with focal
length). Either the maps carry decentering / non-polynomial structure, or the UV convention needs work
(pixel origin, aspect, the 46 x 18.66 mm desqueezed frame). A zoomable anamorphic profile needs the full
3DE4 anamorphic-standard-degree-4 model (CX02..CY44 + rotation + decentering) and a check of tiedtke's
convention against Epic's DistortionSTMapProcessor. Until then the maps are used as-is (prime lenses).

Run: python fit_stmaps.py   (needs numpy)
"""
import json, math, os, re
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = os.path.join(HERE, "data", "raw", "tiedtke_stmap_samples.json")


def load():
    d = json.load(open(SAMPLES))
    return d["grid"], d["sensor_mm"], d["maps"]


def pairs(uv, n, W, H, F):
    """(undistorted, distorted) normalized coordinates in Unreal's convention: x = (u-0.5)/fx, fx = F/W."""
    fx, fy = F / W, F / H
    xu, yu, xd, yd = [], [], [], []
    for j in range(n):
        v = (j + 0.5) / n
        for i in range(n):
            u = (i + 0.5) / n
            su, sv = uv[2 * (j * n + i)], 1.0 - uv[2 * (j * n + i) + 1]   # bottom-left origin
            xd.append((u - 0.5) / fx); yd.append((v - 0.5) / fy)
            xu.append((su - 0.5) / fx); yu.append((sv - 0.5) / fy)
    return (np.array(xu), np.array(yu), np.array(xd), np.array(yd), fx, fy)


def fit_radial(xu, yu, xd, yd):
    ru, rd = np.hypot(xu, yu), np.hypot(xd, yd)
    m = ru > 1e-4
    A = np.stack([ru[m] ** 2, ru[m] ** 4], 1)
    k, *_ = np.linalg.lstsq(A, rd[m] / ru[m] - 1, rcond=None)
    pred = ru * (1 + k[0] * ru ** 2 + k[1] * ru ** 4)
    s = np.where(m, pred / np.where(m, ru, 1), 1)
    return k, xu * s - xd, yu * s - yd


def fit_xy(xu, yu, xd, yd):
    ru = np.hypot(xu, yu)
    kx, *_ = np.linalg.lstsq(np.stack([xu * ru ** 2, xu * ru ** 4], 1), xd - xu, rcond=None)
    ky, *_ = np.linalg.lstsq(np.stack([yu * ru ** 2, yu * ru ** 4], 1), yd - yu, rcond=None)
    return kx, ky, xu * (1 + kx[0] * ru ** 2 + kx[1] * ru ** 4) - xd, yu * (1 + ky[0] * ru ** 2 + ky[1] * ru ** 4) - yd


def main():
    n, (W, H), maps = load()
    print(f"{'map':40s} {'F':>5s} {'K1':>8s} {'K2':>9s} {'rmsRad':>7s} {'rmsXY':>7s} {'raw':>6s}  (px @1920)")
    for name, uv in sorted(maps.items()):
        F = float(re.search(r"_(\d+)mm", name).group(1))
        xu, yu, xd, yd, fx, fy = pairs(uv, n, W, H, F)
        k, ex, ey = fit_radial(xu, yu, xd, yd)
        kx, ky, ex2, ey2 = fit_xy(xu, yu, xd, yd)
        px = lambda ex, ey: float(np.sqrt(np.mean((ex * fx * 1920) ** 2 + (ey * fy * 858) ** 2)))
        print(f"{name:40s} {F:5.0f} {k[0]:8.3f} {k[1]:9.2f} {px(ex, ey):7.2f} {px(ex2, ey2):7.2f} {px(xu - xd, yu - yd):6.1f}")


if __name__ == "__main__":
    main()
