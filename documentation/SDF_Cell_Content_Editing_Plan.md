# SDF Cell Content Editing Plan

## Purpose

This document defines the next editor slice after world-level cell creation, deletion, placement, and preview.

The target here is narrower:

- edit the **content of one active cell** safely;
- keep neighboring cells visible as context only;
- preserve the existing two-level ownership model:
  - `.sdfworld` owns cell placement in world space;
  - each `.sdfscene` owns its local authored SDF content.

This plan is intentionally focused on the first usable authoring workflow, not on a final full-featured DCC-style editor.

## Current Baseline

At the time of writing, the editor already has:

- a master `.sdfworld` document with per-cell world placement;
- one active cell selection;
- cell add/delete workflows;
- flat-shaded and wireframe box preview;
- camera orbit, pan, zoom, and placement cursor support;
- a log panel for editor feedback.

What is still missing is the local authoring loop inside a cell:

- select a box;
- inspect it;
- move it;
- resize it;
- add/remove/duplicate it;
- save the modified `.sdfscene`;
- refresh the local preview safely.

## Core Decision

The first content-editing implementation should stay strictly **cell-local**.

That means:

- only the active cell can be edited;
- neighboring cells may remain visible, but they are read-only context;
- all authored box transforms are edited in the active cell's local coordinate system;
- saving content edits rewrites only the active cell's `.sdfscene`;
- the `.sdfworld` file should remain untouched during ordinary box editing.

This is the safest way to avoid accidental cross-cell damage while the editor is still simple.

## Non-Goals For The First Slice

The initial cell-content workflow should **not** try to solve everything at once.

Out of scope for the first useful version:

- multi-selection;
- arbitrary box rotation;
- hierarchy editing inside the cell;
- modifier editing UI;
- full generated mesh rebuild pipeline;
- final gizmo polish;
- bulk scene validation tools;
- cross-cell simultaneous editing.

Note:

- undo/redo is now covered by [`documentation/SDF_Cell_Edit_Commit_History_Decision.md`](./SDF_Cell_Edit_Commit_History_Decision.md) because the editor reached the point where save semantics and edit granularity had to be stabilized together.

Those can come later once the first local box workflow is stable.

## Editing Invariants

The implementation should keep these rules explicit.

### 1. Active-cell isolation

- picking must only select boxes from the active cell;
- inactive cells must never become editable by mistake;
- destructive actions apply only to the selected box inside the active cell.

### 2. Local-space editing

- box translation is stored relative to the active cell origin;
- box size is stored as local `half_size`;
- snapping should happen in local cell space, not in global world space.

### 3. Preview as a derived view

- the live box preview is not the source of truth;
- the source of truth remains `cell_state.scene_file`;
- after each committed edit, the preview for the active cell should be rebuilt from that source state.

### 4. Save semantics

- the editor should save the active `.sdfscene` deterministically;
- failures must leave the in-memory state explicit and visible in the log;
- world document save must not be part of the normal box-edit path.

## Recommended Implementation Order

The correct order is to start with selection and only then add manipulation.

### Phase 1. Active-cell box picking

Goal:

- click in the 3D view and select one box from the active cell.

Recommended behavior:

- cast a camera ray from the mouse;
- intersect against the active cell preview boxes only;
- choose the nearest hit;
- clear selection when clicking empty space.

Recommended data shape:

```lua
state.selection = {
  active_box_index = nil
}
```

Expected output:

- one selected box index in the active cell;
- one clear visual response in the viewport;
- one log entry describing the selection.

### Phase 2. Visual selection state

Goal:

- make selection obvious before adding any editing operation.

Recommended behavior:

- selected box gets a distinct preview color;
- wireframe mode also highlights the selected box;
- cell switching clears or remaps selection safely;
- deleting a selected box clears selection.

Expected output:

- the user always knows which box is currently targeted.

### Phase 3. Minimal inspector

Goal:

- expose the selected box fields in a small operational GUI.

Recommended editable fields:

- `name`
- `op` (`add` / `subtract`)
- local translation
- `half_size`

Recommended constraints:

- keep direct numeric editing simple;
- clamp half sizes to strictly positive values;
- validate name collisions before save.

Expected output:

- the selected box can be inspected and changed without yet relying on a transform gizmo.

### Phase 4. Commit and preview refresh

Goal:

- make local edits persistent and visible immediately.

Recommended behavior:

- modify `active_cell.scene_file.scene.boxes`;
- save the active `.sdfscene`;
- rebuild preview nodes for the active cell only;
- preserve active cell and selection state if possible.

This phase matters more than fancy manipulation UX.
Without a clean save/refresh loop, the rest of the editor remains fragile.

### Phase 5. Box CRUD

Goal:

- support the basic authoring loop.

Minimum operations:

- `Add Box`
- `Duplicate Box`
- `Delete Box`

Recommended defaults for `Add Box`:

- spawn near the current local origin or near the current selection;
- use one reasonable default size;
- assign an auto-generated unique name;
- select the new box immediately.

Recommended `Duplicate Box` behavior:

- copy the selected box;
- offset it slightly in local space;
- assign a new unique name;
- select the duplicate.

Expected output:

- the user can construct and prune local blockout content without leaving the editor.

### Phase 6. Local translation workflow

Goal:

- move a selected box in the active cell.

Recommended first version:

- numeric fields and step buttons are acceptable;
- direct viewport dragging may come after the state model is proven.

Recommended snap policy:

- snap translation in local space;
- keep the snap step explicit and shared;
- do not mix cell-grid placement logic with local box snap logic.

This is the point where a gizmo can become useful, but it should not be a prerequisite for the first stable edit loop.

### Phase 7. Local resize workflow

Goal:

- resize the selected box by editing `half_size`.

Recommended first version:

- numeric editing in the inspector;
- optional axis-specific grow/shrink buttons;
- no center-offset tricks in v1.

The key rule is:

- keep the transform model explicit;
- avoid hidden pivots or implicit resizing modes too early.

## Suggested Module Split

The Lua implementation should stay procedural and explicit.

A reasonable split is:

- `editor/sdf_selection.lua`
  - picking
  - nearest-hit logic
  - selection state updates

- `editor/sdf_cell_editor.lua`
  - selected-box CRUD
  - field edits
  - validation
  - save operations

- `editor/sdf_preview.lua`
  - preview rebuild for one cell
  - selected-box highlight policy

- `editor/sdf_scene.lua`
  - high-level orchestration only
  - active-cell ownership
  - GUI routing between world-level and cell-level tools

This keeps selection, mutation, and preview refresh from collapsing into one growing file.

## Picking Strategy Recommendation

The first picking implementation should use the existing box model directly.

Recommended approach:

1. build a world-space AABB for each box in the active cell preview;
2. cast a ray from the camera through the mouse cursor;
3. intersect against those AABBs;
4. keep the nearest positive hit.

Why this is the right first step:

- current boxes are axis-aligned;
- the preview already reflects authored boxes closely;
- this avoids premature dependency on a full gizmo layer;
- it is easy to debug and log.

If rotation is introduced later, this can evolve toward OBB picking.

## Save / Refresh Strategy

The first version should prefer correctness over micro-optimization.

Recommended mutation flow:

1. mutate the active cell `scene_file` in memory;
2. validate names and sizes;
3. save the `.sdfscene`;
4. destroy active-cell preview nodes;
5. rebuild active-cell preview nodes from the saved in-memory state;
6. restore selection if the target still exists.

This keeps the preview deterministic and avoids hidden divergence between runtime nodes and authored data.

The current implementation refines this further:

1. validate the edit;
2. commit it immediately;
3. save the `.sdfscene` immediately;
4. capture before/after history snapshots;
5. rebuild the active cell preview from saved state.

## Logging Expectations

The log panel should remain the main feedback channel.

Recommended log events:

- active box selected;
- selection cleared;
- box added;
- box duplicated;
- box deleted;
- box save succeeded;
- box save failed;
- invalid size rejected;
- duplicate name rejected.

The operational GUI should stay concise.
The log should carry the verbose editor narration.

## First Milestone Definition

The first meaningful cell-content milestone is:

- select one box in the active cell;
- highlight it;
- delete it;
- save the `.sdfscene`;
- rebuild the active preview.

Why this is the right first milestone:

- it proves picking;
- it proves active-cell isolation;
- it proves mutation of local scene data;
- it proves save/reload correctness;
- it proves preview refresh after destructive change.

If this works cleanly, the rest of box editing becomes straightforward extension work.

## Second Milestone Definition

The next milestone after that should be:

- add a box;
- duplicate a box;
- edit local translation and `half_size`;
- save and refresh;
- keep selection stable.

At that point, the editor becomes practically usable for local blockout.

## Relationship To Future Gizmo Work

A transform gizmo can still become the preferred interaction layer later.

But the project should not skip directly to gizmo work before the following are stable:

- active-cell-only selection;
- selected-box ownership;
- mutation of the authoritative `.sdfscene` state;
- deterministic save/rebuild behavior.

The gizmo should sit on top of a correct authoring model, not define it.

## Final Recommendation

The next implementation slice should be intentionally small:

1. active-cell box picking;
2. selected-box highlight;
3. selected-box delete;
4. save and preview rebuild.

Then extend to:

5. add box;
6. duplicate box;
7. local translation editing;
8. local size editing.

This keeps the editor aligned with the roadmap's non-destructive philosophy while respecting the world/cell ownership model already established.
