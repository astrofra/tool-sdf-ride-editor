# SDF World Document Decision

## Decision

The editor will use a two-level authoring model:

- each `cell` is stored as its own `.sdfscene` file;
- a separate master `world document` references those scene files and places them in world space.

This means:

- SDF objects inside a cell live in the cell's local coordinate system;
- the master world document owns the world-space placement of each cell;
- the editor can keep one active cell for safe editing while still showing adjacent cells as context.

## Why

This split matches the editor goals better than storing everything in one global scene file.

It gives:

- local editing that does not accidentally damage neighboring cells;
- simple snapping of authored content inside a cell;
- simple relocation of an entire cell in the world;
- a clean future path for streaming, LOD, and selective rebuilds.

## V1 Constraints

The first implementation keeps the world transform of a cell intentionally narrow:

- translation only;
- no free rotation for cells;
- cell placement is expected to snap to a global grid;
- object placement inside a cell is expected to snap in local cell space.

This is a deliberate restriction to avoid broken seams, ambiguous selection, and unstable streaming behavior too early.

## Data Ownership

### Cell `.sdfscene`

Each cell scene owns:

- local SDF boxes;
- local modifiers;
- local build settings;
- local object positions.

### Master `.sdfworld`

The master world document owns:

- the list of cells;
- the path to each `.sdfscene`;
- the world translation of each cell;
- the active cell selection;
- world-level authoring metadata such as shared cell size.

For the current editor prototype, the default shared cell size is **100.0 meters**.
The default cross-cell overlap padding is **10.0 meters** on each side.
That gives a shared effective build envelope of **120.0 meters** per axis.

This overlap exists to allow local scene content to slightly cross a nominal cell boundary and still meet adjacent cells cleanly.

At this stage, that `cell_size` value is a world-layout and editor convention.
The same is true for the overlap padding.
These values do **not** yet clip or crop generated geometry by themselves.
Actual SDF mesh generation is still bounded by each cell scene's own `.sdfscene` `bounds`.
For now, the editor uses the world policy mainly as shared metadata and as a bounds-span diagnostic.

When a new cell scene is created, the editor should initialize its local `.sdfscene` bounds automatically from that world policy, while still leaving those scene bounds editable on a per-cell basis.

## Initial File Format

The first world document format is a simple line-based text file:

```text
world <name>
cell_size <value>
cell_bounds_padding <value>
active_cell <cell_name>
cell <cell_name> <scene_path> <tx> <ty> <tz>
```

Example:

```text
world default_blockout_world
cell_size 100.0
cell_bounds_padding 10.0
active_cell tile_000
cell tile_000 sdf-scenes/tile_000.sdfscene 0 0 0
```

## Editor Implications

The editor should treat the master world document as the top-level state.

That top-level state is responsible for:

- choosing the active cell;
- deciding whether inactive neighbor cells are visible;
- preparing future per-cell selection, snapping, rebuild, and streaming workflows.

## Default New-Cell Bounds

For a newly created cell, the default local `.sdfscene` bounds should be:

```text
min = (-60, -10, -60)
max = ( 60, 110,  60)
```

This comes from:

- a nominal cell body of `100 x 100 x 100` meters;
- `10` meters of extra overlap padding on each side;
- a local convention where the cell is centered around its origin in `X/Z`;
- a local convention where nominal ground sits at `Y = 0`.

Those initial bounds are only a default template.
They remain configurable per cell scene when a specific case needs a different envelope.
