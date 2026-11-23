@echo off
rem ARBS-W: copy today's ARBS-*.log from D:\ARBS-W or %LOCALAPPDATA%\ARBS-W to the Desktop send folder. No admin.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0lib\ArbsWKit.ps1" -Channel Stable -Action SendLogs %*
if errorlevel 1 pause
