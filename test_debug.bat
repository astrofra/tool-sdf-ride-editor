@echo off
setlocal
pushd "%~dp0"

call "%~dp0build_debug.bat"
if errorlevel 1 goto :fail

ctest --preset test-vs2022-debug
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set ERR=%errorlevel%
popd
exit /b %ERR%

