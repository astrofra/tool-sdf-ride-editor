# Island-Aware AO Denoise Future Plan

## Status

Recorded on **August 7, 2026** as a deferred improvement.
First implementation milestone applied on **August 7, 2026**.

This note now describes the remaining follow-up work around the first chart-aware AO denoiser implemented in this repository.

The current implementation already does the following:

- preserves `chart_id` on unwrapped triangles from `xatlas`;
- rasterizes a `chart_id` per baked texel;
- runs an optional chart-aware AO denoise pass before dilation;
- uses surface-normal similarity as an extra cross-bilateral weight.

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

The first implementation already preserves the minimum required data:

1. a `chart_id` per unwrapped triangle;
2. a rasterized `chart_id` per baked texel;
3. a texel normal buffer for edge-aware weights.

---

## Required Unwrap Metadata

`xatlas` exposes chart membership in its output mesh.

The current implementation preserves that information directly on local output triangles through a UV chart identifier.

That choice is now the active local baseline.

---

## Required Bake Outputs

The AO bake raster stage now produces the additional in-memory buffers the denoiser needs:

- `valid_mask`
- `chart_id_map`
- `normal_map`

These still do not need to be written to disk by default.

---

## Recommended First Filter

The first denoiser is now a **chart-aware grayscale filter**.

Recommended constraints:

- only accumulate neighbors with the same `chart_id`;
- only accumulate neighbors that are valid baked texels;
- optionally reduce weight when shading normals diverge too much.

This can start as a small-radius iterative filter such as:

- bilateral;
- cross-bilateral;
- edge-aware box or Gaussian.

The implemented first version is a small-radius **cross-bilateral** filter because it is simple enough to implement locally and respects both island identity and local surface orientation.

---

## Filtering Rules

The current first-pass rules are:

1. reject neighbors outside the atlas image;
2. reject neighbors whose texels are invalid;
3. reject neighbors with a different `chart_id`;
4. compute a spatial weight from texel distance;
5. optionally compute a normal-consistency weight;
6. normalize and write the filtered grayscale AO.

This already avoids the main failure mode of naive atlas-space blurs.

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

is what made the first denoiser stage easy to insert cleanly.

The denoise pass now sits cleanly after AO evaluation and before padding / final image write.

This is a useful architectural side effect:

- denoising becomes a distinct stage;
- island metadata can be consumed there;
- CPU parallelism can later be applied there too.

---

## Remaining Implementation Steps

The next implementation order should be:

1. tune the default filter strength on larger production bakes;
2. expose more edge-aware controls only if practical iteration actually needs them;
3. evaluate per-chart or per-island denoise scheduling if CPU time becomes noticeable;
4. add chart debug outputs if denoise behavior becomes harder to reason about.

This keeps the next work incremental and testable.

---

## Non-Goals For First Version

The current first island-aware denoiser still does **not** attempt:

- texture synthesis;
- hole filling for invalid texels;
- seam stitching across different charts;
- temporal accumulation;
- machine-learning denoisers.

The first goal is simply:

reduce AO noise without leaking shading across disjoint UV islands.
