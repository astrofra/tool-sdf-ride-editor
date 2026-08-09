@echo off
setlocal
pushd "%~dp0"

set "ASSETC=%~dp0app\bin\hg_lua-win-x64\harfang\assetc\assetc.exe"
set "ASSETS_SRC=%~dp0app\assets"
set "ASSETS_OUT=%~dp0app\assets_compiled"

if not exist "%ASSETC%" (
  echo [build_app_assets] ERROR: assetc introuvable:
  echo   %ASSETC%
  popd
  exit /b 1
)

if not exist "%ASSETS_SRC%\project.prj" (
  echo [build_app_assets] ERROR: racine d'assets introuvable:
  echo   %ASSETS_SRC%\project.prj
  popd
  exit /b 1
)

if not exist "%ASSETS_OUT%" mkdir "%ASSETS_OUT%"

echo [build_app_assets] Compiling "%ASSETS_SRC%" to "%ASSETS_OUT%"
"%ASSETC%" %* "%ASSETS_SRC%" "%ASSETS_OUT%"
set "ERR=%ERRORLEVEL%"

if not "%ERR%"=="0" (
  echo [build_app_assets] assetc failed with exit code %ERR%
)

popd
exit /b %ERR%
