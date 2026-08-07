# Island-Aware AO Denoise Future Plan

## Status

Recorded on **August 7, 2026** as a deferred improvement.

This note describes a future post-process denoiser for baked ambient occlusion that respects UV island boundaries instead of filtering across unrelated parts of the mesh.

---

## Problem Statement

The current AO baker can still produce visibly noisy results even when adaptive ray sampling is increased.

A naive image-space denoiser is risky because:

- distinct UV islands can be adjacent in atlas space while being unrelated on the surface;
- blur across those boundaries leaks tone between disconnected parts of the mesh;
- the result can look softer but less correct.

This is especially relevant in the current project because the unwrap is still relatively fragmented.

---

## High-Level Goal

The future denoiser should smooth Monte Carlo noise **within** UV islands while refusing to mix data **across** unrelated islands.

The intended effect is:

- lower visible AO speckle;
- preserved seams between disjoint charts;
- more predictable denoising on heavily packed atlases.

---

## Required Data

The current baker already has:

- AO values per valid texel;
- a validity mask;
- reconstructed shading normals per texel during sampling;
- UV packing produced by `xatlas`.

What it does **not** currently preserve is the chart identity of each texel.

The future denoiser therefore needs at least:

1. a `chart_id` per unwrapped triangle;
2. a rasterized `chart_id` per baked texel;
3. optionally a texel normal buffer for edge-aware weights.

---

## Required Unwrap Metadata

`xatlas` exposes chart membership in its output mesh.

The unwrap stage should later preserve that information explicitly in local data.

There are two practical ways to do that:

1. extend local mesh triangle metadata with a UV chart identifier;
2. keep a sidecar unwrap result structure that stores chart id per output triangle.

Either is acceptable, but the second option is less invasive if the base mesh format should remain generic.

---

## Required Bake Outputs

The AO bake raster stage should later produce additional buffers:

- `valid_mask`
- `chart_id_map`
- optionally `normal_map`

These do not need to be written to disk by default, but they should exist in memory so the denoiser can operate on them.

---

## Recommended First Filter

The first denoiser should be a **chart-aware grayscale filter**.

Recommended constraints:

- only accumulate neighbors with the same `chart_id`;
- only accumulate neighbors that are valid baked texels;
- optionally reduce weight when shading normals diverge too much.

This can start as a small-radius iterative filter such as:

- bilateral;
- cross-bilateral;
- edge-aware box or Gaussian.

The recommended first version is a **cross-bilateral** filter because it is simple enough to implement locally and respects both island identity and local surface orientation.

---

## Filtering Rules

The future first-pass rules should be:

1. reject neighbors outside the atlas image;
2. reject neighbors whose texels are invalid;
3. reject neighbors with a different `chart_id`;
4. compute a spatial weight from texel distance;
5. optionally compute a normal-consistency weight;
6. normalize and write the filtered grayscale AO.

This avoids the main failure mode of naive atlas-space blurs.

---

## Why Island-Aware Matters Here

This repository currently has:

- many UV islands;
- tight chart packing;
- relatively low bake resolution during practical iteration.

Those three factors make naive denoising particularly unsafe.

An island-aware filter is therefore not a luxury refinement.
It is the minimum denoiser shape that fits the current atlas characteristics.

---

## Relationship To OpenMP Refactor

The recent OpenMP-friendly split of the AO baker into:

- UV rasterization;
- AO evaluation per valid texel;
- post-process padding;

is also the right foundation for a later denoiser stage.

A future denoise pass can be inserted cleanly after AO evaluation and before final image write.

This is a useful architectural side effect:

- denoising becomes a distinct stage;
- island metadata can be consumed there;
- CPU parallelism can later be applied there too.

---

## Deferred Implementation Steps

The future implementation order should be:

1. preserve chart id information from `xatlas` output;
2. rasterize a `chart_id_map` during AO bake;
3. add a chart-aware denoise pass on the AO buffer;
4. make the denoiser optional from the CLI;
5. expose radius and strength only after the first working version exists.

This keeps the work incremental and testable.

---

## Non-Goals For First Version

The first island-aware denoiser should **not** attempt:

- texture synthesis;
- hole filling for invalid texels;
- seam stitching across different charts;
- temporal accumulation;
- machine-learning denoisers.

The first goal is simply:

reduce AO noise without leaking shading across disjoint UV islands.
