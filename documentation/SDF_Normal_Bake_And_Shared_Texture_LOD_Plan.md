# SDF Normal Bake And Shared-Texture LOD Plan

## Status

Documented and first normal-bake implementation milestone delivered on **August 7, 2026**.

This note records two related decisions:

- how to restore high-frequency SDF detail on a lower-poly adaptive mesh;
- how shared-texture LODs should be approached later.

---

## Decision 1: Normal Map Bake From The Original SDF

The current low-poly adaptive mesh can become visually lighter than the original SDF scene.

That is expected.

Adaptive dual contouring reduces polygon budget by merging flatter regions, which is exactly what helped `xatlas` so much.

To recover some of the lost micro-shape detail without re-inflating triangle count, the chosen next step is:

- bake a tangent-space normal map from the original SDF onto the UV-unwrapped low-poly mesh.

### Why This Is The Right Next Step

This keeps the performance win where it matters:

- fewer triangles;
- faster unwrap;
- lighter ray scene;
- cheaper downstream asset handling.

At the same time, it restores some of the high-frequency visual information from:

- masked noise displacements;
- damaged top edges;
- local cuts and break-up;
- small shape variations that no longer deserve explicit geometry in the low-poly mesh.

---

## Implemented First Milestone

The first implementation bakes a **tangent-space normal map** directly from the scene SDF.

Current behavior:

- the low-poly mesh is unwrapped first;
- each covered texel is rasterized from the UV mesh;
- the texel position on the low-poly mesh is projected back toward the original SDF surface;
- the SDF normal is evaluated at that projected position;
- that world-space normal is converted into tangent space relative to the low-poly triangle basis;
- the final map is written as an RGB PNG.

This makes the normal bake independent from a separate dense high-poly mesh export.

That is important here because the original source of truth is the SDF scene itself, not a baked reference mesh.

### Current CLI Surface

The CLI now supports:

- `--bake-normal PATH`

The bake currently reuses the same UV atlas dimensions as the unwrap result, just like AO baking.

---

## Intentional Scope Boundaries

This first milestone does not try to solve every texture-bake problem at once.

Deferred for later:

- curvature bake from SDF;
- height or signed-distance texture bake;
- bent-normal bake;
- object-space normal output mode;
- per-material texture-set routing;
- combined ORM-style packing.

---

## Decision 2: Shared-Texture LODs Should Reuse The Hero UVs

The future LOD problem should **not** start by re-running a fresh unwrap for every lower-detail mesh.

That would introduce several problems:

- each LOD would need its own texture set;
- texture memory and export management would get more complex;
- texture continuity between LOD levels would become harder to maintain;
- bake iteration cost would rise quickly.

The preferred future direction is:

1. generate a hero mesh;
2. unwrap the hero mesh once;
3. bake AO and normal from the SDF once;
4. derive lower LOD meshes from that hero mesh;
5. preserve or transfer the hero UVs so all LODs sample the same texture atlas.

This is the right production direction because it separates concerns cleanly:

- the SDF defines the high-detail source shape;
- the hero mesh defines the baked texture domain;
- lower LODs become geometric simplifications of that textured hero asset.

---

## Practical Consequence For Future LOD Work

When the LOD milestone starts, the most relevant strategy is not:

- "generate a brand-new low-detail SDF mesh and unwrap it again"

but rather:

- "simplify the already-unwrapped hero mesh while preserving its UV parameterization as much as possible"

That future milestone should therefore study:

- UV-preserving mesh simplification;
- seam-aware simplification;
- error metrics that protect silhouette and baked-detail readability.

---

## Relationship With Adaptive Meshing

These two directions complement each other well:

- adaptive dual contouring removes geometry where geometry is wasteful;
- SDF normal baking restores appearance detail where texture is cheaper than geometry.

That combination is a strong fit for the current brutalist blockout workflow.

---

## Deferred Follow-Up

Follow-up items already worth tracking:

- expose more normal-bake knobs in CLI only if needed after real usage;
- evaluate whether the tangent-space bake should support OpenGL and DirectX green-channel conventions;
- study a chart-aware normal dilation strategy only if current dilation becomes visibly problematic;
- implement UV-preserving shared-texture LOD generation as a separate milestone.
