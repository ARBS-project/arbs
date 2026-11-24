@echo off
rem ARBS Core (C++17) - build static library + unit/golden/engine tests and run them. Local only:
rem no device access, no network, no installation. All environment changes stay in this cmd process (setlocal).
rem Usage: run_tests.bat [filter-substring]
rem Output: build\arbs_core.lib, build\arbs_core_tests.exe (intermediate files under build\obj-*)
setlocal enableDelayedExpansion

set CORE=%~dp0
set CORE=%CORE:~0,-1%
set BUILD=%CORE%\build

rem --- tool paths: environment variables win, empty = previous defaults ---
rem   ARBS_TOOLCHAIN_DIR  folder for the dotnet/nuget caches below (default D:\AI\ARBS-APEX\toolchain)
rem   ARBS_VCVARS         full path of vcvarsall.bat (default: Visual Studio 2022 Build Tools)
if not defined ARBS_TOOLCHAIN_DIR set "ARBS_TOOLCHAIN_DIR=D:\AI\ARBS-APEX\toolchain"

rem --- side-effect guards ---
set VSCMD_SKIP_SENDTELEMETRY=1
set DOTNET_CLI_TELEMETRY_OPTOUT=1
set DOTNET_NOLOGO=1
set DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
set DOTNET_GENERATE_ASPNET_CERTIFICATE=false
set DOTNET_ADD_GLOBAL_TOOLS_TO_PATH=false
set DOTNET_CLI_HOME=%ARBS_TOOLCHAIN_DIR%\dotnet-home
set NUGET_PACKAGES=%ARBS_TOOLCHAIN_DIR%\nuget-packages
set NUGET_HTTP_CACHE_PATH=%ARBS_TOOLCHAIN_DIR%\nuget-http-cache
set NUGET_PLUGINS_CACHE_PATH=%ARBS_TOOLCHAIN_DIR%\nuget-plugins-cache
set NUGET_SCRATCH=%ARBS_TOOLCHAIN_DIR%\nuget-scratch
set MSBUILDDISABLENODEREUSE=1
set CL=

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
if defined ARBS_VCVARS set "VCVARS=%ARBS_VCVARS%"
if not defined VCToolsInstallDir (
    if not exist "!VCVARS!" (
        echo vcvarsall.bat not found: !VCVARS!
        exit /b 1
    )
    call "!VCVARS!" x64 >nul
    if errorlevel 1 exit /b 1
)
echo [arbs-core] VCToolsVersion=%VCToolsVersion%

if not exist "%BUILD%\obj-core" mkdir "%BUILD%\obj-core"
if not exist "%BUILD%\obj-tests" mkdir "%BUILD%\obj-tests"
del /q "%BUILD%\obj-core\*.obj" "%BUILD%\obj-tests\*.obj" 2>nul

rem /fp:precise + default x64 SSE2 (no /arch:AVX2): no FMA contraction, float sums in Java order (B2_DESIGN.md 2-4 #13)
set FLAGS=/nologo /std:c++17 /W4 /permissive- /EHsc /utf-8 /O2 /fp:precise /MD /Zc:__cplusplus /I"%CORE%\include"

echo [arbs-core] compile core
cl %FLAGS% /c "%CORE%\src\*.cpp" /Fo"%BUILD%\obj-core\\"
if errorlevel 1 exit /b 2
lib /nologo /OUT:"%BUILD%\arbs_core.lib" "%BUILD%\obj-core\*.obj"
if errorlevel 1 exit /b 3

echo [arbs-core] compile tests
cl %FLAGS% /I"%CORE%\tests" /c "%CORE%\tests\*.cpp" /Fo"%BUILD%\obj-tests\\"
if errorlevel 1 exit /b 4
link /nologo /OUT:"%BUILD%\arbs_core_tests.exe" "%BUILD%\obj-tests\*.obj" "%BUILD%\arbs_core.lib"
if errorlevel 1 exit /b 5

echo [arbs-core] run tests
"%BUILD%\arbs_core_tests.exe" "%CORE%\tests\golden" %1
exit /b %ERRORLEVEL%
