# ARBS-H R1a (0.1.0): portable ZIP -> installer stage -> Inno Setup -> host/out/arbs-h-0.1.0-setup.exe (+ .sha256),
# plus host/out/arbs-h-0.1.0-rollback.zip (+ .sha256) with the rollback / snapshot / log tools.
# Run through b1_run.cmd after r1a_configure.sh, b1_build.sh and b1_ctest.sh. Nothing is installed or executed
# from the produced setup.exe; only the Inno Setup compiler (ISCC.exe, already on this PC) runs.
# Stage = package_portable tree minus scripts\ (ApolloService / "Apollo" rule scripts), drivers\sunshine\,
# drivers\vhf-gamepad\ (touch Vibepollo devices), the portable launcher/README/config, plus tools\sunshinesvc.exe
# (analysis/R1a.md D-R1a-6/7, 2-3). The b1~b5 ZIPs in host/out are never touched.
source "${ARBS_ROOT:-/d/AI/ARBS-APEX}/host/scripts/r1a_env.sh"
cd "$ARBS_SRC"
grep -q "^BUILD_VERSION:UNINITIALIZED=$ARBS_BUILD_VERSION\$" "$ARBS_BUILD/CMakeCache.txt"
grep -q '^ARBS_H_SERVICE_NAME:STRING=ARBSHService$' "$ARBS_BUILD/CMakeCache.txt"
grep -q '^ARBS_H_UPDATE_CHECK_DEFAULT_SECONDS:STRING=86400$' "$ARBS_BUILD/CMakeCache.txt"
grep -q '^SUNSHINE_REPO_OWNER:STRING=arbs-project$' "$ARBS_BUILD/CMakeCache.txt"

# The numeric FILEVERSION (0.1.99.<minutes since HEAD commit>) only orders MSI file replacement and refuses to go
# backwards after a new local commit. The Inno installer copies with ignoreversion, so regenerate it from scratch.
rm -f "$ARBS_BUILD"/generated_versioninfo/windows_versioninfo_generated.{h,cache,stamp}
rm -f "$ARBS_BUILD"/cpack_artifacts/*-portable.zip
cmake --build "$ARBS_BUILD" --target package_portable --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
zip_src=$(ls "$ARBS_BUILD"/cpack_artifacts/*-portable.zip)
echo "cpack zip: $zip_src"

# ---- stage ----
rm -rf "$ARBS_R1A_STAGE" "$ARBS_R1A_STAGE.unpack"
mkdir -p "$ARBS_R1A_STAGE.unpack"
(cd "$ARBS_R1A_STAGE.unpack" && cmake -E tar xf "$zip_src")
top=$(ls "$ARBS_R1A_STAGE.unpack")
test "$(echo "$top" | wc -l)" = 1
mv "$ARBS_R1A_STAGE.unpack/$top" "$ARBS_R1A_STAGE"
rmdir "$ARBS_R1A_STAGE.unpack"
for excluded in scripts drivers/sunshine drivers/vhf-gamepad config README-portable.txt start-portable.cmd; do
  if [ -e "$ARBS_R1A_STAGE/$excluded" ]; then rm -rf "$ARBS_R1A_STAGE/$excluded"; echo "excluded: $excluded"; fi
done
cp -f "$ARBS_BUILD/tools/sunshinesvc.exe" "$ARBS_R1A_STAGE/tools/sunshinesvc.exe"
stage_list="${ARBS_EVIDENCE_DIR:-$ARBS_ROOT/evidence/R1a}/c_stage_list.txt"
(cd "$ARBS_R1A_STAGE" && find . -type f | sort) > "$stage_list"
echo "stage files: $(wc -l < "$stage_list")"

# ---- installer ----
pkg="$ARBS_SRC/packaging/windows/arbs-h"
mkdir -p "$ARBS_ROOT/host/out"
rm -f "$ARBS_ROOT/host/out/arbs-h-$ARBS_BUILD_VERSION-setup.exe" "$ARBS_ROOT/host/out/arbs-h-$ARBS_BUILD_VERSION-setup.exe.sha256"
MSYS2_ARG_CONV_EXCL='*' "$ARBS_ISCC" /Q \
  "/DAppVer=$ARBS_BUILD_VERSION" \
  "/DStageDir=$(cygpath -w "$ARBS_R1A_STAGE")" \
  "/DOutDir=$(cygpath -w "$ARBS_ROOT/host/out")" \
  "/DSrcRoot=$(cygpath -w "$ARBS_SRC")" \
  "$(cygpath -w "$pkg/arbs-h.iss")"
cd "$ARBS_ROOT/host/out"
sha256sum "arbs-h-$ARBS_BUILD_VERSION-setup.exe" | tee "arbs-h-$ARBS_BUILD_VERSION-setup.exe.sha256"

# ---- rollback tool (standalone; also installed to %ProgramData%\ARBS-H\rollback by the installer) ----
tools="$ARBS_BUILD/r1a-rollback/arbs-h-$ARBS_BUILD_VERSION-rollback"
rm -rf "$ARBS_BUILD/r1a-rollback"
mkdir -p "$tools"
cp -f "$pkg/arbs-h-setup.ps1" "$pkg/arbs-h-rollback.ps1" "$pkg/arbs-h-rollback.cmd" "$pkg/arbs-h-snapshot.cmd" "$tools/"
rm -f "arbs-h-$ARBS_BUILD_VERSION-rollback.zip" "arbs-h-$ARBS_BUILD_VERSION-rollback.zip.sha256"
(cd "$ARBS_BUILD/r1a-rollback" && cmake -E tar cf "$ARBS_ROOT/host/out/arbs-h-$ARBS_BUILD_VERSION-rollback.zip" --format=zip "arbs-h-$ARBS_BUILD_VERSION-rollback")
sha256sum "arbs-h-$ARBS_BUILD_VERSION-rollback.zip" | tee "arbs-h-$ARBS_BUILD_VERSION-rollback.zip.sha256"
