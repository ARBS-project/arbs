# Applies host/patches/*.patch to pinned submodules without moving their commits. Idempotent.
source /d/AI/ARBS-APEX/host/scripts/b1_env.sh
enet="$ARBS_SRC/third-party/moonlight-common-c/enet"
patch="$ARBS_ROOT/host/patches/enet-c7353c05-arbs-precise-time.patch"
test "$(git -C "$enet" rev-parse HEAD)" = c7353c059373f8d3fc83d451f8f1a477be3dc94e
if git -c core.autocrlf=true -C "$enet" apply --reverse --check "$patch" 2>/dev/null; then
  echo "enet patch already applied"
else
  git -c core.autocrlf=true -C "$enet" apply --check "$patch"
  git -c core.autocrlf=true -C "$enet" apply "$patch"
  echo "enet patch applied"
fi
