local hg = require("harfang")
local runtime = require("editor.runtime")
local gizmos = require("editor.gizmos")

local app = runtime.create()

local ok, err = xpcall(function()
  gizmos.attach(app)

  local translation_state = app.gizmos.translation
  local axis_bundle = translation_state.axis_nodes.x
  local plane_bundle = translation_state.plane_nodes.xy

  assert(hg.GetMaterialBlendMode(axis_bundle.materials.xray_base) == hg.BM_Alpha, "expected axis X-ray base blend mode alpha")
  assert(hg.GetMaterialDepthTest(axis_bundle.materials.xray_base) == hg.DT_Greater, "expected axis X-ray base depth test greater")
  assert(hg.GetMaterialWriteZ(axis_bundle.materials.xray_base) == false, "expected axis X-ray base write z disabled")

  assert(hg.GetMaterialBlendMode(plane_bundle.materials.xray_active) == hg.BM_Alpha, "expected plane X-ray active blend mode alpha")
  assert(hg.GetMaterialDepthTest(plane_bundle.materials.xray_active) == hg.DT_Greater, "expected plane X-ray active depth test greater")
  assert(hg.GetMaterialWriteZ(plane_bundle.materials.xray_active) == false, "expected plane X-ray active write z disabled")
  assert(hg.GetMaterialFaceCulling(plane_bundle.materials.xray_active) == hg.FC_Disabled, "expected plane X-ray active culling disabled")

  print("gizmo xray material smoke test passed")
end, debug.traceback)

runtime.shutdown(app)

if not ok then
  error(err)
end
