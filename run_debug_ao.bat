@echo off
setlocal
pushd "%~dp0"

call "%~dp0build_debug.bat"
if errorlevel 1 goto :fail

if not exist "%~dp0artifacts\generated" mkdir "%~dp0artifacts\generated"

"%~dp0build\vs2022-debug\bin\Debug\sdf_cli.exe" ^
  --scene "%~dp0scenes\frame_006_blockout.sdfscene" ^
  --out "%~dp0artifacts\generated\frame_006_blockout.obj" ^
  --debug-render "%~dp0artifacts\generated\frame_006_debug_ao.png" ^
  --debug-mode ao ^
  --render-width 320 ^
  --render-height 180 ^
  --camera-front ^
  --ao-samples 8 ^
  --ao-max-distance 10.0
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set ERR=%errorlevel%
popd
exit /b %ERR%

