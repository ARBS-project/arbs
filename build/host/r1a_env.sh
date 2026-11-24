# ARBS-H R1a (0.1.x) build environment: same shell-only settings as b1_env.sh, only the version differs.
# R1a-fix1 (2026-11-23): 0.1.1 (credentials ACL repair + TLS load error logging).
# R1b (2026-11-23): 0.1.2 (startup error handling + first-screen name); stage list goes to evidence/R1b.
# Sourced inside the MSYS2 UCRT64 login shell. System PATH, user environment and registry are untouched.
source /d/AI/ARBS-APEX/host/scripts/b1_env.sh
export ARBS_BUILD_VERSION=0.1.3
# Inno Setup 6 compiler (already present on this PC; only ISCC.exe is run, nothing is installed).
export ARBS_ISCC="/c/Program Files (x86)/Inno Setup 6/ISCC.exe"
export ARBS_R1A_STAGE="$ARBS_BUILD/r1a-stage"
export ARBS_EVIDENCE_DIR="$ARBS_ROOT/evidence/R1b"
