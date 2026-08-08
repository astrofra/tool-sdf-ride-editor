@echo off
setlocal
pushd "%~dp0"

call "%~dp0build_app_assets.bat" -quiet
if errorlevel 1 (
  popd
  exit /b %errorlevel%
)

pushd "%~dp0app"
".\bin\hg_lua-win-x64\lua.exe" main.lua
set "ERR=%ERRORLEVEL%"
popd

popd
exit /b %ERR%
