# UV Unwrap Integration Plan

## Status

Planned and first milestone implemented on **August 6, 2026**.

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
2. submits it to `xatlas`;
3. rebuilds a new local `Mesh` with seam-aware duplicated vertices and normalized `uv0`;
4. returns atlas statistics for logging and later bake stages.

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

---

## Current Scope

The current unwrap path supports:

- one generated mesh at a time;
- one UV set in `uv0`;
- OBJ export of the unwrapped result;
- CLI-driven atlas resolution and padding control.

The current unwrap path does **not** yet support:

- multiple atlas pages in export;
- UV layout raster debug images;
- seam padding or bake dilation;
- UV-aware AO baking.

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

More detailed charting or packing controls should remain deferred until there is real need from baking workflows.

---

## Immediate Next Uses

This unwrap milestone exists mainly to unlock:

- inspection of generated UVs in external DCC tools;
- future UV-space AO bake experiments;
- later texture-space rasterization utilities;
- later padding and bake post-processing work.

The unwrap step is not the final bake pipeline.

It is the prerequisite that turns the current mesh generator into something bakeable.
