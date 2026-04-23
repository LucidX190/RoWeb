@echo off
set "REPO=C:\Users\Lucid\projectpizzacomplete"
set "EMSDK=%REPO%\emsdk"
set "PATH=%EMSDK%;%EMSDK%\upstream\emscripten;%PATH%"
for /d %%N in ("%EMSDK%\node\*_64bit") do set "PATH=%%N\bin;%PATH%"
set "EMSDK_NODE=%EMSDK%\node"
set "EM_CONFIG=%EMSDK%\.emscripten"

:: Match the SUBST drive setup used when the cmake cache was first generated.
:: Without this, cmake complains the source path changed (real path vs P:\).
SUBST P: /D >nul 2>&1
SUBST Q: /D >nul 2>&1
SUBST P: "%REPO%"
SUBST Q: "%REPO%\vcpkg"
set "VCPKG_ROOT=Q:\"

set "OUT=P:\build\emscripten-release\UniversalClient"

cd /d P:\

echo Removing stale outputs to force relink (embeds latest assets)...
if exist "%OUT%\UniversalClient.html"      del /f /q "%OUT%\UniversalClient.html"
if exist "%OUT%\UniversalClient.js"        del /f /q "%OUT%\UniversalClient.js"
if exist "%OUT%\UniversalClient.wasm"      del /f /q "%OUT%\UniversalClient.wasm"
if exist "%OUT%\UniversalClient.data"      del /f /q "%OUT%\UniversalClient.data"
if exist "%OUT%\UniversalClient.worker.js" del /f /q "%OUT%\UniversalClient.worker.js"

echo Touching main.cpp...
copy /b "P:\UniversalClient\main.cpp"+,, "P:\UniversalClient\main.cpp"

echo Configuring...
cmake --preset emscripten-release
if errorlevel 1 ( echo CONFIG FAILED & exit /b 1 )

echo Building...
cmake --build "P:\build\emscripten-release" --target UniversalClient
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )

echo BUILD OK
