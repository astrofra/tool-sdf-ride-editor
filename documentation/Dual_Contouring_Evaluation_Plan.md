# Dual Contouring Evaluation Plan

## Status

Planned on **August 7, 2026**.
First uniform-grid milestone implemented on **August 7, 2026**.

This note records the current design and technical decision to evaluate dual contouring as the next meshing evolution milestone.

---

## Current Situation

The current mesh extractor is a **uniform-grid marching tetrahedra style** polygonizer.

More precisely:

- the SDF is sampled on a regular grid;
- each cube cell is split into six tetrahedra;
- each tetrahedron is polygonized independently;
- the emitted result is a triangle soup.

This implementation is intentionally simple and robust, but it has two practical costs:

- it produces many small triangles, especially around noisy or broken surfaces;
- it creates many local normal-direction changes that are expensive for downstream UV chart generation.

Recent production-like timings showed that the dominant unwrap cost is now:

- `xatlas ComputeCharts`

not:

- OBJ export;
- BVH construction;
- AO denoise;
- PNG writing.

This makes meshing quality and triangle budget a more urgent optimization target than most later pipeline stages.

---

## Decision

The next meshing R&D milestone should evaluate **dual contouring**.

This is not yet a decision to fully replace the current extractor.

It is a decision to build and benchmark a first dual contouring path against the current polygonizer under the same scene and bake workloads.

### Current Implemented Milestone

The repository now contains a first opt-in dual contouring path.

Current implementation shape:

- the existing marching-tetrahedra extractor remains available;
- a `MeshingMode` selector now exists in build settings;
- scene files can now declare `meshing_mode`;
- the CLI can now override meshing mode explicitly;
- the new dual contouring path runs on the same uniform sampling grid as the old extractor;
- downstream OBJ export, unwrap, ray build, and AO bake remain unchanged.

This milestone should be treated as a practical evaluation baseline, not as the final meshing architecture.

---

## Why Dual Contouring

Dual contouring is attractive here because it aligns with the current artistic and technical needs:

- the scene is largely architectural and box-driven;
- preserving sharper silhouettes is more valuable than producing softly smoothed surfaces;
- a lower triangle budget should reduce `xatlas ComputeCharts` time directly;
- a less fragmented surface should also improve unwrap behavior indirectly.

Compared to the current extractor, the expected advantages are:

- fewer triangles for similar visible shape quality;
- better preservation of hard or near-hard structural features;
- less unwrap fragmentation pressure;
- lower total bake preparation cost.

---

## Important Scope Boundary

The first milestone should **not** jump directly to adaptive octree dual contouring.

That would mix several hard problems at once:

- adaptive cell subdivision;
- crack prevention across LOD boundaries;
- topology consistency;
- sharper implementation complexity growth;
- harder debugging and benchmarking.

The first milestone should instead implement **uniform-grid dual contouring** on the same regular sampling domain already used by the current generator.

This keeps the comparison fair:

- same scene input;
- same bounds;
- same cell size control;
- same downstream unwrap and bake pipeline.

If that first milestone already reduces `xatlas` cost enough, it may be sufficient for a long time.

If not, an adaptive octree version can be considered later.

This scope boundary remains valid even after the first implementation milestone:

- the current implementation is still uniform-grid only;
- adaptive subdivision is still deferred.

---

## First Milestone Shape

The first dual contouring milestone should:

1. keep the current extractor available;
2. add a second extractor mode in parallel;
3. reuse the same `SceneDocument`, `BuildSettings`, and `Mesh` outputs;
4. keep OBJ export, unwrap, ray build, and AO bake unchanged downstream;
5. expose enough instrumentation to compare both paths honestly.

Recommended implementation shape:

- introduce a meshing mode enum in build settings or CLI-facing configuration;
- keep the current marching-tetrahedra path as the baseline mode;
- add a dual-contouring path beside it, not as an in-place rewrite;
- compare outputs before deciding on default behavior changes.

This recommended shape is now implemented.

---

## Technical Goals

The first dual contouring implementation should focus on these goals only:

- one vertex per active grid cell;
- edge-sign crossing detection from the existing sampled SDF field;
- simple normal-aware vertex placement;
- stable triangle emission from cell-face connectivity;
- output compatibility with the existing `Mesh` struct.

The current implementation uses:

- one shared vertex per active cell;
- edge-crossing hermite samples from the existing sampled SDF field;
- a lightweight regularized least-squares style cell-vertex solve;
- triangulated dual faces for downstream compatibility.

The first milestone does **not** need to solve everything:

- adaptive octrees;
- perfect manifold guarantees;
- hermite-data caching optimizations;
- aggressive quad preservation;
- LOD transitions;
- multi-resolution stitching.

---

## Key Risks

The risks are real and should be acknowledged up front.

### Vertex Placement Quality

A naive dual contouring implementation can place vertices poorly and produce:

- surface wobble;
- self-intersections;
- collapsed or stretched triangles;
- unstable behavior near noisy modifiers.

### Sharp Feature Policy

The scene contains both:

- large planar brutalist forms;
- locally damaged or noise-displaced surfaces.

The solver policy must avoid overfitting noise while still preserving major shape cues.

### Topology and Connectivity

Even a regular-grid version still needs careful face construction across neighboring cells.

Bad connectivity rules can create:

- holes;
- flipped faces;
- inconsistent winding;
- non-manifold output.

### Downstream Compatibility

The resulting mesh must remain compatible with:

- xatlas unwrap;
- BVH construction;
- AO baking;
- OBJ export;
- later Lua-side integration.

A lower triangle count is not sufficient by itself if the resulting topology becomes less stable for unwrap.

---

## Success Metrics

The evaluation should be judged on measurable results, not only on visual intuition.

Primary metrics:

- generated triangle count;
- generated vertex count;
- `xatlas ComputeCharts` time;
- total unwrap time;
- total bake preparation time;
- total end-to-end CLI time.

Secondary metrics:

- chart count;
- single-triangle chart count;
- unwrap atlas utilization;
- AO bake visual stability;
- obvious shading or silhouette regressions.

The first milestone should be considered promising if it gives a meaningful reduction in:

- triangle count;
- `xatlas ComputeCharts` time;
- total pipeline time

without causing unacceptable visual regressions on the current blockout scenes.

---

## Recommended Evaluation Order

1. Keep the current extractor as the baseline.
2. Add a selectable dual contouring path.
3. Run the same scene at the same `cell_size`.
4. Compare geometry counts and phase timings.
5. Inspect the unwrapped mesh and AO bake visually.
6. Decide whether dual contouring becomes:
   - an optional mode only;
   - the new default mesher;
   - the base for a later adaptive octree implementation.

---

## Deferred Follow-Up

If the uniform-grid milestone is successful, the next meshing questions become:

- adaptive octree dual contouring;
- vertex welding or indexed mesh cleanup earlier in the pipeline;
- optional post-mesh simplification for flatter regions;
- sharper-feature-aware placement policy tuning;
- automatic meshing mode selection by scene or quality preset.

These are explicitly **deferred** until the first comparison milestone is implemented and measured.
