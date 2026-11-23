# ARBS-H B1 build environment (sourced inside the MSYS2 UCRT64 login shell).
# Only this shell's variables are set. System PATH, user environment and registry are untouched.
set -euo pipefail
export ARBS_ROOT=/d/AI/ARBS-APEX
export ARBS_SRC=$ARBS_ROOT/host/arbs-h
export ARBS_BUILD=$ARBS_SRC/build
export ARBS_TOOLS=/d/AI/toolchain
export ARBS_BUILD_VERSION=2.18.9-arbsh.b1
export PATH="$ARBS_TOOLS/node-v22.22.2-win-x64:$PATH"
export ARBS_NPM="$(cygpath -m "$ARBS_TOOLS/node-v22.22.2-win-x64/npm.cmd")"
export ARBS_DOTNET="$(cygpath -m "$ARBS_TOOLS/dotnet/dotnet.exe")"
# Keep tool caches inside D:\AI\toolchain instead of the user profile.
export DOTNET_CLI_HOME="$(cygpath -w "$ARBS_TOOLS/dotnet-home")"
export NUGET_PACKAGES="$(cygpath -w "$ARBS_TOOLS/nuget-packages")"
export NUGET_HTTP_CACHE_PATH="$(cygpath -w "$ARBS_TOOLS/nuget-http-cache")"
export NUGET_PLUGINS_CACHE_PATH="$(cygpath -w "$ARBS_TOOLS/nuget-plugins-cache")"
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
# V-03 (2026-11-20): current SDKs ignore DOTNET_SKIP_FIRST_TIME_EXPERIENCE. Without these, the first
# dotnet run added ...\dotnet-home\.dotnet\tools to the user PATH (HKCU) and created an ASP.NET Core
# HTTPS development certificate in CurrentUser\My. Keep first-run side effects off.
export DOTNET_GENERATE_ASPNET_CERTIFICATE=false
export DOTNET_ADD_GLOBAL_TOOLS_TO_PATH=false
export DOTNET_NOLOGO=1
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export npm_config_cache="$(cygpath -w "$ARBS_TOOLS/npm-cache")"
export npm_config_update_notifier=false
export CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-14}
