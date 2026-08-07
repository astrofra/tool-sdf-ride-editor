# Adaptive Dual Contouring Octree Milestone

## Status

Planned and first implementation milestone started on **August 7, 2026**.

This note records the next meshing evolution step after the successful uniform-grid dual contouring baseline.

---

## Why This Exists

The current uniform `dual_contouring` mode already reduced:

- mesh generation time;
- triangle count;
- `xatlas ComputeCharts` time;
- total end-to-end bake time.

That said, it still samples and emits detail at a nearly uniform spatial rate.

This is wasteful for the current scene family because:

- large brutalist walls and slabs are mostly planar;
- silhouette and break-up detail are concentrated around edges, corners, cuts, and noise masks;
- the unwrap pipeline still pays for unnecessary local tessellation in broad flat regions.

The next logical step is therefore:

- keep `cell_size` as the **minimum** geometric resolution;
- allow larger cells in flatter areas;
- preserve smaller cells near shape transitions and damaged zones.

---

## Decision

The repository should evaluate a first **adaptive dual contouring octree** path.

This remains an opt-in meshing mode, not a default replacement yet.

The first milestone must stay pragmatic:

- keep the current `marching_tetrahedra` path untouched;
- keep the current uniform `dual_contouring` path untouched;
- add a third meshing mode for adaptive extraction;
- reuse the same downstream OBJ, UV, BVH, debug render, and AO bake pipeline.

---

## Scope Of The First Milestone

The first adaptive milestone is intentionally constrained.

It should:

- treat `cell_size` as the finest cell size;
- build a power-of-two octree-style hierarchy above that finest grid;
- merge flat surface regions into larger leaves;
- keep sharper regions at finer resolution;
- emit the same `Mesh` output format as the existing modes.

It should **not** try to solve all long-term adaptive meshing questions immediately:

- perfect manifold guarantees in every pathological case;
- feature tagging or explicit sharp-edge constraints;
- quad-preserving export;
- multi-object meshing policies;
- LOD streaming;
- GPU-oriented extraction.

---

## First Implementation Strategy

The first implementation is based on a practical compromise:

1. sample the SDF on the existing finest regular grid;
2. detect active fine cells exactly as before;
3. recursively group those active fine cells into power-of-two blocks;
4. accept a coarse leaf only if the local surface looks flat enough;
5. place one dual vertex per accepted leaf from aggregated Hermite samples;
6. connect the resulting surface while keeping the existing triangle-based downstream compatibility.

This is "octree-driven" in structure, but still anchored to the already proven fine-grid sampling stage.

That is deliberate.

It keeps:

- the debugging model simple;
- the progress instrumentation meaningful;
- the comparison against uniform dual contouring honest.

---

## Leaf Acceptance Heuristic

The first milestone uses a simple heuristic instead of a full research-grade error metric.

A coarse candidate leaf is accepted only if:

- it contains active fine cells;
- the aggregated Hermite samples can be represented by one dual vertex with low residual plane error;
- the sample normals stay sufficiently coherent.

In practical terms, this means:

- flat slabs and walls should merge upward;
- corners should refuse merging because normals diverge;
- noisy tops and broken mask regions should usually refuse merging because fit residuals rise.

This is not yet a final quality metric.

It is a first production-oriented approximation that should already capture the main win:

- fewer polygons on planes;
- finer tessellation near geometric events.

---

## Connectivity Strategy

Full adaptive dual contouring connectivity can become complex very quickly.

For this first milestone, the implementation may rely on a simplified connection strategy derived from the finest sampled grid, then deduplicate repeated coarse faces when several fine crossings resolve to the same adaptive leaf configuration.

Why this is acceptable for the first milestone:

- it keeps the implementation compact enough to ship now;
- it provides a measurable reduction path before more ambitious topology work;
- it is easier to debug with the existing console timings and AO workflow.

This area remains a likely follow-up target if we later want:

- more formal manifold guarantees;
- cleaner transition handling between very different leaf sizes;
- stronger guarantees against redundant coarse-face generation.

---

## Expected Benefits

If the first milestone works as intended, we should see:

- fewer generated triangles than uniform `dual_contouring`;
- fewer UV charts or at least fewer pathological tiny charts;
- less `xatlas ComputeCharts` pressure;
- lower end-to-end unwrap cost;
- potentially lower OBJ size and faster downstream ray setup.

The biggest expected win remains:

- reducing unnecessary tessellation on large flat brutalist masses.

---

## Known Risks

This milestone is intentionally useful before it is perfect.

Known risks:

- the simplified connectivity policy may still leave some redundant triangles in complex adaptive transitions;
- the flatness heuristic may need tuning if it merges noisy regions too aggressively or not aggressively enough;
- blocks near the scene bounds can still inherit the previously documented clipping sensitivity when geometry sits too close to the SDF domain limits.

That last point remains tracked separately in:

- `documentation/SDF_Bounds_Clipping_Deferred_Fix.md`

---

## Success Criteria

This milestone is successful if it produces a stable mesh and demonstrates a measurable reduction versus uniform `dual_contouring` on the sample scene.

Primary checks:

- generated triangle count;
- generated vertex count;
- UV unwrap success;
- `xatlas ComputeCharts` time;
- total `Build scene mesh` time;
- total pipeline wall time.

Secondary checks:

- visible silhouette stability;
- AO bake continuity;
- absence of obvious holes or winding failures.

---

## Deferred Follow-Up

After this milestone, the main follow-up options are:

- tune the leaf acceptance thresholds;
- add dedicated adaptive mesh statistics in CLI output;
- replace the simplified adaptive connectivity step with a more formal recursive contour pass;
- add feature-aware sharpness controls;
- consider a post-pass planar decimator only if the octree path still leaves too much tessellation.
