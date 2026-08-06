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

---

## Padding Decision

The first baker performs a simple post-bake dilation pass.

Why:

- UV charts are packed with padding by `xatlas`;
- valid texels should bleed outward into nearby invalid texels;
- otherwise the bake image would leave black gaps around islands.

The current dilation is intentionally modest:

- neighbor averaging;
- fixed small pass count;
- no seam classification.

This is a functional first step, not the final padding strategy.

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

---

## Deferred Work

The following remain intentionally deferred:

- multi-page atlas baking;
- AO supersampling;
- chart debug images;
- seam-aware blur or resolve;
- packed bake outputs;
- bent-normal or curvature baking;
- parallel bake execution.

---

## Practical Use

The first milestone is meant to answer one concrete question:

Can the project now generate a UV layout and bake a plausible AO texture from the generated SDF mesh without leaving the repository?

With this milestone, the answer should be yes.
