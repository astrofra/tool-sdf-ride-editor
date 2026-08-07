@echo off
setlocal
pushd "%~dp0"

call "%~dp0build_debug.bat"
if errorlevel 1 goto :fail

if not exist "%~dp0artifacts\generated" mkdir "%~dp0artifacts\generated"

"%~dp0build\vs2022-debug\bin\Debug\sdf_cli.exe" ^
  --scene "%~dp0scenes\frame_006_blockout.sdfscene" ^
  --out "%~dp0artifacts\generated\frame_006_blockout_unwrapped.obj" ^
  --unwrap-uvs ^
  --uv-resolution 1024 ^
  --uv-padding 8 ^
  --bake-ao "%~dp0artifacts\generated\frame_006_blockout_ao.png" ^
  --bake-ao-samples 128 ^
  --bake-ao-min-samples 4 ^
  --bake-ao-error-threshold 0.03 ^
  --bake-ao-max-distance 25.0 ^
  --bake-ao-denoise-passes 2 ^
  --bake-ao-denoise-radius 4
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set ERR=%errorlevel%
popd
exit /b %ERR%
