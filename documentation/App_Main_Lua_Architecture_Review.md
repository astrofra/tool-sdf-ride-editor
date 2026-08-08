# `app/main.lua` Architecture Review

Reviewed on August 8, 2026.

## 1. Scope

This note reviews `app/main.lua` only, with one question in mind:

- is there already enough structure and responsibility overlap to justify factorization, isolation, or code separation?

Short answer:

- yes;
- not because the file is unmanageable today, but because it already contains several distinct subsystems that will become harder to evolve once real editor features are added.

This recommendation is consistent with the Lua-side preferences already stated in `documentation/Procedural_Brutalist_Ride_Editor_Lua_Cpp_Roadmap.md`:

- keep the Lua layer procedural;
- prefer explicit state tables;
- use small flat functions instead of introducing heavy abstractions.

## 2. Current Responsibility Map

`app/main.lua` currently mixes five different concerns:

1. App configuration and mutable runtime state
   - `app/main.lua:3-34`
2. Shared math and geometry helpers
   - `app/main.lua:36-320`
3. Editor gizmo creation and update
   - grid: `app/main.lua:322-355`
   - measurement overlay geometry: `app/main.lua:357-443`
4. HARFANG bootstrap and resource loading
   - `app/main.lua:446-516`
5. Per-frame update, UI, camera drive, layout, and rendering
   - `app/main.lua:518-665`

The file still works because the feature set is narrow, but the module boundaries are already visible inside the code.

## 3. Main Findings

## 3.1 Hidden mutable state is already spreading

Several values are mutated at file scope:

- `window_width`, `window_height`
- `camera_drive`, `camera_z_velocity`
- `grid_center`
- `scene_origin`

That is still acceptable for a prototype, but it creates implicit coupling between:

- UI code;
- camera motion;
- grid placement;
- measurement rendering;
- future scene/document state.

The next editor feature will likely need to touch the same shared locals, which is usually the point where `main.lua` starts turning into a permanent god file.

## 3.2 Feature logic and engine lifecycle are interleaved

The block at `app/main.lua:446-516` owns engine startup, resources, scene setup, camera creation, and lighting.

The block at `app/main.lua:518-665` then mixes:

- input collection;
- ImGui window layout;
- camera controller behavior;
- camera matrix rebuild;
- grid snapping;
- measurement label layout;
- world submission;
- text overlay rendering;
- frame presentation.

This is the strongest signal that `main.lua` should become a composition file rather than a feature file.

## 3.3 The measurement overlay is already a standalone subsystem

Everything below is part of one coherent feature:

- screen clipping: `app/main.lua:150-195`
- screen-to-plane projection: `app/main.lua:197-228`
- visible segment computation: `app/main.lua:230-301`
- label transform computation: `app/main.lua:303-320`
- node creation/update: `app/main.lua:357-443`
- per-frame layout and draw: `app/main.lua:577-653`

That is already enough surface area to justify a dedicated module.

If this stays inside `main.lua`, every future overlay feature will tend to follow the same pattern and accumulate in the same place.

## 3.4 UI, controller logic, and rendering are mixed in the frame loop

The camera transport UI and the camera motion logic are tightly coupled today:

- ImGui input: `app/main.lua:531-545`
- smoothing and velocity update: `app/main.lua:547-558`
- transform write-back: `app/main.lua:560-561`

That is not wrong, but it makes the frame loop carry both presentation and behavior details.

Once keyboard shortcuts, mouse drag, scene orbit, or selection controls are added, this section will grow quickly.

## 3.5 Constants are flat instead of grouped by concern

The top of the file mixes:

- visual theme values;
- grid dimensions;
- measurement layout values;
- camera drive parameters;
- global scene anchors.

The values are readable, but not structured. That makes it harder to:

- see what belongs to which subsystem;
- override settings per feature;
- pass configuration explicitly into extracted modules.

## 4. Recommended Split

The right move is a small procedural split, not an OOP rewrite.

Recommended target shape:

```text
app/
  main.lua
  editor/
    runtime.lua
    camera_transport.lua
    grid.lua
    measurement_overlay.lua
    config.lua            # optional, but useful once constants keep growing
```

### `main.lua`

Keep only:

- top-level composition;
- module wiring;
- the frame loop in a short, readable form.

Target responsibility:

```lua
local runtime = require("editor.runtime")
local camera_transport = require("editor.camera_transport")
local grid = require("editor.grid")
local measurement_overlay = require("editor.measurement_overlay")

local app = runtime.create()

while runtime.is_running(app) do
  local frame = runtime.begin_frame(app)
  camera_transport.update(app, frame)
  grid.update(app, frame)
  measurement_overlay.update(app, frame)
  runtime.render(app, frame)
end

runtime.shutdown(app)
```

### `editor/runtime.lua`

Own:

- HARFANG init/shutdown;
- window creation;
- pipeline/resources/program/font loading;
- scene creation;
- light setup;
- frame begin/end helpers;
- resize/reset handling.

Important point:

- `assets_compiled`, shader paths, font paths, and view ids should live here, not leak into feature modules.

### `editor/camera_transport.lua`

Own:

- `camera_drive`;
- `camera_z_velocity`;
- the ImGui transport widget;
- smoothing parameters;
- writing the updated camera transform.

This isolates a controller that will almost certainly grow when the editor gets real navigation controls.

### `editor/grid.lua`

Own:

- grid configuration;
- `snap_to_step`;
- `snap_grid_center`;
- grid node creation;
- grid node transform updates.

This keeps the grid as an editor gizmo layer, separate from future authored scene content.

### `editor/measurement_overlay.lua`

Own:

- clipping and projection helpers;
- visible segment computation;
- label placement;
- backdrop and arrow node creation/update;
- label text rendering.

This is the cleanest extraction candidate because it is already feature-complete and internally coherent.

### `editor/config.lua` (optional but sensible)

Own grouped tables such as:

```lua
return {
  window = {
    width = 1280,
    height = 720,
    title = "SDF Ride Editor"
  },
  theme = {
    background = {24, 28, 34},
    gamma = 2.2
  },
  grid = {
    half_extent = 25,
    spacing = 1,
    line_thickness = 0.025
  },
  camera_transport = {
    translation_speed = 16.0,
    velocity_response = 7.0,
    slider_return_speed = 8.0
  }
}
```

This should stay simple. The goal is grouping, not configurability theater.

## 5. State Shape Recommendation

Before or during extraction, introduce explicit state tables instead of relying on many file-scope locals.

Minimal example:

```lua
local state = {
  window = {
    width = 1280,
    height = 720
  },
  scene = {
    origin = hg.Vec3(0, 0, 0)
  },
  camera = {
    position = hg.Vec3(18, 16, -18),
    drive = 0.0,
    z_velocity = 0.0
  },
  grid = {
    center = hg.Vec3(0, 0, 0)
  }
}
```

This buys two things immediately:

- less hidden coupling;
- much easier module extraction without changing behavior.

## 6. Recommended Extraction Order

The safest order is:

1. Extract `measurement_overlay.lua`
   - highest cohesion, lowest ambiguity
2. Extract `camera_transport.lua`
   - clean separation between UI/controller behavior and frame orchestration
3. Extract `runtime.lua`
   - centralize engine lifecycle and loaded resources
4. Group constants into `config.lua`
   - do this once the feature modules exist, so the grouping follows actual ownership

This order avoids creating a generic utility layer too early.

## 7. What Not To Do

I would explicitly avoid these moves for now:

- no class-heavy Lua rewrite;
- no ECS;
- no generic `utils.lua` dumping ground for unrelated helpers;
- no deep abstraction over HARFANG resource objects;
- no attempt to predict every future editor feature with a big framework.

The project already has the right design bias:

- procedural Lua;
- explicit state;
- small modules;
- narrow responsibilities.

The split should reinforce that, not replace it.

## 8. Why This Matters Now

Today the file is still small enough that refactoring is cheap.

That will change as soon as `main.lua` also starts owning:

- selection/manipulation tools;
- scene document state;
- undo/redo;
- bridge calls into `sdf-generator`;
- build progress reporting;
- asset or artifact inspection.

At that point, keeping grid, measurement, runtime bootstrap, and camera transport inside the same file would create unnecessary friction.

## 9. Final Recommendation

Yes, there is already real material to factorize in `app/main.lua`.

The best split is a pragmatic one:

- keep `main.lua` as the composition shell;
- isolate HARFANG lifecycle;
- isolate camera transport;
- isolate editor gizmos by feature, especially the measurement overlay;
- move toward explicit state tables instead of adding more file-scope mutable locals.

I would not treat this as an urgent rewrite, but I would treat it as the right next cleanup before the Lua editor starts integrating actual authoring and generation workflows.
