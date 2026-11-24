@echo off
rem Runs a host/scripts/*.sh file in the portable MSYS2 UCRT64 shell. Usage: b1_run.cmd b1_configure.sh [args]
setlocal
set MSYSTEM=UCRT64
set CHERE_INVOKING=1
set HOME=
"D:\AI\ARBS-APEX\toolchain\msys64\usr\bin\bash.exe" -lc "bash /d/AI/ARBS-APEX/host/scripts/%*"
