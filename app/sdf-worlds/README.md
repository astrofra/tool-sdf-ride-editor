`app/sdf-worlds/` contains master world documents for the SDF editor.

Each `.sdfworld` file references one or more `.sdfscene` cell files from `app/sdf-scenes/` and provides their world-space placement.

Objects inside each cell remain local to that cell. The world document owns only world-level placement and cell-level editor state.
