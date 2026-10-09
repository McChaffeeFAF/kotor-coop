@echo off
rem Host a co-op game: the game starts hosting; then New Game or Load Game.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\host.ps1" %*
