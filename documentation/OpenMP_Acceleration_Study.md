# OpenMP Acceleration Study

## Status

Recorded and first implementation applied on **August 7, 2026**.

This note captures where `OpenMP` is a good fit in the current CPU bake and debug-render pipeline, and where it is not yet a safe drop-in acceleration.

---

## Motivation

Recent AO bake settings showed that higher atlas resolutions and stricter adaptive error thresholds quickly make CPU render times unacceptable.

The project therefore needs a practical CPU-side acceleration path before more aggressive bake quality presets become usable.

---

## Unsafe Parallelization Point

The most obvious heavy loop in the baker is the UV triangle raster stage.

However, this loop is not a safe first `OpenMP` target because:

- many triangles may touch the same texel;
- texel ownership is resolved by comparing coverage scores;
- parallel writes would race unless the raster stage is redesigned around tiles, buckets, or thread-local reductions.

For that reason, the UV raster stage is currently kept sequential.

---

## Chosen Strategy

Instead of parallelizing the conflicting raster stage directly, the baker is split conceptually into separate phases:

1. UV rasterization into per-texel surface samples;
2. AO evaluation per valid texel;
3. post-process padding / fill;
4. final image write.

This is the right shape for `OpenMP` because phases 2 and 3 are naturally data-parallel.

---

## Current OpenMP Targets

The first implementation targets:

- debug renderer scanline loop;
- AO bake texel evaluation loop;
- AO bake padding / fill passes.

These loops are good candidates because each worker:

- reads shared immutable scene data;
- writes either unique pixels/texels or writes into separate next-pass buffers;
- does not require complicated synchronization.

---

## Why Not Triangle Buckets Yet

Triangle buckets or atlas tiles are a valid future direction, but they are a more invasive change.

They would require:

- spatial binning of UV triangles;
- tile ownership rules;
- conflict resolution at tile boundaries;
- likely a different raster data layout.

That is a useful future optimization, but not the fastest safe path for the current codebase.

---

## Current Build Integration

`OpenMP` is enabled through CMake only when available.

The current build logic:

- keeps the feature optional;
- links `OpenMP::OpenMP_CXX` when found;
- otherwise falls back to the serial implementation automatically.

This keeps the repository portable while still accelerating supported local toolchains.

---

## Expected Effect

The current OpenMP pass is expected to help most when:

- bake resolution is high;
- AO adaptive sampling spends many rays per valid texel;
- padding passes cover many atlas texels;
- debug renders are large enough to amortize thread launch overhead.

It is expected to help less when:

- the bake is dominated by unwrap time;
- the bake is dominated by sequential UV rasterization;
- the atlas is tiny;
- adaptive sampling exits very early almost everywhere.

---

## Current Limits

The current OpenMP integration does **not** yet accelerate:

- UV triangle rasterization itself;
- mesh generation;
- BVH construction;
- xatlas unwrap generation.

Those remain separate future performance topics.

---

## Next Performance Ideas

If further speed is needed later, the next likely steps are:

1. tile- or bucket-based UV raster parallelization;
2. chart-level work partitioning;
3. optional denoise parallelization;
4. broader scene-generation parallelism where safe.

For now, the implemented OpenMP pass should be treated as the first practical CPU acceleration layer, not the final performance architecture.
