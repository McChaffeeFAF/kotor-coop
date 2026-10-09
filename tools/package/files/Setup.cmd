@echo off
rem First-run setup: tell the game where your KotOR and KotOR II installs are.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup.ps1" %*
pause
