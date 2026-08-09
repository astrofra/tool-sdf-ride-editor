`app/sdf-worlds/` contains master world documents for the SDF editor.

Each `.sdfworld` file references one or more `.sdfscene` cell files from `app/sdf-scenes/` and provides their world-space placement.

Objects inside each cell remain local to that cell. The world document owns only world-level placement and cell-level editor state.

Current convention:

- shared cell size defaults to `100.0` meters at the world-document level;
- shared overlap padding defaults to `10.0` meters on each side;
- the resulting effective envelope is `120.0` meters per axis;
- new cells should start with default local scene bounds `(-60, -10, -60)` to `(60, 110, 60)`;
- generated mesh clipping is still controlled by each `.sdfscene` file's `bounds`, not by `.sdfworld` `cell_size`.
