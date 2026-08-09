# Custom Harfang Transform Gizmo DLL Feasibility Study

Reviewed on August 9, 2026.

## 1. Objective

This note evaluates a revised direction:

- ignore ImGuizmo entirely;
- implement only the three required gizmos:
  - translate
  - rotate
  - scale
- package the result as a native Lua module distributed as a DLL;
- integrate it into Harfang without large Harfang architecture changes.

The local Harfang tree used for this review is:

- `C:\works\dev\harfang\harfang3d`

## 2. Updated Context

The new constraints materially change the recommendation.

### Confirmed local facts

- Harfang in the referenced tree uses Dear ImGui `1.87`.
- Harfang already exposes to Lua:
  - mouse and keyboard state
  - projection and unprojection helpers
  - `DrawLines`, `DrawTriangles`, `DrawModel`
  - `ImDrawList`
  - window and cursor screen-space helpers
  - view/framebuffer control
- Harfang Lua launcher preserves standard Lua `package` support and prepends `?.dll` to `package.cpath`.

### Functional scope

The target is not "generic editor widgets".

It is only:

- translation gizmo
- rotation gizmo
- scale gizmo

That smaller scope makes a custom implementation much more realistic.

## 3. Short Answer

Yes, a custom Lua plugin as a DLL is entirely conceivable.

With the updated constraints, it may actually be the better path.

The main reason is simple:

- integrating a modern ImGuizmo branch into an older Dear ImGui `1.87` stack is avoidable complexity;
- implementing only three Harfang-specific transform gizmos is a bounded engineering task;
- Harfang already exposes most of the math, input, and rendering surfaces needed to host such a tool.

## 4. Why the Recommendation Changes

In the first study, the main attraction of ImGuizmo was:

- reuse of existing transform interaction logic;
- reuse of Dear ImGui draw-list rendering.

That remains true in the abstract.

But two new facts shift the tradeoff:

1. Harfang is on an older Dear ImGui base.
2. The feature scope is narrow.

That means the cost of "make ImGuizmo fit this stack" rises, while the cost of "write exactly the three gizmos we need" falls.

For this specific use case, the second path becomes attractive.

## 5. Is a Native Lua DLL Module Viable?

Yes.

The Harfang Lua launcher already configures native module loading through `package.cpath`, with patterns that include:

- `?.dll`
- `loadall.dll`

It also preserves the standard Lua `package` table in the scripting environment.

Practically, that means a plugin can be loaded in the normal way:

```lua
local gizmo = require("hg_transform_gizmo")
```

This does not require any structural Harfang change by itself.

### Practical constraint

The DLL must be built against:

- the same Lua ABI used by the Harfang Lua runtime;
- compatible compiler/runtime settings;
- the same Harfang public headers if Harfang C++ types are passed directly across the module boundary.

That is a normal native-module constraint, not a Harfang-specific blocker.

## 6. Feasible Architectural Shapes

There are three realistic ways to structure the plugin.

## 6.1 Pure Harfang-aware DLL, no ImGui dependency in the core

This is the cleanest long-term architecture.

### Shape

- a native C++ core implements gizmo math, picking, state transitions, and delta generation;
- a Harfang adapter layer exposes the core to Lua;
- optional drawing backends are added on top.

### Benefits

- no dependency on ImGuizmo;
- no dependency on Dear ImGui internals for the core interaction logic;
- easier to keep deterministic and testable;
- easier to reuse later outside an ImGui viewport if needed.

### Recommendation

This is the best foundation.

## 6.2 Harfang-aware DLL with ImGui used only as a host surface

This is also good, and often the most practical editor-facing version.

### Shape

- the core remains independent from Dear ImGui;
- when embedded into an editor panel, the plugin draws into Harfang's exposed `ImDrawList`;
- viewport rect and cursor position come from the surrounding ImGui UI.

### Benefits

- good fit for an editor viewport inside an ImGui window;
- no dependency on ImGuizmo;
- avoids direct use of raw `ImGuiContext*` if the Lua side hands over only the draw list and screen rect.

### Recommendation

This is a strong rendering option for the first editor version.

## 6.3 Fully 3D Harfang-rendered gizmo

This means:

- draw axes, rings, and scale handles as world-space lines/triangles/models using `DrawLines`, `DrawTriangles`, or `DrawModel`;
- perform picking in C++;
- submit rendering through normal Harfang view IDs.

### Benefits

- independent from Dear ImGui visuals;
- can become depth-tested and scene-integrated;
- useful if the editor viewport eventually needs non-ImGui presentation.

### Drawback

- more rendering work than a 2D overlay approach;
- more care needed for per-view state, depth behavior, and anti-aliasing.

### Recommendation

Feasible, but not the fastest first version unless depth-tested 3D handles are a hard requirement.

## 7. Recommended Architecture

The best compromise is a hybrid:

1. core interaction and transform math in plain C++;
2. Harfang-facing Lua binding layer in the DLL;
3. editor rendering initially as screen-space overlay through Harfang's exposed `ImDrawList`;
4. optional later 3D rendering path for depth-tested handles.

This gives the cleanest path to a first usable editor tool without locking the entire design to Dear ImGui.

## 8. What the Plugin Actually Needs From Harfang

The good news is that the required surface is narrow.

### Required inputs

- current mouse state
- current keyboard state
- viewport rectangle in screen space
- camera view matrix
- camera projection matrix
- target transform matrix

### Required math helpers

These already exist in Harfang Lua and/or can be reproduced in the native side:

- matrix inverse
- screen-to-view unprojection
- view/projection composition
- projection to screen space
- axis extraction from matrices
- ray/plane and ray/axis distance math

### Required drawing support

At least one of these is already public:

- `ImDrawList`
- `DrawLines`
- `DrawTriangles`
- `DrawModel`

That is enough to render a custom gizmo without exposing renderer internals.

## 9. Feasibility by Gizmo Type

## 9.1 Translate

This is the easiest.

Required behavior:

- axis handles
- optional plane handles
- drag constrained to axis or plane

Interaction model:

- cast a camera ray from mouse position;
- test closest handle in projected or world space;
- when dragging, solve motion against:
  - axis-aligned line constraint
  - or plane constraint

This is a standard and well-bounded problem.

Feasibility: high.

## 9.2 Rotate

This is the hardest of the three, but still very manageable.

Required behavior:

- X/Y/Z rotation rings
- optional screen-space/free rotate later

Interaction model:

- project visible rings;
- pick nearest ring segment or solve against rotation plane;
- accumulate angle delta around the chosen axis.

The main difficulty is not math correctness in principle.

It is making the interaction feel stable near grazing angles and during camera-relative ring projection.

Still, for axis-only rotation, this remains a reasonable implementation target.

Feasibility: medium to high.

## 9.3 Scale

This is easier than rotation and close to translation.

Required behavior:

- axis scaling
- optional uniform center scale

Interaction model:

- same picking structure as translation;
- map drag distance to scalar delta along the chosen axis or uniform factor.

The main caution is avoiding inverted or unstable scale when crossing the pivot.

That is solvable with clamping and explicit sign rules.

Feasibility: high.

## 10. What "No Large Harfang Changes" Means in Practice

This revised direction works precisely because it does not need large Harfang changes.

### Very likely not required

- no renderer architecture rewrite
- no exposure of private scene structures
- no custom engine-side gizmo manager
- no raw Dear ImGui context bridge if the core does not depend on ImGui internals
- no special bgfx integration layer just for this plugin

### Reasonable optional additions

These are optional convenience additions only:

- a small helper to fetch current viewport dimensions if Lua-side code wants less boilerplate;
- a small helper to expose editor-specific camera rays if you want to centralize that in Harfang;
- possibly a helper to render to a chosen `ImDrawList` or view more directly.

None of these are structural requirements.

## 11. Rendering Strategy Options

## 11.1 First version: 2D overlay in viewport

Recommended first path:

- compute gizmo geometry in world space;
- project it into screen space;
- render with `ImDrawList` lines, triangles, circles, and filled shapes.

Why this is good:

- fast to iterate;
- visually crisp in editor viewports;
- minimal GPU-side complexity;
- independent from ImGuizmo;
- no need to synchronize with old Dear ImGui internals beyond ordinary draw-list usage.

## 11.2 Second version: world-space rendering

If you later need:

- depth-tested handles;
- occlusion behavior against scene geometry;
- stronger visual integration with the scene;

then add a world-space backend using:

- `DrawLines`
- `DrawTriangles`
- small reusable primitive models via `DrawModel`

This should be treated as an optional rendering backend, not as the first implementation requirement.

## 12. Recommended Plugin Boundary

The module should stay data-oriented.

The safest API shape is:

- input matrices in
- mouse/keyboard state in
- updated matrix out
- draw submission or draw data out
- a few status queries

Not:

- direct ownership of Harfang scene nodes
- hidden mutation of engine objects
- deep coupling to Harfang scene internals

Conceptually, the native surface should look more like:

```lua
local gizmo = require("hg_transform_gizmo")

gizmo.begin_frame(mouse, keyboard)
gizmo.set_viewport(x, y, w, h)

local changed
changed, matrix = gizmo.manipulate(
  view_matrix,
  projection_matrix,
  matrix,
  gizmo.OP_TRANSLATE,
  gizmo.MODE_LOCAL)

gizmo.draw_overlay()
```

or, if drawing is returned to Lua:

```lua
local changed, matrix, draw_data = gizmo.manipulate(...)
gizmo_renderer.draw(draw_data)
```

The first style is simpler for users.

The second style is cleaner if you want the core to stay renderer-agnostic.

## 13. Suggested Internal Split

Even if the plugin ships as one DLL, the code should still be split internally.

### Layer 1: core math and state

Own:

- handle definitions
- hover detection
- drag state
- translation/rotation/scale solving
- projection helpers
- snapping rules

Dependencies:

- ideally none beyond math primitives

### Layer 2: Harfang adapter

Own:

- conversion to/from `hg::Mat4`, `hg::MouseState`, `hg::KeyboardState`
- Lua binding surface
- optional direct Harfang render submission

Dependencies:

- Harfang public headers only

### Layer 3: optional UI/render backends

Own:

- ImDrawList overlay backend
- world-space `DrawLines` / `DrawTriangles` backend

This split keeps the core durable even if the editor presentation changes later.

## 14. Why This May Be Better Than ImGuizmo for This Project

Under the revised assumptions, the custom path has several concrete advantages.

### Advantage 1: no old-ImGui compatibility chase

You avoid:

- pinning a historical ImGuizmo snapshot;
- adapting upstream changes to Dear ImGui `1.87`;
- carrying compatibility glue for an old UI stack.

### Advantage 2: tighter functional scope

You implement only what the editor needs.

Not:

- view cube
- sequencer
- generic extras
- extra operation variants you may never use

### Advantage 3: cleaner Harfang fit

You can make the API match Harfang usage directly:

- Harfang matrices
- Harfang input states
- Harfang draw surfaces
- Harfang view IDs

### Advantage 4: less DLL-context complexity

If the gizmo core does not depend on raw Dear ImGui context ownership, the hardest cross-DLL problem from the first study largely disappears.

## 15. Main Risks

This path is feasible, but not free.

## 15.1 Interaction quality risk

The hardest part is not rendering.

It is making the gizmo feel:

- stable
- predictable
- low-jitter
- consistent across camera angles

This is most acute for rotation.

## 15.2 Scope creep risk

The project stays bounded only if the first version is strict about scope.

Recommended first-version scope:

- translate: axis + plane
- rotate: axis rings only
- scale: axis + uniform
- optional snapping
- overlay rendering only

Not:

- arbitrary pivot editing
- full screen-rotate manipulator
- bounds handles
- multi-selection transform
- depth-tested fancy shading

## 15.3 ABI/build discipline risk

Because this is a DLL Lua module, you must keep:

- matching Lua ABI
- matching compiler/runtime
- matching Harfang public headers if C++ types cross the boundary

That is manageable, but it should be treated as part of the design.

## 16. Recommended Implementation Plan

1. Build a small C++ core that supports only:
   - hover
   - drag
   - translate
2. Render it as a 2D overlay through Harfang `ImDrawList`.
3. Add scale.
4. Add axis-only rotation.
5. Add snapping.
6. Only after that, decide whether a world-space rendering backend is actually needed.

This order matters.

If translation is not crisp, adding rotation and scale early will only compound the debugging burden.

## 17. Final Recommendation

Yes, a custom Lua plugin as a DLL is viable.

Given:

- Harfang on Dear ImGui `1.87`;
- the requirement limited to translate, rotate, and scale;
- the desire to avoid large Harfang changes;

this custom path is arguably more suitable than integrating ImGuizmo.

My recommendation is:

- build a custom native module;
- keep the interaction core independent from Dear ImGui;
- use Harfang public APIs only;
- render the first version as a screen-space overlay in the editor viewport;
- keep world-space/depth-tested rendering as a second-phase enhancement.

That path is technically realistic, well-scoped, and much better aligned with the revised constraints than a forced ImGuizmo integration.

## 18. Source References

Local Harfang references used for this study:

- `C:\works\dev\harfang\harfang3d\extern\imgui\imgui.h`
- `C:\works\dev\harfang\harfang3d\harfang\engine\dear_imgui.h`
- `C:\works\dev\harfang\harfang3d\binding\bind_harfang.py`
- `C:\works\dev\harfang\harfang3d\languages\hg_lua\launcher.cpp`
- `C:\works\dev\harfang\harfang3d\harfang\script\lua_vm.cpp`
- `C:\works\dev\harfang\harfang3d\tutorials\mouse_scene_projection.lua`
