@echo off
rem One click: finds your games the first time, then Host / Join / Play solo.
start "" powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0scripts\launcher.ps1"
