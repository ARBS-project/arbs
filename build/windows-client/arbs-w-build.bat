@echo off
rem ARBS-W build wrapper (B2). All environment changes below are local to this cmd process (setlocal).
rem No setx, no registry writes. Usage:
rem   arbs-w-build.bat <out-zip-base-name>      -> client-w\out\<base>-<version>-x64-portable.zip + .sha256
rem   arbs-w-build.bat <exact-name>.zip         -> client-w\out\<exact-name>.zip + .sha256   (B2 D9)
rem   e.g. arbs-w-build.bat arbs-w-b2-d2-baseline
rem        arbs-w-build.bat arbs-w-b2-portable.zip
setlocal enableDelayedExpansion

set ARBS_ROOT=D:\AI\ARBS-APEX
set ARBS_SRC=%ARBS_ROOT%\client-w\arbs-w
set ARBS_OUT=%ARBS_ROOT%\client-w\out
set ARBS_TC=D:\AI\ARBS-APEX\toolchain
set OUT_BASE=%~1
if "%OUT_BASE%"=="" set OUT_BASE=arbs-w-build

rem R1b (D-R1b-10): edition name for the first log line = product version file (process only, read by app\arbs\arbs.pri).
rem The output name no longer decides it: arbs-w-0.1.0-windows-portable.zip would otherwise give "0.1.0-windows".
set ARBS_W_EDITION=
set /p ARBS_W_EDITION=<"%ARBS_SRC%\app\arbs\arbs_w_version.txt"
if "!ARBS_W_EDITION!"=="" (
    echo arbs_w_version.txt is empty
    exit /b 1
)
echo [arbs-w-build] edition !ARBS_W_EDITION!

rem --- tool PATH (process only) ---
set PATH=%ARBS_TC%\Qt\6.12.1\msvc2022_64\bin;%ProgramFiles%\7-Zip;%PATH%

rem --- side-effect guards (B1 lesson EA-B1-V-03 / B2_DESIGN 6-3) ---
set VSCMD_SKIP_SENDTELEMETRY=1
set DOTNET_CLI_TELEMETRY_OPTOUT=1
set DOTNET_NOLOGO=1
set DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
set DOTNET_GENERATE_ASPNET_CERTIFICATE=false
set DOTNET_ADD_GLOBAL_TOOLS_TO_PATH=false
set DOTNET_CLI_HOME=%ARBS_TC%\dotnet-home
set NUGET_PACKAGES=%ARBS_TC%\nuget-packages
set NUGET_HTTP_CACHE_PATH=%ARBS_TC%\nuget-http-cache
set NUGET_PLUGINS_CACHE_PATH=%ARBS_TC%\nuget-plugins-cache
set NUGET_SCRATCH=%ARBS_TC%\nuget-scratch
set MSBUILDDISABLENODEREUSE=1

rem --- build-arch.bat fork switches ---
set ARBS_SKIP_MSI=1
rem B2 D9: Discord is excluded from the build (app.pro); also drop discord-rpc.dll from the package
set ARBS_NO_DISCORD=1
set ARBS_VC_REDIST_DLL_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT
rem CI_VERSION intentionally not defined -> portable.dat is placed in the package
set CI_VERSION=
rem cl.exe multi-process compile inside nmake batch rules (speed only, no output change)
set CL=/MP

if not exist "%ARBS_SRC%\libs\windows\lib\x64\SDL3.dll" (
    echo libs\windows missing - extract moonlight-qt-deps v20.1 Windows-x64.zip first
    exit /b 1
)
if not exist "%ARBS_VC_REDIST_DLL_PATH%\vcruntime140.dll" (
    echo VC runtime folder not found: !ARBS_VC_REDIST_DLL_PATH!
    exit /b 1
)

echo [arbs-w-build] start %DATE% %TIME%
pushd "%ARBS_SRC%"
call scripts\build-arch.bat release
set RC=!ERRORLEVEL!
popd
echo [arbs-w-build] build-arch rc=!RC! end %DATE% %TIME%
if !RC! NEQ 0 exit /b !RC!

set /p VERSION=<"%ARBS_SRC%\app\version.txt"
set SRC_ZIP=%ARBS_SRC%\build\installer-x64-release\MoonlightPortable-x64-%VERSION%.zip
if not exist "%SRC_ZIP%" (
    echo portable zip not found: %SRC_ZIP%
    exit /b 2
)
if not exist "%ARBS_OUT%" mkdir "%ARBS_OUT%"
set DST_ZIP=%ARBS_OUT%\%OUT_BASE%-%VERSION%-x64-portable.zip
if /i "%OUT_BASE:~-4%"==".zip" set DST_ZIP=%ARBS_OUT%\%OUT_BASE%
copy /y "%SRC_ZIP%" "%DST_ZIP%" >nul
if !ERRORLEVEL! NEQ 0 exit /b 3
rem R1b (D-R1b-7/9): LICENSE, NOTICE, README and the user tools folder go into every zip.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ARBS_ROOT%\client-w\scripts\arbs-w-zip-extras.ps1" -Zip "%DST_ZIP%" -Version "!ARBS_W_EDITION!" -BaseVersion "%VERSION%"
if !ERRORLEVEL! NEQ 0 exit /b 5
rem R1b: the release name carries only the user tools; the developer test kit stays in test-named zips.
set ARBS_RELEASE_NAME=0
echo %OUT_BASE%| findstr /r /x /c:"arbs-w-[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*-windows-portable\.zip" >nul && set ARBS_RELEASE_NAME=1
rem B4b 5-2: home PC test kit into the zip as test-kit\ (two launchers + lib\HomeKit.ps1, not tests\).
rem -mcu=on = UTF-8 entry names (Korean launcher names). The hash below is computed after this step.
if "!ARBS_RELEASE_NAME!"=="0" if exist "%ARBS_ROOT%\client-w\test-kit\lib\HomeKit.ps1" (
    pushd "%ARBS_ROOT%\client-w"
    7z a -tzip -mcu=on "%DST_ZIP%" "test-kit\ARBS-W-*.cmd" "test-kit\lib\HomeKit.ps1" >nul
    set KIT_RC=!ERRORLEVEL!
    popd
    if !KIT_RC! NEQ 0 exit /b 4
    echo [arbs-w-build] test-kit added
)
powershell -NoProfile -Command "$h=(Get-FileHash -LiteralPath '%DST_ZIP%' -Algorithm SHA256).Hash.ToLower(); Set-Content -LiteralPath '%DST_ZIP%.sha256' -Value ($h + '  ' + (Split-Path -Leaf '%DST_ZIP%')) -Encoding ascii; Write-Output ('[arbs-w-build] ' + $h + '  %DST_ZIP%')"
exit /b 0
