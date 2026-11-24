@echo off
rem Runs a host/scripts/*.sh file in the portable MSYS2 UCRT64 shell. Usage: b1_run.cmd b1_configure.sh [args]
rem CI1 (2026-11-24): ARBS_ROOT_WIN (Windows form) overrides the original build PC folder; the shell gets ARBS_ROOT.
setlocal
if not defined ARBS_ROOT_WIN set "ARBS_ROOT_WIN=D:\AI\ARBS-APEX"
set MSYSTEM=UCRT64
set CHERE_INVOKING=1
set HOME=
"%ARBS_ROOT_WIN%\toolchain\msys64\usr\bin\bash.exe" -lc "export ARBS_ROOT=$(cygpath -u '%ARBS_ROOT_WIN%'); bash $ARBS_ROOT/host/scripts/%*"
