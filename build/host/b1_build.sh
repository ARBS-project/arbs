# Builds all targets (application + tests).
source "${ARBS_ROOT:-/d/AI/ARBS-APEX}/host/scripts/b1_env.sh"
cd "$ARBS_SRC"
cmake --build "$ARBS_BUILD" --target all --parallel "$CMAKE_BUILD_PARALLEL_LEVEL" "$@"
