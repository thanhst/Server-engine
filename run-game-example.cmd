@echo off
setlocal
title ServerEngine - Game example
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-example.ps1" -Example Game
set "example_exit_code=%errorlevel%"
pause
exit /b %example_exit_code%
