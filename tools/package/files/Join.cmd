@echo off
rem Join a co-op game with the code your host gave you.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\join.ps1" %*
