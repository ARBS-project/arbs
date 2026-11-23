@echo off
rem ARBS-W: install or update the newest arbs-w-x.y.z-windows-portable.zip into D:\ARBS-W or %LOCALAPPDATA%\ARBS-W (settings kept). No admin.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0lib\ArbsWKit.ps1" -Channel Stable -Action Install %*
if errorlevel 1 pause
