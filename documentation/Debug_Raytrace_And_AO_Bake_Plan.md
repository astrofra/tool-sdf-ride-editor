# Debug Raytrace And AO Bake Plan

## Status

Planned and first milestone implemented on **August 6, 2026**.

This note records the current design and technical decisions for introducing:

- a debug-oriented mesh raytracer;
- image output for debugging;
- a future ambient occlusion bake pipeline.

The immediate implementation target is the **debug raytracer path**, not the full UV-space baker.

### Implemented In The First Milestone

The following pieces are now implemented in this repository:

- a local mesh BVH and triangle ray query path;
- local image writing through a vendored `stb_image_write.h`;
- fixed-camera orthographic debug rendering;
- `depth`, `normal`, and `ao` debug render modes;
- CLI options for generating debug images from generated meshes.

### Still Deferred

The following remain intentionally deferred:

- UV unwrap driven AO baking;
- texel-to-surface rasterization;
- bake padding/dilation;
- packed AO/curvature/cavity bake textures.

---

## Primary Goal

The near-term goal is to make it easy to validate mesh-space ray queries on generated SDF meshes.

This should let the project:

- inspect generated geometry from fixed cameras;
- debug dense-mesh intersection behavior;
- validate acceleration structure correctness;
- validate normals and self-intersection bias;
- preview object-space or screen-space AO behavior before UV baking exists.

---

## Architectural Decision

The system is intentionally split into two layers:

1. a **debug raytracer** for fixed-camera inspection and AO experimentation;
2. a later **AO baker** that reuses the same ray query core for UV-space output.

This split is intentional.

The debug renderer and the UV-space baker solve related but different problems:

- the debug renderer validates intersection and sampling behavior visually;
- the UV-space baker converts those ray queries into texture-space data.

The first one should exist before the second one.

---

## Reuse Strategy

There is already a useful raytracing implementation in:

- `C:\works\projects\game-liminal-raytraced-llm-world\src`

There is also a vendorized image writer in:

- `C:\works\projects\game-liminal-raytraced-llm-world\vendor\stb`

### Decision

Reuse the **algorithms and structure**, but do **not** create a build-time dependency on that repository.

Instead:

- copy only the minimum useful code into this repository;
- keep the new code independent from the liminal project runtime;
- keep the API specific to this SDF tool.

### Why

This avoids:

- cross-project coupling;
- accidental drift in external behavior;
- pulling unrelated scene/material/runtime code into this tool;
- turning the generator into a consumer of another evolving executable project.

---

## What To Reuse

The useful parts of the liminal project are:

- triangle ray intersection;
- AABB ray rejection;
- BVH node structure and recursive build approach;
- occlusion queries;
- cosine hemisphere sampling;
- `stb_image_write.h`.

The parts that should **not** be imported directly are:

- the full liminal scene format;
- materials and emissive-light logic;
- camera spotlight logic;
- sky/background logic;
- SDL/frontend code;
- full path tracing behavior.

---

## New Local Modules

The implementation should be organized into local modules under `sdf_core`.

### `raytrace`

Purpose:

- build a ray-query scene from a generated mesh;
- accelerate triangle intersection with a BVH;
- answer closest-hit and occlusion queries.

Expected responsibilities:

- `Ray`
- `RayHit`
- `RayTriangle`
- `RayBvhNode`
- `RayScene`
- `build_ray_scene(const Mesh&)`
- `intersect_ray(...)`
- `is_occluded(...)`

### `image_write`

Purpose:

- write debug images to disk;
- keep image output separate from ray logic.

Expected responsibilities:

- wrap local use of `stb_image_write.h`;
- expose a minimal PNG writer for RGB8 images.

### `debug_render`

Purpose:

- render generated meshes from fixed cameras for debugging;
- visualize intersection and AO behavior before texture baking exists.

Expected first render modes:

- `depth`
- `normal`
- `ao`

---

## Camera Decision

The first debug camera should be **fixed and orthographic**.

This is preferred initially over a perspective camera because:

- it is easier to reason about;
- it makes geometry clipping and projection errors more obvious;
- it is better for repeatable debug comparisons;
- it is enough for early AO validation.

Additional presets should still exist so the scene can be inspected from:

- front
- left three-quarter
- right three-quarter

---

## Why Not Start With UV-Space Baking

UV-space AO baking is not blocked by ray queries.

It is blocked mainly by:

- stable UV unwrap;
- texel-to-surface rasterization;
- seam handling;
- padding/dilation rules;
- texture layout decisions.

Because of that, the ray query core should be proven first in simpler contexts:

- image-space debug rendering;
- optionally vertex AO;
- optionally object-space AO output.

Only then should the project move to full baked textures.

---

## Immediate Milestone

The first useful milestone is:

1. local BVH and ray query core;
2. local image output;
3. fixed-camera debug render for depth;
4. fixed-camera debug render for normals;
5. fixed-camera debug render for AO.

This milestone is successful when the tool can:

- build the demo SDF scene;
- generate its polygonal mesh;
- render debug images to disk from that mesh;
- produce visually plausible AO without requiring UV unwrap.

---

## CLI Direction

The command-line interface should remain simple and coarse-grained.

Planned options include:

- `--debug-render PATH`
- `--debug-mode depth|normal|ao`
- `--render-width N`
- `--render-height N`
- `--camera-front`
- `--camera-left-3q`
- `--camera-right-3q`
- `--ao-samples N`
- `--ao-max-distance F`

These are intended only for debugging and validation, not as a final authoring UI.

---

## AO Strategy For The Debug Renderer

The AO debug renderer should:

1. cast one primary ray per pixel;
2. find the first surface hit;
3. sample a cosine-weighted hemisphere around the hit normal;
4. cast short occlusion rays;
5. output grayscale AO.

Important implementation details:

- a small normal-offset bias is required to avoid self-intersection;
- AO radius should be configurable;
- AO sample count should be configurable;
- deterministic RNG seeding is preferable for reproducibility.

---

## Deferred AO Baker

The full AO baker should be implemented later as a separate module, reusing the same ray query core.

Expected future module:

- `bake_ao`

Expected future responsibilities:

- rasterize UV charts;
- map texels to triangles;
- reconstruct world-space positions and normals per texel;
- cast AO rays;
- write grayscale or packed bake textures;
- perform padding/dilation around valid texels.

This later phase should remain out of scope until UV generation is stable enough.

---

## Decision Summary

The current design decisions are:

- use a **local** ray query implementation in this repository;
- reuse the liminal project only as a source of algorithms, not as a dependency;
- vendor `stb_image_write.h` locally;
- build the **debug renderer first**;
- defer UV-space AO baking until unwrap exists;
- keep the first camera setup fixed, deterministic, and orthographic.

### Performance Note

A first **OpenMP** acceleration pass was implemented on **August 7, 2026**.

The current implementation now parallelizes:

- the debug raytracer image loop;
- UV-space AO bake texel evaluation;
- UV-space AO bake padding / fill passes.

The main heavy loop that still remains intentionally serial is:

- UV triangle rasterization into the bake atlas.

This is deliberate.

That raster stage still has texel ownership conflicts and is not a safe drop-in parallel loop without a more explicit tile, bucket, or chart-partition strategy.

### Related Notes

For the follow-up design details, see:

- `documentation/OpenMP_Acceleration_Study.md`
- `documentation/Island_Aware_AO_Denoise_Future_Plan.md`

### Current Demo Baseline

The current debug AO demo baseline is:

- output resolution `640x360`;
- `16` AO samples per pixel;
- fixed front orthographic camera.

This is a deliberate balance for now:

- high enough to make silhouette and AO behavior easier to inspect;
- still cheap enough for quick iteration during mesh-generation work.
