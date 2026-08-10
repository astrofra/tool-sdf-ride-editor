# SDF Cell Edit Commit / History Decision

## Purpose

This note records the technical decision taken once cell-content editing became operational.

The editor now has enough CRUD and transform features that manual "Apply" workflows would become fragile and would delay a proper undo model.

The goal is therefore:

- commit each valid cell-content edit immediately;
- save the edited `.sdfscene` immediately;
- keep an internal undo/redo history of committed edits;
- keep this history deterministic and decoupled from preview nodes.

## Decision

Cell-content editing now follows an **auto-apply + auto-save** model.

Concretely:

- every valid box edit is committed immediately;
- every committed edit rewrites the active cell `.sdfscene`;
- the active cell preview is rebuilt from the authoritative saved state;
- undo/redo works from explicit committed snapshots, not from preview-node deltas.

This applies to:

- `Add Box`
- `Duplicate Box`
- `Delete Box`
- selected-box field edits (`name`, `op`, local translation, `half_size`)
- translation nudges and snap

## Scope

This history currently covers **cell-content edits only**.

It does **not** currently try to cover world-topology edits such as:

- `Add Cell`
- `Delete Cell`
- future cell move / reorder operations

When the world topology changes, the cell-edit history is cleared on purpose.

Reason:

- history entries target one concrete cell scene file;
- world-level changes can invalidate that targeting;
- clearing is safer than replaying stale cell references.

## Snapshot Strategy

Each committed edit stores:

- the target cell identity (`cell_name`, `scene_path`);
- the committed action kind;
- a target runtime box id for merge decisions;
- the full `.sdfscene` text before the edit;
- the full `.sdfscene` text after the edit;
- the session-local runtime box-id list before and after;
- the selected-box index before and after.

Undo writes back the `before` snapshot.

Redo writes back the `after` snapshot.

Then the editor:

1. reloads the `.sdfscene`;
2. restores the runtime id list for the session;
3. restores selection;
4. rebuilds the active cell preview.

This keeps preview state derived and disposable.

## Runtime Box Identity

Box runtime ids are **session-local only**.

They are not serialized into `.sdfscene` files.

They exist only to answer one question:

- can two consecutive committed edits be merged into one undo step?

This avoids coupling undo logic to mutable properties such as box names.

## Merge Policy

Two consecutive edits merge only when they target the same:

- cell;
- runtime box id;
- action kind.

Examples:

- translate the same box twice: merge;
- resize the same box twice: merge;
- rename the same box twice: merge;
- translate then resize the same box: do not merge;
- edit two different boxes: do not merge.

This keeps the history compact without hiding semantically different operations.

## GUI Consequences

The inspector no longer needs a manual `Apply Box` button.

The intended behavior is:

- valid edits save immediately;
- temporary invalid text can remain in the inspector while the field is active;
- when the field loses focus while still invalid, the edit is rejected, logged, and the inspector is resynced from the saved box state.

This preserves continuous editing without giving up deterministic saved state.

## Relationship To The Content Editing Plan

This decision supersedes the earlier "undo/redo later" assumption from the first content-editing plan.

Reason:

- save semantics;
- box CRUD;
- numeric editing;
- and undo granularity

are now the same architectural concern.

It is cheaper to stabilize them together now than to retrofit history after more editing features are added.
