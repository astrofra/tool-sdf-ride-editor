# tool-sdf-ride-editor
Simple editor based on SDF, demo-oriented.

Authoring `.sdfscene` documents live under `app/sdf-scenes/` and are not treated as HARFANG source assets.
Generated meshes and textures emitted by the SDF pipeline belong under `app/sdf-output/`.
The Lua runtime module `sdf-generator.dll` is deployed to `app/bin/hg_lua-win-x64/` so `require("sdf-generator")` resolves through the bundled HARFANG Lua runtime.
Developer-only validation scripts live under `app/tools/`.
