@echo off
setlocal
pushd "%~dp0"
set ERR=0

call "%~dp0build_debug.bat"
if errorlevel 1 goto :fail

if not exist "%~dp0artifacts\generated" mkdir "%~dp0artifacts\generated"

"%~dp0build\vs2022-debug\bin\Debug\sdf_cli.exe" --scene "%~dp0scenes\frame_006_blockout.sdfscene" --out "%~dp0artifacts\generated\frame_006_blockout.obj"
if errorlevel 1 goto :fail

goto :done

:fail
set ERR=%errorlevel%

:done
pause
popd
exit /b %ERR%
