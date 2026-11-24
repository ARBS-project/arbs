# ARBS-H R1a (0.1.x) build environment: same shell-only settings as b1_env.sh, only the version differs.
# R1a-fix1 (2026-11-23): 0.1.1 (credentials ACL repair + TLS load error logging).
# R1b (2026-11-23): 0.1.2 (startup error handling + first-screen name); stage list goes to evidence/R1b.
# H013 (2026-11-24): 0.1.3 (uninstall/post-install/lock handling, names of its own); stage list goes to evidence/H013.
# Sourced inside the MSYS2 UCRT64 login shell. System PATH, user environment and registry are untouched.
# CI1 (2026-11-24): ARBS_ROOT / ARBS_ISCC / ARBS_EVIDENCE_DIR may be set by the caller (CI); unset = original build PC.
source "${ARBS_ROOT:-/d/AI/ARBS-APEX}/host/scripts/b1_env.sh"
export ARBS_BUILD_VERSION=0.1.3
# Inno Setup 6 compiler (already present on this PC; only ISCC.exe is run, nothing is installed).
export ARBS_ISCC="${ARBS_ISCC:-/c/Program Files (x86)/Inno Setup 6/ISCC.exe}"
export ARBS_R1A_STAGE="$ARBS_BUILD/r1a-stage"
export ARBS_EVIDENCE_DIR="${ARBS_EVIDENCE_DIR:-$ARBS_ROOT/evidence/H013}"
