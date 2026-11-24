# ARBS-H B1 build environment (sourced inside the MSYS2 UCRT64 login shell).
# Only this shell's variables are set. System PATH, user environment and registry are untouched.
set -euo pipefail
# The paths below can be overridden from the environment (CI runners); unset = original build PC.
export ARBS_ROOT="${ARBS_ROOT:-/d/AI/ARBS-APEX}"
export ARBS_SRC=$ARBS_ROOT/host/arbs-h
export ARBS_BUILD=$ARBS_SRC/build
export ARBS_TOOLS="${ARBS_TOOLS:-$ARBS_ROOT/toolchain}"
export ARBS_BUILD_VERSION=2.18.9-arbsh.b1
export ARBS_NODE_DIR="${ARBS_NODE_DIR:-$ARBS_TOOLS/node-v22.22.2-win-x64}"
export PATH="$ARBS_NODE_DIR:$PATH"
export ARBS_NPM="$(cygpath -m "$ARBS_NODE_DIR/npm.cmd")"
export ARBS_DOTNET="${ARBS_DOTNET:-$(cygpath -m "$ARBS_TOOLS/dotnet/dotnet.exe")}"
# Keep tool caches under $ARBS_TOOLS (default D:\AI\ARBS-APEX\toolchain) instead of the user profile.
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
# Short TEMP/TMP for this shell only. The MSYS2 login profile points them at msys64\tmp, which can push a
# test's intermediate file path to the 260-char Windows limit.
export ARBS_TEMP="${ARBS_TEMP:-$ARBS_ROOT/.tmp}"
mkdir -p "$ARBS_TEMP"
export TEMP="$(cygpath -w "$ARBS_TEMP")"
export TMP="$TEMP"
