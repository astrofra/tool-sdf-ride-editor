# Procedural Brutalist Ride Editor – Technical Specification

## Purpose

The goal of this tool is to build a dedicated scene editor for a large-scale demoscene production.

The editor is **not** intended to be a generic level editor. It is specifically designed around a single artistic constraint:

- the camera moves along a mostly straight trajectory;
- the environment is therefore essentially organized along a single longitudinal axis;
- the objective is to author a very large environment efficiently while keeping complete artistic control.

The editor is implemented inside the HARFANG ecosystem using Lua.

---

# Design Philosophy

The workflow must be entirely **non-destructive**.

The user must be able to:

- add primitives;
- remove primitives;
- move them freely;
- regenerate geometry at any time.

The source representation always remains procedural.

The generated geometry is considered a cache built from this procedural description.

---

# World Representation

The environment is primarily composed of **signed distance fields (SDFs)**.

The basic primitive is a **parallelepiped (box)**.

The artistic direction is intentionally:

- brutalist;
- architectural;
- massive;
- ruined;
- concrete-oriented.

Objects are positioned manually inside the editor.

The scene consists of large towers and architectural volumes placed one after another along the camera path.

---

# Distance Field Deformation

Each SDF primitive can be perturbed using procedural noise.

The objective is to avoid perfectly clean geometry and instead obtain:

- damaged concrete;
- irregular silhouettes;
- eroded surfaces;
- abandoned / urbex aesthetics.

---

# Editor Views

The editor should provide multiple visualization modes, including:

- first-person camera;
- top view;
- left three-quarter view;
- right three-quarter view.

Since the camera path is reversible, navigation must support both forward and backward movement.

---

# Preview Modes

Two display modes are required.

## Lightweight mode

Only simplified boxes are displayed.

This mode is intended for fast editing.

## Full mode

All procedural geometry is generated and displayed.

This mode represents the final appearance of the scene.

---

# Geometry Generation

The SDF description generates polygonal meshes.

The generated meshes may be exported as OBJ files.

Geometry generation is entirely procedural.

---

# UV Generation

Generated meshes require automatic UV unwrapping.

The intended solution is to rely on Microsoft's C++ UV generation library.

No manual UV editing is expected.

---

# Baking Pipeline

After geometry generation, several attributes should be baked automatically.

Candidate baked information includes:

- ambient occlusion;
- curvature / edge intensity;
- cavity information (or another useful geometric attribute).

The goal is to store these attributes inside a compact multi-channel texture.

These baked textures are then reused by the rendering shaders.

---

# Material System

The renderer combines traditional material textures (concrete, stone, etc.) with the baked information.

The baked maps are used to:

- damage materials;
- accentuate edges;
- introduce variation;
- enrich the procedural appearance.

---

# Architectural Details

The procedural system should support recurring architectural features such as:

- openings;
- windows;
- split blocks;
- damaged concrete;
- exposed reinforcing bars.

These elements may themselves rely on distance fields.

---

# Ground Generation

The environment includes a procedural ground made of combinations such as:

- bare earth;
- concrete slabs;
- broken pavement;
- irregular arrangements.

The objective is to avoid repetitive patterns while keeping manual artistic control.

---

# Manual Artistic Additions

The editor must also allow manual placement of custom scene instances.

These instances are intended for unique handcrafted assets that complement the procedural environment.

The editor should also allow manual placement of light sources.

---

# Scene Streaming

The world is divided into sectors.

Generated geometry is split into blocks.

As the camera moves:

- nearby blocks are inserted into the scene graph;
- distant blocks are removed.

This maintains a relatively constant polygon budget throughout the ride.

The approach is conceptually inspired by Nanite, although intentionally much simpler.

Memory usage is not considered a primary constraint.

---

# Level of Detail

A simple automatic LOD system may be used.

As an object approaches the camera, a higher-detail version is selected.

Since geometry originates from distance fields, LOD meshes can be generated automatically.

Only a limited number of LOD levels is required.

---

# Rendering Pipeline

The renderer is custom.

The final compositing stage has access to the depth buffer.

This depth information is intended to drive a procedural fog solution.

A compute shader may perform a raymarch using the depth buffer in order to produce volumetric-looking fog that naturally fills recesses and architectural cavities.

---

# Lighting

The rendering pipeline supports up to seven point lights.

The objective is to obtain rich lighting without introducing a deferred rendering pipeline.

Lighting remains manually authored.

---

# Technology Stack

- HARFANG engine
- Lua editor
- Procedural Signed Distance Fields
- Automatic mesh generation
- Automatic UV unwrapping
- Automatic baking
- OBJ export
- Sector-based scene streaming
- Automatic LOD generation
- Custom rendering pipeline
- Compute shader based compositing
- Depth-buffer driven procedural fog

## Technology Resources

- https://github.com/ands/lightmapper
- https://github.com/Microsoft/UVAtlas
- https://github.com/jpcy/xatlas
- https://github.com/Thekla/thekla_atlas

---

# Overall Objective

The editor is a purpose-built authoring tool for creating a very large procedural brutalist environment for a demoscene production.

It combines procedural generation with manual artistic direction.

The emphasis is on:

- fast iteration;
- non-destructive editing;
- large-scale environments;
- strong artistic control;
- efficient rendering suitable for a real-time PC demo.
