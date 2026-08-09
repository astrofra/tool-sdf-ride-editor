# Reusable Pure-Lua Harfang Transform Gizmo Study

Reviewed on August 9, 2026.

## 1. Objective

This note evaluates a third direction:

- `translate`, `rotate`, and `scale` only;
- no depth-tested rendering;
- Lua only;
- implemented with a reasonable mix of OOP and procedural style;
- reusable in any Harfang Lua project with minimal adaptation.

The target is not a one-off editor script.

The target is a small Lua package that can be dropped into another Harfang Lua project and used as a generic transform gizmo layer.

## 2. Short Answer

Yes, this is feasible.

Under the revised constraints, a pure-Lua implementation is not only possible, but probably the simplest portable solution.

That is true because:

- the feature scope is narrow;
- Harfang Lua already exposes the required input and projection helpers;
- no depth-tested rendering removes the hardest rendering-specific part;
- a reusable module can stay independent from project-specific native code, build scripts, or DLL packaging.

## 3. Main Recommendation

The recommended architecture is:

- pure Lua package;
- stateful `GizmoController` objects;
- procedural helper modules for math, projection, picking, and drawing;
- overlay rendering only;
- no dependency on project-private Harfang modifications.

In short:

- OOP for runtime state and public API;
- procedural style for the stateless heavy lifting.

That is the right balance for a reusable Harfang Lua tool.

## 4. Why Lua-Only Is Reasonable Here

If this were:

- a full editor framework;
- depth-tested 3D handles;
- complex mesh submission;
- or large batches of scene tools;

then native code would become much more attractive.

But for:

- one gizmo at a time;
- a few handles;
- one active drag session;
- simple projection and picking logic;
- overlay-only rendering;

Lua is a reasonable implementation language.

The per-frame work is small enough that clarity and portability matter more than squeezing out native-level micro-performance.

## 5. Confirmed Harfang Lua Surface That Makes This Possible

From the local Harfang tree at `C:\works\dev\harfang\harfang3d`, the relevant Lua surface already exists.

### Input

- `hg.ReadMouse`
- `hg.ReadKeyboard`

### Projection and unprojection

- `hg.ComputePerspectiveProjectionMatrix`
- `hg.ProjectToScreenSpace`
- `hg.UnprojectFromScreenSpace`

### Overlay-friendly rendering

- `hg.DrawLines`
- `ImDrawList`
- `hg.ImGuiGetWindowDrawList`
- `hg.ImGuiGetWindowPos`
- `hg.ImGuiGetCursorScreenPos`
- `hg.ImGuiGetContentRegionAvail`

This is enough to build a portable pure-Lua gizmo without engine changes.

## 6. Best Reusable Architecture

The package should be organized as a small library, not as one monolithic script.

Recommended shape:

```text
gizmo/
  init.lua
  controller.lua
  config.lua
  enums.lua
  state.lua
  math3d.lua
  project.lua
  pick.lua
  draw_overlay.lua
  draw_imgui.lua
  ops/
    translate.lua
    rotate.lua
    scale.lua
```

### Public API layer

- `init.lua`
- `controller.lua`

### Procedural core helpers

- `math3d.lua`
- `project.lua`
- `pick.lua`
- `ops/*.lua`

### Rendering adapters

- `draw_overlay.lua`
- `draw_imgui.lua`

This keeps the package reusable and host-agnostic.

## 7. Recommended Mix of OOP and Procedural Style

The clean split is:

### OOP responsibilities

Use objects for:

- persistent gizmo state
- active mode
- active operation
- hover state
- drag state
- snapping configuration
- theme configuration
- backend selection

That points naturally to one main object:

- `GizmoController`

Optional secondary object:

- `ViewportContext`

### Procedural responsibilities

Use stateless functions for:

- matrix and vector helpers
- world-to-screen projection
- screen-to-ray conversion
- axis/ring hit tests
- drag delta solving
- handle geometry generation
- screen-space overlay drawing primitives

This avoids turning math and picking into class-heavy Lua.

### Why this balance is good

Pure procedural style becomes awkward once hover and drag state need to persist across frames.

Pure OOP style becomes noisy if every vector projection or ray/plane solve is buried inside methods.

The mixed approach is better:

- object at the top;
- procedural modules underneath.

## 8. Recommended Public API Shape

The public API should stay small and data-oriented.

Conceptually:

```lua
local gizmo = require("gizmo")

local controller = gizmo.new{
  renderer = "imgui",
  theme = gizmo.DefaultTheme,
  snap = {
    translate = 1.0,
    rotate = 15.0,
    scale = 0.1
  }
}

controller:begin_frame{
  mouse = hg.ReadMouse(),
  keyboard = hg.ReadKeyboard(),
  view = view_mtx,
  proj = proj_mtx,
  viewport = {x = x, y = y, w = w, h = h}
}

local changed, matrix = controller:manipulate{
  matrix = object_matrix,
  operation = gizmo.Translate,
  mode = gizmo.Local
}

controller:draw()
```

This shape has three important properties:

- no hidden scene ownership;
- no dependency on project-global state;
- one controller can be reused per viewport or per tool context.

## 9. Rendering Strategy

Because depth-tested rendering is out of scope, the package should default to overlay rendering.

There are two realistic overlay paths.

## 9.1 Recommended default: ImGui draw-list overlay

This is the most reusable editor-oriented path.

### Why

- no custom shader asset required;
- crisp 2D overlay drawing;
- natural integration with an editor viewport inside an ImGui window;
- no depth-state management;
- no native bridge required.

### Best use case

- project already uses Harfang ImGui;
- gizmo lives inside an editor viewport panel.

### Limitation

This backend assumes the host project has an ImGui frame active.

## 9.2 Optional fallback: Harfang line/triangle overlay view

This is useful if a project does not use ImGui.

### Why

- still pure Lua;
- still project-independent at the code level;
- usable in presentation tools or custom runtimes.

### Limitation

This backend will likely require:

- one small bundled line shader path or equivalent project-side shader setup.

That makes it slightly less drop-in than the ImGui backend.

### Recommendation

Support it as an optional backend, not as the default.

## 10. Viewport Model for Reuse

The package should not assume:

- full-window rendering;
- one global viewport;
- or fixed editor layout.

Instead, every frame should receive an explicit viewport rectangle:

- `x`
- `y`
- `w`
- `h`

This is critical for reuse across projects.

It allows the same package to work in:

- full-screen scene viewers;
- split views;
- docked editor panels;
- offscreen preview panels shown in ImGui.

## 11. Feasibility by Operation

## 11.1 Translate

This is highly feasible in pure Lua.

Recommended first-version behavior:

- X/Y/Z axis translation
- XY/YZ/ZX plane translation

Implementation outline:

- derive camera ray from mouse position;
- compute projected handle visibility;
- pick nearest axis or plane handle;
- solve constrained movement from ray against:
  - line for axis movement
  - plane for plane movement

This is the easiest operation and should be implemented first.

## 11.2 Rotate

This is the hardest operation, but still feasible in Lua.

Recommended first-version behavior:

- axis rotation rings only
- no screen-space free-rotate in version one

Implementation outline:

- define ring plane per axis;
- intersect ray with ring plane;
- derive angle around axis from projected hit point;
- accumulate signed delta against drag start reference.

The most important quality risk here is stability near grazing camera angles.

That should be handled by:

- rejecting unstable ring picks;
- fading or disabling back-facing or nearly edge-on rings;
- preferring axis-only rings first.

## 11.3 Scale

This is also highly feasible in pure Lua.

Recommended first-version behavior:

- X/Y/Z axis scale
- one uniform center scale handle

Implementation outline:

- reuse axis picking logic from translate;
- map drag distance to scale delta;
- clamp crossing behavior to avoid unstable negative scales unless negative scale is explicitly supported.

## 12. No Depth Test: What This Changes

This constraint simplifies the project considerably.

### Benefits

- handles are always visible;
- no scene-depth sampling or occlusion logic;
- no per-backend depth-state tuning;
- no need for world-space mesh handles in version one.

### Tradeoff

The gizmo is an editor overlay, not a scene-embedded object.

That means:

- handles may appear on top of geometry even when the object is visually behind something;
- occlusion cues must come from styling, not depth.

This is acceptable for a reusable editor package.

## 13. Recommended Visual Model

For an overlay-only reusable package, the visual language should stay simple.

Recommended styling:

- red, green, blue axis colors
- yellow or white hover highlight
- semi-transparent inactive plane handles
- slightly thicker active handle stroke
- optional screen-space pivot marker

Recommended interaction feedback:

- hover color shift
- active color shift
- subtle ghost preview line during drag
- optional numeric delta text near the pivot or cursor

This is enough for usability without a complex rendering system.

## 14. Reuse Across Other Harfang Lua Projects

To make the package genuinely reusable, it should avoid project-specific assumptions.

### Avoid

- hardcoded asset paths
- hardcoded editor globals
- direct scene-node ownership
- assumptions about one camera variable name
- assumptions about one render loop structure

### Prefer

- explicit per-frame inputs
- explicit viewport rectangle
- explicit camera matrices
- explicit object matrix
- optional host callbacks

That allows the same package to be copied into another project and wired in with very little adaptation.

## 15. Recommended Host Integration Pattern

The cleanest host contract is:

1. host owns scene and selection;
2. host computes or already has camera matrices;
3. host calls gizmo each frame with the selected matrix;
4. gizmo returns:
   - updated matrix
   - change flag
   - optional metadata
5. host applies the matrix back to its own scene representation.

This keeps the gizmo package generic.

It should never become the owner of scene nodes.

## 16. Suggested Internal State Shape

The main controller can keep explicit state like:

```lua
{
  config = {...},
  theme = {...},
  frame = {
    mouse = nil,
    keyboard = nil,
    view = nil,
    proj = nil,
    viewport = nil
  },
  interaction = {
    hovered_handle = nil,
    active_handle = nil,
    drag_origin = nil,
    drag_reference_matrix = nil,
    drag_reference_hit = nil
  }
}
```

This is more maintainable than scattering hidden locals across modules.

## 17. Suggested Module Responsibilities

### `controller.lua`

Own:

- controller construction
- frame state intake
- operation dispatch
- drag lifecycle
- renderer dispatch

### `math3d.lua`

Own:

- matrix helpers
- axis extraction
- angle helpers
- scalar clamping
- basic geometric solves

### `project.lua`

Own:

- screen-to-ray
- world-to-screen
- viewport conversions

### `pick.lua`

Own:

- axis hit tests
- plane hit tests
- ring hit tests
- hover scoring

### `ops/translate.lua`

Own:

- translate-specific hit zones
- translate drag solving

### `ops/rotate.lua`

Own:

- rotate-specific hit zones
- ring angle solving

### `ops/scale.lua`

Own:

- scale-specific hit zones
- uniform and axis scale solving

### `draw_imgui.lua`

Own:

- rendering through `ImDrawList`

### `draw_overlay.lua`

Own:

- optional rendering through Harfang overlay views

## 18. Risks

## 18.1 Rotation feel

This remains the main usability risk.

The pure-Lua path is still feasible, but rotation quality will decide whether the package feels professional or merely functional.

## 18.2 Backend drift

If both ImGui and non-ImGui renderers are added too early, maintenance cost rises.

Recommendation:

- ship the ImGui backend first;
- add the fallback backend second.

## 18.3 Over-abstracting the package

Because the goal is reuse, there is a temptation to design a mini-framework.

That would be a mistake.

The package should stay narrow:

- one controller object;
- a handful of helper modules;
- no generalized editor toolkit ambitions.

## 19. Recommended Delivery Order

1. Build pure-Lua controller and procedural helpers.
2. Implement translate first.
3. Add ImGui draw-list renderer.
4. Add scale.
5. Add axis-only rotation.
6. Add snapping.
7. Only then decide whether the non-ImGui overlay backend is needed.

This sequence keeps the package shippable early.

## 20. Final Recommendation

Yes, a reusable pure-Lua Harfang transform gizmo package is feasible.

For the stated scope, it is probably the most portable and least intrusive option.

The right design is:

- pure Lua;
- no depth-tested rendering;
- one stateful controller object;
- procedural math/picking/render helper modules;
- default ImGui draw-list overlay backend;
- explicit inputs and no hidden scene ownership.

That combination gives the best balance of:

- reuse across projects;
- low integration cost;
- acceptable implementation effort;
- maintainable Lua code style.

## 21. Source References

Local Harfang references used for this study:

- `C:\works\dev\harfang\harfang3d\binding\bind_harfang.py`
- `C:\works\dev\harfang\harfang3d\docs\api\lua\functions.html`
- `C:\works\dev\harfang\harfang3d\extern\imgui\imgui.h`
- `C:\works\dev\harfang\harfang3d\tutorials\mouse_scene_projection.lua`
