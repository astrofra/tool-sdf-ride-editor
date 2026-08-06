@echo off
setlocal
pushd "%~dp0"

cmake --preset vs2022-debug
if errorlevel 1 goto :fail

cmake --build --preset build-vs2022-debug
if errorlevel 1 goto :fail

popd
exit /b 0

:fail
set ERR=%errorlevel%
popd
exit /b %ERR%

