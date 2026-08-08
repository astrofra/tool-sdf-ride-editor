@echo off
setlocal
pushd "%~dp0"
set ERR=0

call "%~dp0build_debug.bat"
if errorlevel 1 goto :fail

if not exist "%~dp0artifacts\generated" mkdir "%~dp0artifacts\generated"

"%~dp0build\vs2022-debug\bin\Debug\sdf_cli.exe" ^
  --scene "%~dp0scenes\frame_006_blockout.sdfscene" ^
  --out "%~dp0artifacts\generated\frame_006_blockout_adaptive_unwrapped.obj" ^
  --meshing-mode adaptive_dual_contouring ^
  --adaptive-normal-dot-threshold 0.9925 ^
  --adaptive-plane-error-ratio 0.02 ^
  --adaptive-min-plane-error-cells 0.35 ^
  --unwrap-uvs ^
  --uv-resolution 2048 ^
  --uv-padding 8 ^
  --bake-ao "%~dp0artifacts\generated\frame_006_blockout_adaptive_ao.png" ^
  --bake-ao-samples 128 ^
  --bake-ao-min-samples 4 ^
  --bake-ao-error-threshold 0.03 ^
  --bake-ao-max-distance 25.0 ^
  --bake-ao-denoise-passes 2 ^
  --bake-ao-denoise-radius 2 ^
  --bake-normal "%~dp0artifacts\generated\frame_006_blockout_adaptive_normal.png" ^
  --bake-surface-pack "%~dp0artifacts\generated\frame_006_blockout_adaptive_surface_pack.png" ^
  --surface-thickness-max-distance 25.0
if errorlevel 1 goto :fail

goto :done

:fail
set ERR=%errorlevel%

:done
pause
popd
exit /b %ERR%
