# SDF Surface Pack Texture Plan

## Status

Documented and first implementation milestone delivered on **August 7, 2026**.

The current stochastic-thickness revision was updated on **August 8, 2026**.

This note records the decision to add a compact packed texture alongside the tangent-space normal map.

---

## Decision

The project now supports a single packed PNG with the following channel layout:

- `R`: ambient occlusion;
- `G`: curvature;
- `B`: thickness.

This packed texture is intended as a compact geometric-information map generated directly from the UV-unwrapped mesh and the original SDF scene.

---

## Why This Layout

This keeps three useful grayscale signals in one texture fetch:

- AO is still useful for preview lighting, stylization, masking, or material blending;
- curvature is useful for edge wear, dirt masks, and hard-surface accentuation;
- thickness is useful for transmission, dirt accumulation heuristics, and material variation.

The normal map stays separate because it is vector data and does not belong in the same scalar-signal pack.

---

## Implementation Direction

The packed texture does **not** re-raytrace AO a second time.

The chosen flow is:

1. bake AO once;
2. keep the AO image in memory;
3. bake curvature and thickness from the original SDF onto the same UV atlas;
4. assemble the final packed PNG as `R=AO`, `G=curvature`, `B=thickness`.

This avoids duplicate AO cost while keeping the packed texture aligned with the same unwrap domain.

---

## Current Signal Definitions

### Ambient Occlusion

AO remains the existing raytraced bake already used elsewhere in the toolchain.

### Curvature

Curvature is currently estimated from SDF normal variation around each projected texel position.

In practice:

- the UV texel is projected back to the SDF surface;
- the SDF normal is evaluated at the center;
- nearby samples are taken in the local tangent frame;
- the variation of neighboring normals becomes the curvature intensity.

This is intentionally an artist-friendly scalar mask, not a physically strict principal-curvature solver.

### Thickness

Thickness is currently estimated with a **single stochastic inward ray per texel**.

In practice:

- the UV texel is projected back to the SDF surface;
- one inward direction is sampled in a cone around `-normal`;
- the direction is deterministic for a given texel and seed, so the bake remains reproducible;
- the ray marches through the solid until it exits or reaches the configured maximum distance;
- the resulting noisy field is filtered in UV space per chart to recover a stable large-mass signal.

The current direction pattern is a stable low-clumping pseudo-random distribution, meant as a practical blue-noise-like compromise rather than strict white noise.

#### Decision Record

On **August 8, 2026**, the project explicitly moved away from the earlier deterministic multi-ray cone evaluation for thickness.

The deterministic cone was more regular, but in practice it tended to produce values that felt too flat and too conservative for this kind of large-mass scalar signal.

The chosen replacement is:

- one stochastic inward ray per texel;
- deterministic per-texel seeding for reproducibility;
- UV-space chart-aware filtering after the trace pass.

This was kept because it gave more interesting large-volume variation while also reducing the raw tracing cost compared with firing several rays per texel.

For this project, the thickness channel is treated as an approximate artistic mass indicator rather than a high-precision physical measurement.

Default maximum distance:

- `5.0` world units.

The exported blue channel is normalized to `[0, 1]` over that distance range.

---

## Scope Boundaries

This first surface-pack milestone intentionally does not include:

- bent normals;
- cavity as a separate channel;
- signed convex versus concave curvature separation;
- multi-bounce thickness or volumetric transport;
- per-material channel packing policies.

Those can be added later if the current pack proves useful in production.

---

## Follow-Up Items

Items already worth tracking for later:

- expose curvature tuning in the CLI only if real production usage requires it;
- evaluate whether the thickness filter should become user-configurable in the CLI;
- study additional packing presets if future materials need different signal groupings;
- keep the pack aligned with shared-texture LOD work instead of duplicating texture sets.
