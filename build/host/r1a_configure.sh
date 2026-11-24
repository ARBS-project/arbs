# ARBS-H R1a (0.1.0): configure the existing build tree as ARBS-H 0.1.0 (installer build).
# The repo/publisher values are passed explicitly because the old Nonary values are already cached.
source "${ARBS_ROOT:-/d/AI/ARBS-APEX}/host/scripts/r1a_env.sh"
bash "$ARBS_ROOT/host/scripts/b1_apply_patches.sh"
cd "$ARBS_SRC"
cmake -B "$ARBS_BUILD" -G Ninja -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_VERSION="$ARBS_BUILD_VERSION" \
  -DBUILD_TESTS=ON \
  -DBUILD_DOCS=OFF \
  -DSUNSHINE_ENABLE_WEBRTC=OFF \
  -DSUNSHINE_BUNDLE_VHF_GAMEPAD_DRIVER=OFF \
  -DBUILD_SUNSHINE_VIRTUAL_DISPLAY_DRIVER=OFF \
  -DBUILD_VIRTUALDISPLAY_PROBE=OFF \
  -DBUILD_VIRTUALDISPLAY_TOOLS=OFF \
  -DBUILD_VIRTUALDISPLAY_VULKAN_LAYER=OFF \
  -DSUNSHINE_ASSETS_DIR=assets \
  -DSUNSHINE_DOTNET_EXECUTABLE="$ARBS_DOTNET" \
  -DSUNSHINE_NPM_EXECUTABLE="$ARBS_NPM" \
  -DSUNSHINE_REPO_OWNER=arbs-project \
  -DSUNSHINE_REPO_NAME=arbs-h \
  "-DSUNSHINE_PUBLISHER_NAME=ARBS project" \
  -DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/arbs-project/arbs-h \
  -DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/arbs-project/arbs-h/issues \
  -DARBS_H_SERVICE_NAME=ARBSHService \
  -DARBS_H_UPDATE_CHECK_DEFAULT_SECONDS=86400 \
  "$@"
