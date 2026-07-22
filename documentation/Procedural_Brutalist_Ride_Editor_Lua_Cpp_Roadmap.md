# Procedural Brutalist Ride Editor – Lua/C++ Roadmap

## 1. Objective

This roadmap defines the Lua/C++ split for the procedural brutalist ride editor described in `Procedural_Brutalist_Ride_Editor_Specification.md`.

The clarified target is:

- the final tool runs inside a Lua project;
- the host uses `hg = require("harfang")`;
- the procedural generator is loaded as `sdf = require("sdf-generator")`;
- the native side should have as few dependencies as possible;
- synchronous execution is preferred initially;
- maintainability and simplicity are more important than premature async infrastructure.

The native side must own the heavy work:

- SDF evaluation;
- meshing;
- split / sector generation;
- UV unwrapping;
- texture baking;
- geometry export.

Lua remains the authoring layer:

- scene editing;
- UI and editor workflow;
- undo/redo;
- preset logic;
- build triggering;
- error reporting and inspection.

Display integration and HARFANG import are out of scope for this roadmap. The native module is responsible only for producing geometry artifacts and related metadata.

---

## 2. Primary Integration Target

The primary integration target is a Lua native module:

```lua
hg = require("harfang")
sdf = require("sdf-generator")
```

The roadmap therefore should not optimize around a general multi-host binding system first.

The correct priority order is:

1. build a solid native core;
2. expose it as a small Lua module;
3. optionally keep the core reusable from a CLI or test harness;
4. avoid engine-specific code inside the generator itself.

The native generator should stay HARFANG-agnostic even if its first real consumer is a HARFANG-based Lua application.

---

## 3. Native Design Bias

Whenever it makes sense, the native code should follow a simple “Orthodox C++” style:

- keep the code readable to someone comfortable with C;
- prefer explicit data structures over deep object hierarchies;
- avoid exceptions in the core;
- avoid RTTI in the core;
- avoid clever template-heavy abstractions unless they clearly reduce complexity;
- keep ownership, allocation, and error paths explicit;
- prefer plain arrays, handles, POD-like structs, and straightforward control flow.

For this project, that translates into:

- simple structs for scene data and mesh data;
- explicit build functions;
- return-code based error handling;
- minimal hidden allocation;
- no dependency on large framework-style binding or serialization systems unless they solve a real problem.

This should be treated as a design bias, not as dogma. The point is to keep the codebase understandable and robust.

---

## 4. Recommended Native Component Split

The native side should be split into three layers only.

### 4.1 `sdf_core`

Pure C++ domain code with no HARFANG dependency and no Lua dependency.

It owns:

- scene document storage;
- SDF primitive evaluation;
- scene splitting;
- mesh generation;
- UV unwrap;
- baking;
- OBJ export.

### 4.2 `sdf_lua`

A thin Lua module that exposes `luaopen_sdf_generator`.

It owns:

- Lua stack marshaling;
- userdata lifetime;
- translation between Lua tables and native structs;
- error propagation into Lua.

It should not own generation logic.

Implementation note:

- this layer can be generated with Fabgen for consistency;
- any remaining hand-written code should stay limited to module bootstrap or special-case conversions.

### 4.3 `sdf_cli` (optional)

A small CLI built on the same core is still useful for:

- offline testing;
- regression scenes;
- bake debugging;
- batch export.

But it is optional and secondary. The roadmap should not be shaped around the CLI first.

---

## 5. How Complex the Scenegraph Should Be

The tool does **not** need a full engine-style scenegraph.

It needs a **procedural authoring graph** for SDF content, which is simpler.

### 5.1 What the scenegraph must represent

The scenegraph should represent:

- every SDF primitive;
- transform hierarchy when useful;
- grouping for editing convenience;
- build-related tags;
- per-node procedural parameters;
- optional material assignment or material tag;
- optional per-node export flags.

It does **not** need to represent:

- rendering state;
- draw calls;
- runtime animation systems;
- cameras;
- light evaluation internals;
- imported HARFANG nodes.

### 5.2 Recommended complexity level

The simplest useful structure is:

- a flat node array;
- stable integer node ids;
- each node stores its parent id or `invalid`;
- each node stores a type tag;
- each node stores a local transform;
- node-specific parameters live in explicit structs.

This is usually better than a deep graph of heap-allocated polymorphic objects.

Suggested node kinds:

- `group`
- `sdf_box`
- `sdf_modifier`
- `sector_marker`
- `instance_marker`
- `light_marker`

If the initial production mostly uses deformed boxes, the first usable version can be even simpler:

- `group`
- `sdf_box`
- `sector_marker`

Everything else can be added later.

### 5.3 Recommended scene data shape

The scene document should look conceptually like this:

```c
struct SdfBoxParams
{
  Vec3 size;
  float roundness;
  uint32_t material_id;
  uint32_t noise_id;
};

struct SceneNode
{
  uint32_t id;
  uint32_t parent;
  uint16_t kind;
  uint16_t flags;
  Transform local;
  uint32_t payload_index;
};

struct SceneDocument
{
  Array<SceneNode> nodes;
  Array<SdfBoxParams> boxes;
  Array<NoiseParams> noises;
  Array<MaterialSlot> materials;
  BuildSettings build_settings;
};
```

The important point is the separation between:

- generic node bookkeeping;
- primitive payload arrays;
- build settings;
- generated mesh artifacts.

### 5.4 Where split strategy belongs

Split strategy parameters such as:

- cell size;
- minimum bounds;
- maximum bounds;
- padding / overlap;
- resolution limits;

should primarily live in `BuildSettings`, not in the scenegraph itself.

Per-node overrides can exist later if needed, but the default should be global build settings plus optional per-sector overrides.

### 5.5 Recommendation

Keep the authoring scenegraph narrow and explicit.

Do not try to mirror a generic DCC or engine scenegraph. The tool is specialized and should stay specialized.

---

## 6. Recommended Geometry Representation

The roadmap should separate:

1. the **procedural source representation**;
2. the **working mesh representation** used during generation and baking;
3. the **export representation** written to OBJ.

Trying to use one structure for all three will usually create unnecessary complexity.

### 6.1 Procedural source representation

This is the scenegraph described above:

- nodes;
- transforms;
- SDF parameters;
- deformation parameters;
- build settings;
- material tags.

It does **not** store final triangles.

### 6.2 Working mesh representation

For meshing, UVs, and baking, use a native mesh struct that is explicit and simple.

Recommended baseline:

```c
struct MeshVertex
{
  Vec3 position;
  Vec3 normal;
  Vec2 uv0;
};

struct MeshTriangle
{
  uint32_t i0;
  uint32_t i1;
  uint32_t i2;
  uint32_t material_id;
};

struct Mesh
{
  Array<MeshVertex> vertices;
  Array<MeshTriangle> triangles;
};
```

This is intentionally triangle-centric.

### 6.3 Triangles vs quads

Quads are useful only as an authoring or intermediate convenience.

The canonical generated representation should be triangles because:

- meshing algorithms naturally produce triangles;
- UV unwrap and bake pipelines are usually triangle-based;
- OBJ export handles triangles well;
- triangulated output simplifies later processing.

If quads are useful for some procedural step, keep them as a temporary internal structure only.

### 6.4 Materials

Because display is out of scope, the material model should stay minimal.

The native tool only needs enough information to:

- tag faces or triangle ranges;
- write OBJ groups or `usemtl`;
- assign bake outputs consistently.

Recommended material representation:

- stable material id;
- human-readable name;
- optional export name;
- optional bake policy flags.

No shader graphs, no renderer state, no engine material objects.

### 6.5 Normals and UVs

Normals and UVs should be stored explicitly in the working mesh once generated.

Recommended rule:

- positions, normals, and UVs should already be split as needed for seams and hard edges before export.

This avoids forcing the rest of the tool to deal with OBJ-style separate indexing for positions, UVs, and normals.

### 6.6 Topology helpers

If UV unwrap or baking needs adjacency, edge maps, or manifold checks, build those as temporary helper structures during processing.

Do not make a half-edge or winged-edge structure the default persistent representation unless it proves necessary.

### 6.7 Generated artifact boundary

Generated geometry should live outside the scenegraph.

Suggested artifact structure:

```c
struct SectorMeshArtifact
{
  uint32_t sector_id;
  Mesh mesh;
  Aabb bounds;
  String obj_path;
  String bake_path;
};
```

This keeps authored data and generated data clearly separated.

---

## 7. Export Strategy

For the first useful version, a vendorized or custom Wavefront OBJ writer is a good default.

That aligns with the project constraints:

- simple format;
- easy to inspect;
- easy to debug;
- no dependency on a heavy scene SDK;
- already compatible with the existing Assimp-based import path downstream.

### 7.1 Recommendation

Use OBJ as the primary exported geometry format for now.

Recommended output set:

- `.obj` for geometry;
- `.mtl` if material grouping is needed;
- baked textures as separate files;
- a small manifest file if build metadata must be retained.

### 7.2 What OBJ export must support

The writer should support:

- positions;
- normals;
- UVs;
- triangle faces;
- object/group naming;
- material assignments;
- deterministic output order.

### 7.3 What should stay out of scope

This roadmap should not include:

- HARFANG import logic;
- Assimp integration;
- runtime scene instantiation;
- display optimization.

Those belong to the consumer side of the pipeline.

---

## 8. Generation Pipeline Inside the Tool

The native pipeline should be straightforward and synchronous by default.

Recommended build flow:

1. validate the scene document;
2. resolve transforms and primitive parameters;
3. compute split / sector layout;
4. evaluate SDF content per sector;
5. generate polygonal meshes;
6. compute normals;
7. unwrap UVs;
8. bake AO / curvature / sharp-edge or cavity data;
9. export OBJ and textures;
10. return build summary to Lua.

Optional later improvements:

- dirty-only rebuild;
- per-sector caching;
- optional background build mode.

But none of those should complicate the first implementation.

---

## 9. Lua/C++ Contact Surface

The Lua/C++ boundary should be:

- coarse-grained;
- synchronous first;
- document-oriented;
- easy to expose through Fabgen, with small manual shims only where needed.

### 9.1 Recommended Lua-facing model

Lua should manipulate a small number of concepts:

- scene document;
- nodes / primitives;
- build settings;
- build result.

Conceptually:

```lua
local sdf = require("sdf-generator")

local scene = sdf.new_scene()
local node = scene:add_box{
  parent = nil,
  position = {0, 0, 0},
  rotation = {0, 0, 0},
  size = {10, 20, 10},
  material = "concrete_a"
}

scene:set_split_settings{
  cell_size = 32.0,
  min = {-256, -64, -256},
  max = {256, 256, 2048}
}

local result = scene:build{
  out_dir = "build/ride",
  export_obj = true,
  bake = {"ao", "curvature", "edges"}
}
```

### 9.2 Recommended operations

Lua needs these operations first:

- create / destroy scene;
- load / save scene;
- add / remove / duplicate node;
- set transform;
- set primitive parameters;
- set split settings;
- build scene;
- build one sector;
- export build artifacts;
- query diagnostics and artifact paths.

### 9.3 Synchronous first

The default API should block until completion:

- `scene:build(options)`
- `scene:export_obj(options)`

If progress reporting is needed, a simple callback or coarse progress hook can be added later.

Do not design the first version around a job system unless real build times force it.

### 9.4 Bulk transfer rule

Lua should not pull raw geometry vertex-by-vertex unless there is a very specific tool need.

Preferred patterns:

- Lua edits procedural inputs;
- C++ writes OBJ and baked textures;
- Lua receives paths, counts, timings, and warnings.

This keeps the bridge simple and fast.

---

## 10. Binding Strategy Recommendation

### 10.1 Updated recommendation

For this project, Fabgen is a valid and sensible choice for the Lua binding layer.

Recommended order:

1. write the native generator in simple C++;
2. keep the public API small and explicit;
3. generate the Lua-facing binding with Fabgen for consistency;
4. keep any hand-written wrapper code minimal and localized.

This fits the project if the goal is:

- consistency with existing Lua/native integration style;
- a generated binding rather than a large hand-written Lua C API layer;
- readable generated code with few runtime dependencies;
- a Lua module that still exposes a narrow, domain-specific surface.

### 10.2 Why Fabgen fits here

Fabgen is independent from HARFANG as a tool, even if it was originally created in that ecosystem.

That makes it reasonable here because:

- the final consumer is a Lua module, not a general multi-language SDK;
- the binding layer can stay generated while the native core remains engine-agnostic;
- generated code remains outside the core architecture and does not force HARFANG-specific design choices;
- it can reduce wrapper boilerplate while preserving a simple C++ codebase.

The roadmap therefore should treat Fabgen as a tooling choice, not as an engine dependency.

### 10.3 How to use Fabgen without damaging the design

Fabgen should generate bindings for a deliberately small API, not for an uncontrolled native object graph.

Recommended rules:

- expose scene, node, build settings, and build result concepts only;
- avoid binding internal meshing, baking, or topology helper types unless Lua truly needs them;
- prefer explicit entry points over large class hierarchies;
- keep ownership and lifetime rules obvious from the Lua side;
- avoid per-vertex or per-triangle Lua access in the first version.

The important point is that Fabgen should automate the wrapper, not define the architecture.

### 10.4 Manual binding is still a fallback

If Fabgen proves awkward for one part of the API, a small manual shim is still acceptable.

That fallback should be used for:

- module bootstrap;
- special conversions;
- awkward ownership cases;
- diagnostics formatting.

But the main roadmap can assume Fabgen for consistency.

### 10.5 Why not prioritize `sol2` or `LuaBridge3`

They remain viable libraries, but if the team values consistency around Fabgen-generated Lua bindings, there is no strong reason to introduce another binding abstraction first.

The better choice is to keep:

- simple C++ in the core;
- Fabgen at the boundary;
- explicit data structures throughout the API.

### 10.6 Internal boundary still matters

Even when Fabgen is used, the native code should still keep a clean separation between:

- binding definitions;
- scene/document code;
- generation code;
- export code.

That separation matters more than the wrapper technology itself.

---

## 11. Dependencies Policy

The dependency policy should be conservative.

### 11.1 Baseline

Prefer a baseline of:

- the Lua C API;
- the C/C++ runtime;
- project-local code;
- small vendorized single-purpose helpers when justified.

### 11.2 Avoid early heavy dependencies

Avoid introducing large dependencies for:

- scenegraph management;
- serialization;
- mesh containers;
- full asset SDKs.

Fabgen is acceptable here as a targeted binding tool rather than as a general framework dependency.

### 11.3 UV unwrap and bake dependencies

UV unwrap and baking are the two areas most likely to justify external code.

The roadmap should therefore keep those behind narrow internal interfaces:

- `unwrap_mesh(mesh, settings)`
- `bake_mesh(mesh, bake_settings)`

This keeps the core architecture stable even if the implementation changes later.

### 11.4 Recommendation

Do not hard-wire the entire architecture to one third-party UV or bake library at the roadmap stage.

Keep those as replaceable backends behind simple internal APIs.

---

## 12. Delivery Roadmap

### Phase 1: Define the authoring document and mesh structures

Deliver:

- scene document format;
- node kinds;
- primitive parameter structs;
- split settings;
- mesh structs;
- material slot structs.

Exit criterion:

- the codebase has a stable in-memory representation for authored scene data and generated mesh data.

### Phase 2: Build the native core

Deliver:

- scene document implementation;
- SDF box primitives;
- transform resolution;
- split / sector generation;
- triangle mesh generation;
- normal generation;
- OBJ writer.

Exit criterion:

- a sample scene can generate OBJ output from the native side.

### Phase 3: Add Lua binding

Deliver:

- `luaopen_sdf_generator`;
- Fabgen binding definition files;
- scene userdata;
- node creation/edit functions;
- build and export entry points;
- diagnostics reporting.

Exit criterion:

- Lua can build a procedural scene and export OBJ through `require("sdf-generator")`.

### Phase 4: Add UV unwrap

Deliver:

- UV data in working meshes;
- unwrap settings;
- seam handling;
- export of UV-ready OBJ.

Exit criterion:

- generated OBJ files contain usable UV coordinates.

### Phase 5: Add bake pipeline

Deliver:

- AO bake;
- curvature or edge bake;
- cavity or sharp-edge signal;
- packed output textures;
- bake result metadata.

Exit criterion:

- each generated sector can produce geometry plus baked texture outputs.

### Phase 6: Add pragmatic production improvements

Deliver:

- dirty rebuild support;
- per-sector cache files;
- optional CLI;
- optional background build mode if needed;
- performance baselines.

Exit criterion:

- the tool is production-usable without changing the core architecture.

---

## 13. Open Questions

1. Do we want the scene document stored as JSON, a custom text format, or a small binary format?

2. Is the first native module target plain Lua only, or must it support LuaJIT too?

3. Do we still want to depend on Microsoft UVAtlas, or should the first version keep UV unwrap behind a local backend interface and decide later?

4. Should baked outputs be one packed texture per sector, or one packed texture per exported object?

5. Do material ids need to be stable across rebuilds for downstream HARFANG import logic?

6. Should sector bounds be fully global build settings, or do we need per-zone overrides from day one?

7. Is there any real need to expose raw generated vertices back to Lua, or is path-based artifact exchange enough?

8. Do lights and manual instances need native ownership now, or can they remain Lua/editor metadata until rendering integration matters?

9. Is deterministic OBJ output required for regression testing and source control diffs?

10. What is the smallest acceptable bake feature set for the first production milestone: AO only, AO plus curvature, or the full packed map?

---

## 14. Final Recommendation

The simplest architecture that matches the stated constraints is:

- a simple C++ core with explicit scene and mesh structs;
- a Fabgen-generated Lua binding exposed as `require("sdf-generator")`;
- a narrow procedural authoring graph, not a full scene engine graph;
- a triangle-centric working mesh representation with explicit normals and UVs;
- OBJ export as the primary geometry handoff format;
- UV and bake code behind replaceable internal interfaces;
- synchronous builds first, async only if later proven necessary.

This keeps the native side small, understandable, and aligned with the actual production use case while preserving consistency in the Lua binding layer.
