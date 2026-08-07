# UV Unwrap Integration Plan

## Status

Planned and first milestone implemented on **August 6, 2026**.
Chart-debug and fragmentation instrumentation added on **August 7, 2026**.

This note records the current design and technical decisions for adding automatic UV unwrapping to the SDF mesh pipeline.

---

## Library Decision

The selected unwrap library is **xatlas**.

Reasoning:

- it is a small C++ library with no external dependencies;
- upstream documents direct integration through only `xatlas.cpp` and `xatlas.h`;
- it is explicitly positioned for baking-oriented UV generation;
- it is a more practical fit for this repository than older or platform-specific alternatives.

Alternatives considered:

- `thekla_atlas`: important historical basis, but older and less convenient as a local integration target;
- `UVAtlas`: capable, but the Microsoft repository explicitly marks it as archived and says it is not recommended for new projects.

---

## Vendoring Decision

The library is vendored locally under `vendor/xatlas/`.

This repository keeps:

- `xatlas.cpp`
- `xatlas.h`
- `LICENSE`
- a local upstream tracking note

The goal is to keep UV generation:

- reproducible;
- buildable without an external install step;
- easy to update in a controlled way.

---

## Integration Shape

The unwrap path is implemented as a local `sdf_core` module:

- `src/sdf_core/include/sdf/uv_unwrap.h`
- `src/sdf_core/src/uv_unwrap.cpp`

The module:

1. accepts a generated `Mesh`;
2. welds the triangle-soup input into a shared-vertex unwrap mesh;
3. submits that welded mesh to `xatlas`;
4. rebuilds a new local `Mesh` with seam-aware duplicated vertices and normalized `uv0`;
5. returns atlas statistics for logging and later bake stages.

The existing generator remains unchanged.

This is intentional:

- mesh extraction and mesh UV generation are separate concerns;
- the unwrap step should remain optional from the CLI;
- later bake stages should be able to reuse the same unwrap output.

---

## Mesh Topology Decision

The current polygonizer emits vertices per triangle.

This is not ideal as a long-term mesh format, but it is acceptable for the first unwrap milestone because `xatlas` supports colocal vertex handling through its mesh declaration epsilon.

For now:

- the generator stays simple;
- unwrap is applied on the generated mesh as-is;
- any future shared-vertex optimization is deferred.

This keeps the UV milestone focused on function rather than premature mesh refactoring.

To compensate partially, the unwrap path now performs a lightweight vertex weld before calling `xatlas`.

This does not change the generator API, but it gives the unwrap library a better connected input topology.

As a side effect, the unwrap/export mesh may contain fewer triangles than the raw generated triangle soup when welded duplicates or degenerate faces collapse away.

The same topology choice also explains why unwrap fragmentation is still visible:

- the generated mesh is effectively a triangle soup at the API level;
- the tetrahedral surface extractor produces many small local directional changes;
- automatic chart generation therefore sees many opportunities to split islands.

This is a structural limitation of the current mesher, not only an unwrap-library issue.

---

## Low-Fragmentation Chart Profile

The unwrap module now uses a lower-fragmentation chart profile by default.

Compared to the effective `xatlas` defaults, this profile is intentionally more tolerant:

- more chart-growing iterations;
- higher chart growth cost threshold;
- lower penalties for normal deviation;
- lower penalties for straightness and roundness;
- much lower normal seam weight;
- zero texture seam weight from the provisional input UVs.

The unwrap path also stops feeding the generator's provisional `uv0` values into `xatlas`.

This is intentional because those UVs are only placeholder projections and can bias chart segmentation in unhelpful ways.

The goal is not to produce artist-grade UVs.

The goal is to reduce unnecessary chart explosion on the current generated mesh so AO baking produces fewer visibly isolated islands.

This does **not** remove the deeper limitation from the current mesher topology, but it improves the unwrap stage enough to be useful now.

---

## Current Scope

The current unwrap path supports:

- one generated mesh at a time;
- one UV set in `uv0`;
- OBJ export of the unwrapped result;
- CLI-driven atlas resolution and padding control;
- chart-colored UV atlas debug images;
- unwrap fragmentation statistics in the CLI output.

The current unwrap path does **not** yet support:

- multiple atlas pages in export;
- seam padding or bake dilation;
- UV-aware AO baking.

### Current Fragmentation Instrumentation

The unwrap stage now reports additional chart-fragmentation metrics.

The current CLI output includes:

- chart count;
- chart triangles min / average / max;
- single-triangle chart count;
- chart texels min / average / max;
- occupied chart texel count;
- padding texel count.

This is intended to make unwrap tuning less blind before deeper mesher changes happen.

### Current UV Chart Debug Output

The unwrap path can now also emit a chart-colored atlas debug image.

The current CLI switch is:

- `--debug-uv-charts PATH`

The output uses deterministic per-chart colors and distinguishes padding texels visually.

This is meant for:

- quick inspection of island count and packing;
- checking whether neighboring colors correspond to unrelated islands;
- understanding whether unwrap fragmentation or bake settings are the dominant issue.

---

## Single-Atlas Constraint

The first implementation accepts only a **single atlas page**.

If `xatlas` decides to split output into multiple atlas pages, the unwrap step fails explicitly.

This is deliberate because:

- the current OBJ export path assumes one UV domain;
- the future AO baker should begin with a single texture target;
- multi-page support adds policy and asset-format questions that are not needed yet.

If this becomes a blocker later, the next step should be one of:

1. force a larger atlas resolution;
2. add multi-material or multi-object export;
3. add explicit multi-page bake support.

---

## Debug-Build Performance Decision

Upstream warns that `xatlas` is much slower when its internal debug path is enabled.

Because this repository currently uses debug-oriented local scripts, the vendored `xatlas.cpp` is compiled with:

- `XA_DEBUG=0`

This applies only to the vendored library translation unit, not to the rest of the project.

The goal is to keep local iteration practical without changing the global project configuration.

---

## CLI Direction

The unwrap path is exposed through coarse CLI switches:

- `--unwrap-uvs`
- `--uv-resolution N`
- `--uv-padding N`

This is enough for the first milestone.

The lower-fragmentation chart profile is currently applied internally rather than exposed as a large set of CLI tuning flags.

This is deliberate:

- the project does not yet need a full unwrap-authoring surface;
- broad chart tuning would add noise to the CLI too early;
- the current objective is to keep the default bake workflow improving without making the tool harder to use.

---

## Immediate Next Uses

This unwrap milestone exists mainly to unlock:

- inspection of generated UVs in external DCC tools;
- future UV-space AO bake experiments;
- later texture-space rasterization utilities;
- later padding and bake post-processing work.

The unwrap step is not the final bake pipeline.

It is the prerequisite that turns the current mesh generator into something bakeable.
