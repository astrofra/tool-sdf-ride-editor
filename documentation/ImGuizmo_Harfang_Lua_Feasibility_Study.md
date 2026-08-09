# ImGuizmo / Harfang Lua Feasibility Study

Reviewed on August 9, 2026.

## 1. Objective

This note evaluates whether it is technically reasonable to:

- fork or wrap [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo);
- expose it to Lua with FabGen, in the same general style as Harfang Lua bindings;
- use Harfang input state and Harfang/bgfx rendering paths;
- keep the integration compatible with a two-DLL setup;
- avoid large architecture changes or exposing private Harfang structures as public API.

The specific target is an editor-style gizmo workflow inside a Harfang Lua application.

## 2. Short Answer

Yes, this is feasible.

But the feasible version is not:

- "make ImGuizmo a separate bgfx renderer with direct Harfang object access";
- nor "share arbitrary Harfang engine objects between two Lua modules".

The feasible version is:

- keep ImGuizmo on its natural side of the fence, which is Dear ImGui;
- let Harfang continue to own input collection and bgfx submission;
- expose a thin Lua API around ImGuizmo operations;
- share only a small set of public, stable types across bindings;
- add one or two very small Harfang bridge entry points if the two-DLL split is mandatory.

In other words:

- a light Harfang-oriented ImGuizmo bridge is realistic;
- a heavy rendering fork is possible, but it is the wrong first architecture.

## 3. Main Technical Observation

ImGuizmo is not a standalone renderer.

Its normal model is:

1. Dear ImGui owns the frame and input state.
2. ImGuizmo reads the active `ImGuiContext` and `ImGuiIO`.
3. ImGuizmo writes draw commands into an `ImDrawList`.
4. The host Dear ImGui backend performs the actual GPU submission.

That matters because it changes the question from:

- "can ImGuizmo draw directly into Harfang/bgfx?"

to:

- "can Harfang's Dear ImGui path host ImGuizmo draw commands and submit them through its existing bgfx renderer?"

That second question is much easier, and the answer is yes in principle.

## 4. What Already Exists in the Current Repo

From the local Harfang Lua binding and examples already present in this repository:

- Harfang Lua already exposes `MouseState`, `KeyboardState`, `ReadMouse`, and `ReadKeyboard`.
- Harfang Lua already exposes `ImGuiInit`, `ImGuiInitContext`, `ImGuiBeginFrame`, and `ImGuiEndFrame`.
- Harfang Lua already exposes `DearImguiContext` as a public Lua-side class.
- Harfang Lua already exposes `ImDrawList` and `ImGuiGetWindowDrawList()`.
- Harfang Lua already exposes viewport/window helpers such as `ImGuiGetWindowPos()`, `ImGuiGetWindowSize()`, `ImGuiGetCursorScreenPos()`, and `ImGuiGetContentRegionAvail()`.
- Harfang Lua already exposes `bgfx::FrameBufferHandle`, `SetViewFrameBuffer`, `SetViewRect`, and `SetViewTransform`.
- The repo already uses the standard Harfang ImGui render path in `app/main.lua`.

This is a strong baseline.

It means the missing pieces are relatively narrow:

- one ImGuizmo binding layer;
- one clean way to synchronize ImGuizmo's active Dear ImGui context in a two-DLL setup;
- possibly one small helper if framebuffer-targeted ImGui rendering needs a stricter API.

## 5. Feasible Integration Models

## 5.1 Recommended model: ImGuizmo as a Harfang ImGui extension

This is the recommended architecture.

### How it works

- Harfang creates and owns the Dear ImGui context.
- Harfang reads mouse/keyboard and feeds them into `hg.ImGuiBeginFrame(...)`.
- Lua builds the editor window or viewport UI.
- The ImGuizmo bridge is called during the same ImGui frame.
- ImGuizmo writes into the current draw list.
- Harfang renders the ImGui frame with `hg.ImGuiEndFrame(view_id)`.

### Why this is good

- It follows ImGuizmo's intended architecture.
- It avoids reimplementing ImGuizmo's rendering in bgfx primitives.
- It reuses Harfang's existing ImGui shaders and bgfx submission path.
- It keeps the Lua API simple and matrix-oriented.
- It minimizes Harfang-side API growth.

### What this means in practice

The gizmo DLL does not need direct Harfang scene access.

It mainly needs:

- a current Dear ImGui context;
- a viewport rectangle in screen space;
- view and projection matrices;
- the matrix being edited;
- optional snap/bounds parameters.

That is a much narrower problem than sharing scene, node, transform, or renderer internals.

## 5.2 Acceptable model: separate DLL, but still ImGui-backed

This is feasible if the two-DLL split is important.

In that model:

- `harfang.dll` owns Harfang and its Lua bindings;
- `imguizmo_hg.dll` owns the ImGuizmo Lua bindings;
- both modules attach to the same Lua VM;
- shared public types are linked through FabGen extern/shared type support;
- ImGuizmo still renders through Dear ImGui draw lists, not through a custom bgfx layer.

This is still a reasonable design, but it has one real extra constraint:

- the gizmo DLL must be able to set its own active `ImGuiContext` to Harfang's current one.

That is the main technical seam.

## 5.3 High-risk model: custom bgfx renderer for ImGuizmo

This is the version I do not recommend as a first implementation.

It would require:

- bypassing or forking ImGuizmo's normal `ImDrawList` output path;
- replacing draw-list based rendering with direct bgfx submission;
- deciding how to preserve hit testing, clipping, and draw order;
- deciding whether the gizmo is a screen-space overlay or a depth-tested 3D pass;
- maintaining a Harfang-specific renderer fork of an upstream ImGui tool.

This is much more work and much less aligned with the upstream library.

If the real goal is "editor overlay gizmos", this extra complexity buys very little.

## 6. Input Integration Feasibility

This part is straightforward.

ImGuizmo relies on Dear ImGui input state, not on a custom engine input API.

So the correct flow is:

1. Harfang reads mouse and keyboard.
2. Harfang feeds them into `hg.ImGuiBeginFrame(...)`.
3. ImGuizmo reads `ImGuiIO` from the current ImGui context.

That means:

- no direct Harfang-to-ImGuizmo input adapter is required;
- no private Harfang input structure needs to be exposed;
- existing Harfang Lua input bindings are already enough.

This is one of the strongest reasons the project is feasible without major engine surgery.

## 7. Framebuffer / bgfx Integration Feasibility

This is feasible, with one important clarification.

### Clarification

ImGuizmo itself does not naturally "draw into a bgfx framebuffer" as an engine-level render pass.

What it naturally does is:

- append ImGui draw commands;
- let the ImGui backend submit them later.

So the practical rendering question becomes:

- can Harfang's ImGui backend submit its view to the desired bgfx view/framebuffer?

From the current public Lua surface, that appears plausible because:

- Harfang exposes `SetViewFrameBuffer(view_id, handle)`;
- Harfang exposes `ImGuiEndFrame(view_id)`;
- Harfang already treats ImGui rendering as a view-based submission.

This part is an inference from the visible API shape and examples, not a direct confirmation from Harfang native source.

### Practical consequences

For a normal desktop editor overlay, the likely path is:

- draw scene first;
- draw ImGui and ImGuizmo as overlay afterward on a chosen view.

For an offscreen editor viewport, there are two options:

1. Render the scene to a texture/framebuffer, show that texture inside an ImGui window, and draw ImGuizmo over the image inside that same ImGui window.
2. Route the ImGui render view itself to a chosen framebuffer/view if Harfang's backend supports it as expected.

Option 1 is usually simpler and often removes the need for any gizmo-specific framebuffer API.

## 8. Two-DLL Boundary: What Can Be Shared Safely

The key rule is:

- share values and opaque handles;
- do not share broad engine internals.

### Low-risk shared types

These are good candidates for shared/bound extern types:

- `hg::Mat4`
- `hg::Vec2`
- `hg::Vec3`
- `bgfx::ViewId`
- `bgfx::FrameBufferHandle`
- `hg::MouseState`
- `hg::KeyboardState`

These are all small, public-facing concepts that fit a data-oriented gizmo API.

### Medium-risk shared types

These are feasible, but require tighter ABI discipline:

- `hg::DearImguiContext`
- `ImDrawList`
- raw `ImGuiContext*`

They are closely tied to Dear ImGui ABI and version matching.

They should be treated as opaque bridge types, not general-purpose public scripting objects.

### Types I would avoid sharing across modules

- scene internals
- private renderer structures
- pipeline implementation objects
- arbitrary node/component internals
- STL-heavy ownership-bearing engine objects unless already part of a stable public binding

The gizmo does not need them.

## 9. FabGen Fit

FabGen is a good fit for this job.

Why:

- it already underpins Harfang's binding workflow;
- it supports Lua output;
- it supports route helpers and input/output argument patterns;
- it explicitly supports linking shared C++ types across bindings through extern type support.

That last point is especially relevant for a separate ImGuizmo Lua module that must understand Harfang-exposed types.

So from a binding-generator perspective, the answer is yes:

- this is aligned with how FabGen is intended to be used.

## 10. The Real Hard Part

The real hard part is not matrices, input, or even draw submission.

It is Dear ImGui context ownership across DLL boundaries.

ImGuizmo already includes `SetImGuiContext(ImGuiContext* ctx)`, which is a strong signal that upstream expected DLL-split integration scenarios.

That helps a lot.

However, in the current visible Harfang Lua API:

- `DearImguiContext` is exposed;
- but a raw `ImGuiContext*` accessor is not visible.

So if the architecture really must stay "two native modules", one small bridge is still needed.

## 11. Minimal Harfang API Additions I Would Recommend

If the goal is to keep the gizmo in a separate DLL, I would add as little as possible.

### Required bridge

One opaque access path to the raw Dear ImGui context:

- either `intptr_t` / `void*` returned from a function such as `GetImGuiContextHandle(ctx)`;
- or an equivalent bridge entry point designed specifically for the gizmo module.

This does not require exposing a private struct layout.

It only exposes an opaque handle.

### Optional bridge

Only if Harfang's ImGui framebuffer routing turns out not to be sufficient:

- a small helper to render the active ImGui frame to an explicit framebuffer/view combination.

This may not be necessary, but it is a reasonable escape hatch.

### What I would not add

- no public exposure of private Harfang renderer structs;
- no public scene-internal gizmo hooks;
- no custom Harfang input duplication just for ImGuizmo.

## 12. Recommended Lua API Shape

The Lua API should stay matrix-centric.

That means the gizmo module should manipulate values like:

- operation
- mode
- rectangle
- view matrix
- projection matrix
- object/world matrix
- optional snapping/bounds

Not:

- scene nodes
- cameras
- transforms by hidden reference
- editor state by side effect

Conceptually, the correct usage shape is closer to:

```lua
local gizmo = require("imguizmo_hg")

-- Harfang side
hg.ImGuiBeginFrame(w, h, dt, mouse, keyboard)

if hg.ImGuiBegin("Viewport") then
  local pos = hg.ImGuiGetCursorScreenPos()
  local size = hg.ImGuiGetContentRegionAvail()

  gizmo.SetCurrentContext(native_imgui_context_handle)
  gizmo.SetRect(pos.x, pos.y, size.x, size.y)

  local changed
  changed, object_matrix = gizmo.Manipulate(
    view_matrix,
    projection_matrix,
    gizmo.UNIVERSAL,
    gizmo.LOCAL,
    object_matrix)
end
hg.ImGuiEnd()

hg.ImGuiEndFrame(view_id)
```

That keeps ownership and control flow obvious.

## 13. Why a Full Fork Is Probably Not the Right First Move

A full fork is only justified if you need Harfang-specific rendering behavior that upstream ImGuizmo fundamentally cannot host.

For the initial editor use case, I do not think that is true.

What is probably enough is:

- a thin Harfang-oriented binding layer;
- maybe a very small packaging fork;
- maybe a few tiny helper functions;
- but not a deep renderer rewrite.

That is the important distinction:

- a source fork for integration convenience is fine;
- a rendering architecture fork should be avoided unless proven necessary.

## 14. Important Limits and Risks

## 14.1 ABI/version matching

If Harfang and the gizmo DLL do not build against the exact same Dear ImGui revision and compatible compiler settings, the two-DLL bridge becomes fragile.

This is the single biggest implementation risk.

## 14.2 Depth-tested gizmos are a different project

Standard ImGuizmo is an editor overlay tool.

If you want:

- true depth-tested world-pass gizmo geometry;
- complex occlusion against scene depth;
- custom 3D shading of handles as native engine geometry;

then the work is substantially larger.

That is no longer just "bind ImGuizmo to Lua".

## 14.3 Framebuffer path still needs one native confirmation

The public Harfang API strongly suggests that view/framebuffer routing should work for ImGui submission.

But this should still be confirmed against the Harfang native implementation before locking the final API.

## 14.4 Do not over-share engine objects

The more Harfang-native objects the gizmo DLL starts accepting, the more brittle the architecture becomes.

A matrix tool should remain a matrix tool.

## 15. Recommended Implementation Order

1. Do not start from a deep fork of ImGuizmo.
2. First build a thin native bridge that exposes only:
   - `BeginFrame`
   - `SetImGuiContext`
   - `SetRect`
   - `SetDrawlist` if truly needed
   - `Manipulate`
   - `IsOver`
   - `IsUsing`
   - `Enable`
3. Keep the Lua API purely data-oriented around matrices and viewport rectangles.
4. Add exactly one Harfang bridge for raw ImGui context handoff if the DLL split is kept.
5. Validate the normal editor overlay path on the main backbuffer first.
6. Only then validate custom framebuffer routing.
7. Only consider deeper rendering changes if a proven requirement remains unsolved.

## 16. Final Recommendation

This integration is feasible, and reasonably so, if it is framed correctly.

My recommendation is:

- yes to a Harfang Lua ImGuizmo bridge;
- yes to FabGen for the binding layer;
- yes to a two-DLL setup if you keep the cross-module surface narrow;
- no to sharing broad Harfang runtime objects;
- no to a first-pass custom bgfx renderer fork of ImGuizmo.

The clean target architecture is:

- Harfang owns input, ImGui frame lifecycle, and bgfx submission;
- ImGuizmo owns matrix editing logic and ImGui draw-list emission;
- Lua owns editor behavior and object selection;
- the bridge between the two stays small and public-API-safe.

Under that model, the project is not only possible, but fairly well aligned with the APIs that already exist.

## 17. Source References

External:

- ImGuizmo repository: <https://github.com/CedricGuillemet/ImGuizmo>
- ImGuizmo header and API notes: <https://raw.githubusercontent.com/CedricGuillemet/ImGuizmo/refs/heads/master/src/ImGuizmo.h>
- ImGuizmo implementation: <https://raw.githubusercontent.com/CedricGuillemet/ImGuizmo/refs/heads/master/src/ImGuizmo.cpp>
- FABGen README: <https://raw.githubusercontent.com/ejulien/FABGen/master/readme.md>

Internal repo references:

- `app/documentation/api/bind_harfang.py`
- `app/documentation/tutorials-hg2/imgui_mouse_capture.lua`
- `app/documentation/tutorials-hg2/scene_vr_teleporter.lua`
- `app/main.lua`
