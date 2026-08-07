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
  --debug-uv-charts "%~dp0artifacts\generated\frame_006_blockout_uv_charts.png"
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set ERR=%errorlevel%
popd
exit /b %ERR%
