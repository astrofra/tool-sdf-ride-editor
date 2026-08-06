# AO Bake First Milestone

## Status

Planned and first milestone implemented on **August 6, 2026**.

This note records the current design and technical decisions for the first UV-space ambient occlusion baker in this repository.

---

## Preconditions

The AO baker is built on top of two pieces that already exist locally:

- the mesh ray query core and BVH traversal;
- the `xatlas`-based UV unwrap step.

This is the intended order of work.

The baker should not have been started before:

- mesh-space ray intersection was visually validated;
- the project could generate a bakeable UV layout.

---

## First Milestone Scope

The first AO bake milestone is intentionally narrow.

It should:

1. unwrap the generated mesh to a single atlas;
2. rasterize UV triangles into image space;
3. reconstruct world-space position and normal per covered texel;
4. cast hemisphere AO rays using the existing ray query core;
5. write a grayscale PNG bake.

This milestone does **not** attempt to solve every quality issue yet.

---

## Architecture Decision

The AO baker is a separate local module:

- `src/sdf_core/include/sdf/bake_ao.h`
- `src/sdf_core/src/bake_ao.cpp`

This module consumes:

- a UV-unwrapped mesh;
- a ray scene for occlusion queries;
- bake settings;
- an image output target.

This separation is intentional.

The unwrap stage owns atlas generation.
The baker owns texel coverage, AO sampling, and image output.

---

## UV Rasterization Decision

The first baker uses a straightforward UV-space triangle rasterizer:

- one texel-center sample per pixel;
- barycentric interpolation in UV space;
- barycentric reconstruction of world position and shading normal.

This is deliberately simple.

Deferred for later:

- supersampled texel coverage;
- conservative rasterization;
- seam-aware filtering;
- multi-sample coverage masks.

---

## AO Sampling Decision

The AO sample model matches the debug renderer conceptually:

- cosine-weighted hemisphere sampling;
- short occlusion rays;
- deterministic per-texel RNG seeding;
- a normal-offset bias against self-intersection.

This is useful because it keeps debug AO and baked AO conceptually aligned during early development.

### Current Adaptive Sampling Policy

The baker now uses adaptive AO sampling per texel.

The current policy is:

- always cast at least `4` rays;
- estimate AO from those first samples as a Bernoulli mean;
- estimate the standard error of that mean;
- stop early when the estimated error falls below a configurable threshold;
- otherwise continue until the configured maximum sample count is reached.

This gives the baker a cheap early-out on texels that are obviously open or obviously occluded, while still allowing difficult boundary texels to spend more rays.

The default bake profile is:

- minimum samples: `4`
- maximum samples: `256`
- error threshold: `0.03`

---

## Padding Decision

The baker keeps a **1-bit validity mask** for texels that were actually rasterized from UV triangles.

Why:

- UV charts are packed with padding by `xatlas`;
- valid texels should bleed outward into nearby invalid texels;
- otherwise the bake image would leave black gaps around islands.

The fill step now works as an iterative texel propagation pass:

- each pass only writes into texels that are still invalid;
- each newly written texel copies the AO value of a nearby valid texel;
- the validity mask grows outward iteratively.

This is closer to the standard lightmap-style gutter fill than the earlier neighbor-average prototype.

### Current Pass Count Policy

If no explicit dilation pass count is provided, the baker derives it automatically from the bake resolution:

- `16` passes minimum;
- `64` passes maximum;
- approximately `max(width, height) / 32` in between.

Examples:

- around `512` pixels: `16` passes;
- around `1024` pixels: `32` passes;
- around `2048` pixels: `64` passes.

This intentionally favors robust island padding over preserving a black background around charts.

---

## Current Constraints

The first baker currently assumes:

- one UV set in `uv0`;
- one atlas page;
- one grayscale AO output image;
- no multi-material texture orchestration.

If atlas generation produces multiple pages, the unwrap stage already fails explicitly.

That constraint remains valid for the baker.

---

## Output Decision

The first bake output is stored as an RGB PNG containing replicated grayscale AO.

This is not the most compact format, but it is convenient because:

- the repository already has a simple RGB PNG writer;
- the image is directly inspectable in common tools;
- the next milestones can stay focused on functionality.

If needed later, this can be replaced by:

- single-channel output;
- packed material textures;
- EXR or higher-precision outputs.

### Temporary Preview Material Export

When AO baking is requested together with OBJ export, the tool now also writes a companion `.mtl` file next to the OBJ.

For now this material export is intentionally provisional:

- one material only;
- the baked AO texture is connected as `map_Kd`;
- the AO is treated as a temporary diffuse preview.

This is not meant as the final shading model.

It exists so the unwrapped OBJ can be inspected immediately in DCC tools without a separate manual material setup step.

---

## Deferred Work

The following remain intentionally deferred:

- multi-page atlas baking;
- AO supersampling;
- chart debug images;
- seam-aware blur or resolve;
- source-aware nearest-fill heuristics beyond simple neighborhood propagation;
- packed bake outputs;
- bent-normal or curvature baking;
- parallel bake execution.

---

## Practical Use

The first milestone is meant to answer one concrete question:

Can the project now generate a UV layout and bake a plausible AO texture from the generated SDF mesh without leaving the repository?

With this milestone, the answer should be yes.
