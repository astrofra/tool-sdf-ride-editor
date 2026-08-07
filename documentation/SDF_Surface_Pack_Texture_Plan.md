# SDF Surface Pack Texture Plan

## Status

Documented and first implementation milestone delivered on **August 7, 2026**.

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

Thickness is currently estimated by marching from the surface inward along the opposite of the SDF normal until the ray exits the solid or hits a configurable maximum distance.

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
- evaluate whether thickness should optionally use a denoised or smoothed variant;
- study additional packing presets if future materials need different signal groupings;
- keep the pack aligned with shared-texture LOD work instead of duplicating texture sets.
