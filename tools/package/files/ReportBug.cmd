@echo off
rem Pack your logs and settings (no game files) into a zip to send to the developer.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\report.ps1" %*
pause
