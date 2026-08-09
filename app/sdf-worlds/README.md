`app/sdf-worlds/` contains master world documents for the SDF editor.

Each `.sdfworld` file references one or more `.sdfscene` cell files from `app/sdf-scenes/` and provides their world-space placement.

Objects inside each cell remain local to that cell. The world document owns only world-level placement and cell-level editor state.

Current convention:

- shared cell size defaults to `100.0` meters at the world-document level;
- generated mesh clipping is still controlled by each `.sdfscene` file's `bounds`, not by `.sdfworld` `cell_size`.
