# Procedural Brutalist Ride Editor – Lua/C++ Roadmap

## 1. Objective

This roadmap defines how to split responsibilities between Lua and C/C++ for the procedural brutalist ride editor described in `Procedural_Brutalist_Ride_Editor_Specification.md`.

The priority is **model generation performance**, not rendering or viewport integration.

The native side must own all heavy geometry work:

- SDF evaluation;
- meshing;
- sector splitting;
- LOD generation;
- UV unwrapping;
- baking of geometric textures such as AO / curvature / cavity;
- export of generated artifacts.

Lua should remain the orchestration and authoring layer:

- scene editing logic;
- UI/editor logic;
- undo/redo;
- procedural presets and artist-facing tools;
- build triggering;
- inspection of build results and diagnostics.

The solution should stay **engine-agnostic** and **HARFANG-independent**, so that the same native core can be used:

- from Lua in-process;
- from a command-line tool;
- later from another host if needed.

---

## 2. Architectural Position

The correct boundary is **not** “Lua calls native functions for every geometric operation”.

The correct boundary is:

- Lua authors and edits a **procedural scene document**;
- C++ owns the **compiled scene representation** and all heavy build steps;
- Lua submits **coarse-grained build requests**;
- C++ returns **artifacts, handles, manifests, and diagnostics**, not raw per-sample callbacks.

This is important for performance:

- Lua must never sit inside the SDF hot path;
- Lua must never be called for per-voxel, per-triangle, or per-vertex decisions;
- large meshes should not shuttle back and forth between Lua and C++;
- the interface should be batch-oriented and job-oriented.

In practice, the native core should be treated like a **content compiler**, not like a math helper library.

---

## 3. Recommended Component Split

### 3.1 Native core

Create a portable native stack with these layers:

1. `ride_core`
   - pure C++ domain code;
   - no HARFANG dependency;
   - no editor UI dependency;
   - owns scene compilation, meshing, UVs, baking, export.

2. `ride_c_api`
   - stable C ABI over the C++ core;
   - small surface area;
   - opaque handles instead of exposing the C++ class graph directly.

3. `ride_cli`
   - headless executable built on the same C API / core;
   - used for offline generation, batch baking, CI, and reproducible builds.

4. `ride_lua`
   - thin Lua module over the C API;
   - exposes document editing, job submission, and result queries;
   - does not duplicate geometry logic.

### 3.2 Suggested native modules

- `scene`
  - procedural source document;
  - primitive definitions;
  - transforms;
  - noise / deformation descriptors;
  - sector metadata;
  - manual instance metadata.

- `compiler`
  - dirty tracking;
  - scene normalization;
  - dependency graph;
  - sectorization;
  - build caching.

- `sdf`
  - box SDFs;
  - boolean / blend operators if introduced;
  - damage and erosion modifiers;
  - detail controls.

- `mesher`
  - SDF to polygon conversion;
  - mesh cleanup;
  - normal generation;
  - optional LOD derivation.

- `uv`
  - UVAtlas integration;
  - texel density and chart policy;
  - atlas packing configuration.

- `bake`
  - AO;
  - curvature / edge intensity;
  - cavity;
  - packed output texture generation.

- `io`
  - scene serialization;
  - build manifests;
  - OBJ export;
  - native binary cache files.

---

## 4. What the Lua/C++ Contact Surface Should Be

The contact surface should be **document-oriented**, **transactional**, and **asynchronous where possible**.

### 4.1 Scene document API

Lua needs to create and edit a procedural source document.

Recommended operations:

- create / load / save a scene document;
- create / delete / duplicate nodes;
- batch-apply edit operations;
- set primitive parameters;
- set deformation parameters;
- assign sector or streaming tags;
- attach custom metadata for future editor use;
- query bounds, hashes, dirty flags, and lightweight statistics.

Important rule:

- Lua should send **edit commands** or **batched property sets**;
- C++ should own the validated internal scene representation.

Recommended pattern:

- `begin_edit(document)`
- `apply_ops(document, ops[], count)`
- `end_edit(document)`

This keeps validation, hash recomputation, and dirty propagation on the native side and avoids excessive bridge traffic.

### 4.2 Build API

Lua needs to trigger builds without touching the heavy implementation details.

Recommended build requests:

- build one object;
- build one sector;
- build a sector range;
- rebuild only dirty sectors;
- generate LODs;
- unwrap UVs;
- bake textures;
- export artifacts.

Build requests should be explicit structs, for example:

- target sectors;
- resolution / quality tier;
- meshing parameters;
- UV atlas size;
- bake channels;
- output directory;
- overwrite / cache policy;
- deterministic seed.

### 4.3 Job API

Generation should be job-based rather than blocking the editor on every operation.

Recommended job operations:

- submit build job;
- poll progress;
- wait for completion;
- cancel job;
- fetch diagnostics;
- fetch artifact manifest.

Lua should receive:

- job id;
- progress percentage or coarse build stage;
- warnings / errors;
- output manifest path or in-memory manifest handle.

### 4.4 Artifact API

Lua does not need raw geometry for most operations.

Lua usually needs:

- mesh metadata;
- output paths;
- counts and timings;
- bounding boxes;
- sector-level status;
- bake texture locations;
- manifest summaries.

Only expose full vertex/index buffers when there is a proven need.

For performance, prefer:

- native binary caches for meshes;
- manifest files for metadata;
- optional OBJ export as a secondary format.

### 4.5 Diagnostics API

The native side should report authoring and build problems clearly:

- invalid parameters;
- non-manifold output;
- UV unwrap failure;
- bake failure;
- degenerate geometry;
- cache invalidation reasons;
- per-stage timing.

This is part of the contact surface because the editor will need to explain build failures to the artist.

---

## 5. Data Ownership and Performance Rules

These rules matter more than the exact binding library.

### 5.1 Keep the hot path native

Never expose these as Lua-driven callbacks:

- distance evaluation;
- meshing inner loops;
- per-triangle UV operations;
- per-texel baking;
- LOD simplification loops.

### 5.2 Prefer coarse calls over chatty calls

Bad shape:

- `set_box_size(id, x, y, z)` called thousands of times individually;
- `get_vertex(i)` repeated from Lua;
- `evaluate_sdf(x, y, z)` called from Lua in a sampling loop.

Good shape:

- `apply_ops(document, ops[], count)`;
- `build_dirty_sectors(document, options)`;
- `get_build_manifest(job)`.

### 5.3 Use opaque handles

Expose handles such as:

- `ride_document_handle`;
- `ride_job_handle`;
- `ride_artifact_handle`.

Do not expose template-heavy C++ types directly as the stable public boundary.

### 5.4 Make results disk-friendly

Because the same system must work from Lua and CLI, every major output should have a serializable form:

- scene document;
- build cache;
- mesh artifact;
- bake artifact;
- manifest;
- diagnostics report.

### 5.5 Support incremental rebuilds

The native compiler should hash:

- primitive parameters;
- deformation parameters;
- sector contents;
- build options affecting output.

Lua should be able to ask for:

- dirty sectors only;
- dirty UV/bake only;
- full clean rebuild.

Incremental rebuild support will matter more than micro-optimizing the binding layer.

---

## 6. Binding Strategy Recommendation

### Recommendation Summary

The recommended architecture is:

1. **C++ core**
2. **small C ABI**
3. **thin Lua binding on top of the C ABI**
4. **CLI built on the same core**

This is better than binding the full C++ object model directly to Lua.

### 6.1 Why a C ABI should be the real boundary

A C ABI gives:

- host independence;
- easier CLI reuse;
- simpler testing;
- lower coupling to a single Lua binding technology;
- cleaner symbol visibility and memory ownership rules;
- easier future reuse from Python, tools, or another engine.

It also keeps the public surface stable even if the C++ internals change aggressively.

### 6.2 Best default: thin manual Lua module over the C ABI

For this project, the best baseline is a hand-controlled Lua module with a **small, explicit API**.

Reasons:

- the desired surface is narrow and domain-specific;
- performance depends mainly on batching and native ownership, not on auto-generated wrappers;
- memory ownership is easier to reason about;
- error reporting can be tailored to the editor;
- the same API can map cleanly to CLI semantics.

This approach is also the least tied to any engine ecosystem.

### 6.3 When `sol2` is a good option

If the host application is a C++ executable embedding Lua directly, `sol2` is a strong convenience layer for the Lua-facing side because it is header-only, supports Lua 5.1+ / LuaJIT, and is explicitly positioned as a fast C++/Lua binding layer in its official documentation.

Use it if:

- the host is already C++;
- you want pleasant Lua table marshaling;
- you still keep the **real stable boundary** at the C API or a very small wrapper layer.

Do **not** use `sol2` as a reason to expose the entire native object graph to Lua.

### 6.4 When `LuaBridge3` is a good option

`LuaBridge3` is also a reasonable lightweight choice if you want a smaller header-only wrapper and a simpler integration model.

It is acceptable for:

- a compact editor bridge;
- exposing a few document and job objects;
- cases where compile-time footprint matters.

It is still better used as a thin facade than as the core architectural contract.

### 6.5 Why `fabgen` is not the default recommendation

`fabgen` is viable, but it is not the best default for this project.

Reasons:

- the project needs a **carefully limited** contact surface, not a broad automatically generated one;
- the tool is historically tied to the HARFANG ecosystem, which is exactly what we want to avoid as an architectural dependency;
- the generator itself is GPLv3, even though its README states that generated code can be licensed independently;
- a custom build/codegen step adds complexity before the native API shape is stable.

`fabgen` becomes more attractive only if:

- the native API grows large;
- many classes must be mirrored into Lua;
- manual wrapper maintenance becomes a real burden.

For the first implementation phase, it is more pragmatic to avoid generator-driven bindings.

### 6.6 Best practical decision

The most robust decision for phase 1 is:

- define the C API first;
- build the CLI second;
- add a thin Lua binding third;
- only revisit auto-generated bindings after the API stabilizes.

---

## 7. Recommended Native API Shape

The exact names can change, but the shape should look like this:

```c
typedef struct ride_document_t* ride_document_handle;
typedef struct ride_job_t* ride_job_handle;

typedef enum ride_status {
  RIDE_STATUS_OK = 0,
  RIDE_STATUS_INVALID_ARGUMENT,
  RIDE_STATUS_BUILD_FAILED,
  RIDE_STATUS_IO_FAILED
} ride_status;

ride_document_handle ride_document_create(void);
ride_status ride_document_load(const char* path, ride_document_handle* out_doc);
ride_status ride_document_save(ride_document_handle doc, const char* path);
ride_status ride_document_apply_ops(
  ride_document_handle doc,
  const ride_edit_op* ops,
  size_t op_count
);

ride_status ride_build_submit(
  ride_document_handle doc,
  const ride_build_request* request,
  ride_job_handle* out_job
);

ride_status ride_job_poll(
  ride_job_handle job,
  ride_job_progress* out_progress
);

ride_status ride_job_get_manifest_json(
  ride_job_handle job,
  const char** out_json,
  size_t* out_size
);

void ride_string_free(const char* ptr);
void ride_job_destroy(ride_job_handle job);
void ride_document_destroy(ride_document_handle doc);
```

Key idea:

- Lua manipulates documents and jobs;
- C++ owns everything expensive;
- large results come back as manifests, file paths, or bulk buffers.

---

## 8. CLI Strategy

The CLI should not be treated as an afterthought.

It is a core part of the architecture because it proves that the native system is not engine-bound.

Suggested commands:

- `ride build scene.ride.json --out build/`
- `ride build --dirty --uv --bake`
- `ride export-obj scene.ride.json --sector 12`
- `ride bake scene.ride.json --channels ao,curvature,cavity`
- `ride inspect-cache build/cache/`

Suggested role split:

- Lua/editor = interactive authoring and job triggering;
- CLI = automation, regression testing, full offline generation, farm execution later if needed.

---

## 9. Delivery Roadmap

### Phase 1: Freeze the procedural source schema

Deliver:

- scene document format;
- primitive and deformation descriptors;
- build request schema;
- manifest schema.

Exit criterion:

- a scene can be serialized and reloaded without loss of procedural intent.

### Phase 2: Build the native core without Lua

Deliver:

- document model in C++;
- SDF evaluation;
- meshing;
- sector splitting;
- OBJ export;
- dirty tracking.

Exit criterion:

- a sample scene can be generated headlessly from the command line.

### Phase 3: Add the stable C API

Deliver:

- opaque handles;
- document editing API;
- build API;
- job API;
- diagnostics API.

Exit criterion:

- a small C test program can create a scene, build it, and export artifacts.

### Phase 4: Add the Lua bridge

Deliver:

- `ride_native` Lua module;
- Lua document wrappers;
- job polling;
- manifest access;
- error translation into Lua-friendly messages.

Exit criterion:

- Lua can create or load a scene, edit it, trigger a build, and inspect the manifest.

### Phase 5: Integrate UV unwrapping and baking

Deliver:

- UVAtlas integration;
- bake pipeline;
- packed texture output;
- per-stage diagnostics and timings.

Exit criterion:

- generated sectors come out with mesh + UVs + baked maps from both Lua and CLI.

### Phase 6: Add incremental and production-oriented features

Deliver:

- per-sector caching;
- partial rebuilds;
- LOD generation;
- deterministic build hashing;
- regression scenes and performance baselines.

Exit criterion:

- repeated builds avoid recomputing unchanged sectors and timings are measurable.

---

## 10. Open Questions

These should be answered before the API is considered stable.

1. What is the authoritative scene format?
   - Human-readable JSON is convenient.
   - MessagePack or a binary schema may be better if document size becomes large.

2. Will the editor runtime be standard Lua, LuaJIT, or both?
   - If LuaJIT is allowed, FFI over a C ABI becomes a more attractive option.
   - If plain Lua compatibility is required, a compiled Lua module is the safer baseline.

3. What meshing algorithm is preferred?
   - Marching cubes, dual contouring, or another approach will affect topology quality, UV conditioning, and bake quality.

4. How much of the scene document should live natively versus mirrored in Lua?
   - For performance, the build-authoritative copy should live in C++.
   - Lua may still keep a lightweight UI mirror for tools and undo/redo.

5. Should baking remain CPU-only at first?
   - CPU baking is easier to keep engine-agnostic.
   - A headless GPU backend may be worth adding later if bake times dominate.

6. What is the internal cache format?
   - Native binary mesh caches will be faster than OBJ.
   - OBJ should remain an interchange/export format, not the primary cache.

7. How much validation should happen on edit versus on build?
   - Early validation improves UX.
   - Deferred validation can reduce edit-time overhead.

8. Is UVAtlas sufficient for all target meshes?
   - Its official documentation recommends mesh cleaning before atlas generation.
   - The roadmap should include a conditioning step before unwrap.

9. What is the minimum artifact set for the first usable version?
   - Mesh only?
   - Mesh + UVs?
   - Mesh + UVs + baked texture pack?

10. Do manual instances and lights need to cross the same native boundary now?
   - If display is out of scope, they may remain metadata-only in phase 1.

---

## 11. Final Recommendation

For this project, the safest and most scalable structure is:

- **C++ owns procedural generation, UVs, baking, and caching**
- **a small C ABI is the real integration contract**
- **Lua stays as the authoring/orchestration layer**
- **CLI and Lua both use the same native core**
- **binding generation is optional, not foundational**

If a binding helper is still desired, prefer:

1. thin manual Lua binding over the C API;
2. `sol2` or `LuaBridge3` as convenience layers if embedding Lua inside a C++ host;
3. `fabgen` only after the API becomes broad enough to justify code generation.

This keeps the project fast, portable, and independent from HARFANG while leaving room for a richer editor later.
