#!/usr/bin/env python3
"""
Generate sky texture assets for Martian Melodies.

Outputs (relative to this script):
  ../../assets/maps/sky_noise.png   -- greyscale fractal noise for atmospheric variation
  ../../assets/maps/sky_stars.png   -- greyscale equirectangular starfield

Usage: python gen_sky_textures.py
"""

import os
import numpy as np
from PIL import Image

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
ASSETS_MAPS = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "assets", "maps"))


# ------------------------------------------------------------------ #
# Value noise — tileable (modular grid indexing)
#
# The grid is periodic: gw x gh cells map exactly onto width x height
# pixels. Indices wrap with %, so the right edge connects seamlessly
# to the left edge and the texture can be scrolled without seams.
# ------------------------------------------------------------------ #
def value_noise(width, height, cell_size, seed=0):
    cell_size = max(cell_size, 2)
    gw = max(width  // cell_size, 2)
    gh = max(height // cell_size, 2)
    rng  = np.random.default_rng(seed)
    grid = rng.random((gh, gw)).astype(np.float32)

    # linspace endpoint=False: coords span [0, gw) so the texture wraps perfectly
    xs = np.linspace(0, gw, width,  endpoint=False)
    ys = np.linspace(0, gh, height, endpoint=False)
    gx, gy = np.meshgrid(xs, ys)   # (height, width)

    xi = np.floor(gx).astype(np.int32)
    yi = np.floor(gy).astype(np.int32)
    fx = gx - xi
    fy = gy - yi

    # Smoothstep
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)

    # Modular wrap — guarantees seamless tiling in both axes
    xi0 = xi       % gw
    xi1 = (xi + 1) % gw
    yi0 = yi       % gh
    yi1 = (yi + 1) % gh

    v00 = grid[yi0, xi0]
    v10 = grid[yi0, xi1]
    v01 = grid[yi1, xi0]
    v11 = grid[yi1, xi1]

    return (v00 * (1 - fx) + v10 * fx) * (1 - fy) + \
           (v01 * (1 - fx) + v11 * fx) * fy


# ------------------------------------------------------------------ #
# Fractal Brownian Motion — Terrain variant (SideFX "valley-dampened")
#
# Large-scale octaves use standard fBm (smooth gradients for density).
# Fine-scale octaves use billow folding: v = 1 - |2v - 1|
# which flattens the valleys and sharpens the peaks — giving cloud-like
# bright tendrils on a dark background, matching the Terrain fractal
# type described in SideFX Sky Field Noise documentation.
# ------------------------------------------------------------------ #
def fbm_terrain(width, height, octaves=8, base_cell=160, gain=0.52,
                billow_start=3, seed=42):
    result = np.zeros((height, width), dtype=np.float32)
    amp    = 1.0
    total  = 0.0
    cell   = base_cell
    for i in range(octaves):
        layer = value_noise(width, height, max(cell, 2), seed=seed + i * 17)
        if i >= billow_start:
            # Billow fold: peaks stay bright, valleys collapse to dark
            layer = 1.0 - np.abs(2.0 * layer - 1.0)
        result += layer * amp
        total  += amp
        amp    *= gain
        cell   //= 2
    return result / total


# ------------------------------------------------------------------ #
# Sky noise texture (atmospheric variation — terrain fractal)
# ------------------------------------------------------------------ #
def gen_sky_noise(width=512, height=512):
    noise = fbm_terrain(width, height, octaves=8, base_cell=160,
                        gain=0.52, billow_start=3, seed=1337)
    # Keep the full dynamic range: the domain warp in the shader
    # applies its own gain/contrast correction at runtime.
    return (np.clip(noise, 0, 1) * 255).astype(np.uint8)


# ------------------------------------------------------------------ #
# Starfield texture (equirectangular, sphere-uniform distribution)
# ------------------------------------------------------------------ #
def gen_starfield(width=2048, height=1024, n_stars=2800, seed=9001):
    rng = np.random.default_rng(seed)
    img = np.zeros((height, width), dtype=np.float32)

    # Power-law brightness distribution: many dim stars, few bright ones
    brightness = rng.uniform(0.0, 1.0, n_stars) ** 2.5

    # Uniform distribution on sphere -> equirectangular projection
    # elevation: arcsin of uniform [-1, 1]  ->  [-π/2, π/2]
    # azimuth:   uniform [0, 2π)            ->  [-π, π]  after shift
    elev = np.arcsin(rng.uniform(-1.0, 1.0, n_stars))
    azim = rng.uniform(0.0, 2.0 * np.pi,   n_stars)

    px = ((azim / (2 * np.pi))         ) * width
    py = ((elev / np.pi) + 0.5         ) * height
    px = px.astype(int) % width
    py = py.astype(int).clip(0, height - 1)

    for i in range(n_stars):
        b = float(brightness[i])
        x, y = int(px[i]), int(py[i])

        if b < 0.18:
            # Single-pixel faint star
            img[y, x] = min(1.0, img[y, x] + b)
        else:
            # Gaussian blob — radius 1 for medium stars, 2 for bright ones
            radius = 1 if b < 0.65 else 2
            sigma2 = 1.2 + (radius - 1) * 0.6
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    nx = (x + dx) % width
                    ny = (y + dy)
                    if 0 <= ny < height:
                        d2 = dx * dx + dy * dy
                        img[ny, nx] = min(1.0, img[ny, nx] + b * np.exp(-d2 / (2 * sigma2)))

    return (np.clip(img, 0, 1) * 255).astype(np.uint8)


# ------------------------------------------------------------------ #
# Entry point
# ------------------------------------------------------------------ #
def main():
    os.makedirs(ASSETS_MAPS, exist_ok=True)

    print("Generating sky_noise.png  (512x512, greyscale fBm)...")
    Image.fromarray(gen_sky_noise(1024, 1024), mode='L').save(
        os.path.join(ASSETS_MAPS, "sky_noise.png"))
    print(f"  -> {os.path.join(ASSETS_MAPS, 'sky_noise.png')}")

    print("Generating sky_stars.png  (1024x512, equirectangular starfield)...")
    Image.fromarray(gen_starfield(4096, 4096, n_stars=2800), mode='L').save(
        os.path.join(ASSETS_MAPS, "sky_stars.png"))
    print(f"  -> {os.path.join(ASSETS_MAPS, 'sky_stars.png')}")

    print("Done.")


if __name__ == "__main__":
    main()
