# SDF Bounds Clipping – Deferred Fix Note

## Status

Deferred on **August 6, 2026**.

The issue is known and currently tolerated because functional and aesthetic iteration are a higher priority than automating bounds management.

---

## Problem Summary

The current polygon generation pipeline evaluates the scene only inside a fixed SDF domain defined by `BuildSettings.bounds`.

If a box, a `box_cut`, or a `noise_displace_masked` modifier pushes useful geometry too close to that domain boundary, the generated surface can be:

- clipped;
- flattened against the domain edge;
- partially missing;
- visually misleading during look-development.

This is especially easy to trigger when:

- a tower is already near the outer scene bounds;
- a top or edge noise modifier expands silhouette variation outward;
- a later aesthetic pass increases modifier amplitude;
- a manually edited scene file grows beyond the original conservative bounds.

---

## Current Workaround

The current workaround is manual padding of scene bounds.

For the current `frame_006_blockout` demo scene, the bounds were intentionally expanded so the main masses and their active modifiers do not sit directly on the SDF clipping boundary.

This is acceptable for now because:

- the project is still in an exploratory stage;
- the scene format is still evolving;
- build time and memory are not yet under tight optimization pressure;
- manual control is sufficient for early artistic iteration.

---

## Why This Is Not the Final Solution

Manual bounds management has several weaknesses:

- it is easy to forget after editing scene content;
- it does not scale well once many sectors or scenes exist;
- it couples artistic edits to low-level meshing constraints;
- it can cause either clipping or unnecessary oversizing of the sampled domain;
- it becomes more fragile as modifier vocabulary grows.

In other words, the current workaround is safe enough for prototyping, but it should not remain the long-term production approach.

---

## Recommended Future Fix

When the project is more mature functionally and aesthetically, bounds should be derived automatically from scene content plus a conservative modifier-aware padding rule.

Recommended direction:

1. Compute base authored bounds from all boxes in the scene.
2. Expand those bounds by a per-box safety margin derived from active modifiers.
3. Aggregate the result into final scene build bounds.
4. Optionally allow manual override only when explicitly requested.

### Suggested Padding Heuristics

For a future first implementation, a pragmatic heuristic is enough:

- `box_cut` does not need outward padding by itself because it only removes matter inside its target box;
- `noise_displace_masked` should contribute outward padding proportional to its amplitude;
- an additional global safety epsilon should be added for meshing interpolation and numerical tolerance.

Conceptually:

```text
final_bounds = union_of_box_bounds
final_bounds += max_noise_amplitude_per_relevant_axis
final_bounds += meshing_safety_margin
```

This does not need to be perfect on day one. It only needs to be conservative and deterministic.

---

## Better Long-Term Version

Once the editor and build pipeline are more complete, the system could support:

- automatic bounds recomputation during scene load or build;
- per-sector bounds instead of one large global domain;
- warnings when authored content is too close to a computed boundary;
- optional debug visualization of scene bounds and padding;
- author-facing diagnostics that explain why a domain became larger.

That later version would be more appropriate once the project has:

- more scene files;
- more modifiers;
- stronger performance targets;
- a real authoring workflow around repeated rebuilds.

---

## Non-Goals For The Deferred Fix

This future bounds fix should **not** try to solve everything at once.

It should not initially include:

- aggressive optimization of sampled domain size;
- a full sector streaming policy rewrite;
- predictive clipping analysis for every possible future modifier type;
- automatic aesthetic decisions about how much empty margin is visually desirable.

The first goal is simply:

**avoid accidental geometry clipping without forcing the user to babysit bounds manually**.

---

## Acceptance Criteria For The Future Fix

The deferred fix can be considered successful when:

- adding or enlarging a tower does not silently clip it at the SDF boundary;
- increasing noise amplitude on edge or top masks does not require immediate manual bounds edits in normal cases;
- the build remains deterministic;
- generated geometry no longer depends on fragile hand-maintained scene bounds for ordinary authoring changes;
- manual bounds overrides remain possible for exceptional cases.

---

## Practical Reminder

Until this fix is implemented:

- any scene edit that pushes masses outward should trigger a quick review of `bounds`;
- any increase in noise amplitude should be treated as a potential clipping risk;
- any strange “cut flat at the edge” result should be investigated as a bounds issue first.

