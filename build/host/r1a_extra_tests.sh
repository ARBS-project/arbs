# ARBS-H R1a: tests that ctest does not run on this Windows build, run directly.
#  1. tests/unit/platform/windows/test_service_startup.cpp (ctest registers it only on non-Windows hosts, with a
#     Windows.h shim): the web UI startup toggle opens ARBSHService, never ApolloService (D-R1a-2).
#  2. tests/unit/test_display_restore_task.py (needs Python, not found by this toolchain's CMake): restore task name.
#  3. web unit tests (node --test scripts/*.test.ts), including the R1a update target / asset rule tests.
# Usage (Git Bash or MSYS): bash r1a_extra_tests.sh   -> prints one PASS/FAIL line per test, exit 0 if all pass.
set -u
ROOT=/d/AI/ARBS-APEX
SRC=$ROOT/host/arbs-h
UCRT=/d/AI/ARBS-APEX/toolchain/msys64/ucrt64/bin
NODE=/d/AI/ARBS-APEX/toolchain/node-v22.22.2-win-x64
TMP=$(mktemp -d)
rc=0

PATH="$UCRT:$PATH" "$UCRT/c++.exe" -std=c++23 -I "$SRC/tests/support/windows_service_api" -I "$SRC" \
  -I "$SRC/third-party/googletest/googletest/include" \
  "$SRC/tests/unit/platform/windows/test_service_startup.cpp" \
  "$SRC/build/lib/libgtest_main.a" "$SRC/build/lib/libgtest.a" -o "$TMP/service_startup.exe" \
  && PATH="$UCRT:$PATH" "$TMP/service_startup.exe" > "$TMP/service_startup.log" 2>&1
r=$?; tail -3 "$TMP/service_startup.log" 2>/dev/null
if [ $r -eq 0 ]; then echo "PASS test_service_startup (ARBSHService)"; else echo "FAIL test_service_startup ($r)"; rc=1; fi

PATH="$UCRT:$PATH" PYTHONUTF8=1 python "$SRC/tests/unit/test_display_restore_task.py" "$UCRT/g++.exe"
r=$?
if [ $r -eq 0 ]; then echo "PASS test_display_restore_task.py (ARBS-H-DisplayRestore)"; else echo "FAIL test_display_restore_task.py ($r)"; rc=1; fi

(cd "$SRC/src_assets/common/assets/web" && PATH="$NODE:$PATH" node --experimental-strip-types --test scripts/*.test.ts > "$TMP/web.log" 2>&1)
r=$?; grep -E "^# (tests|pass|fail)" "$TMP/web.log"
if [ $r -eq 0 ]; then echo "PASS web unit tests"; else echo "FAIL web unit tests ($r)"; rc=1; fi

rm -rf "$TMP"
exit $rc
