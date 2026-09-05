@echo off
setlocal
title ServerEngine - Web example
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-example.ps1" -Example Web
set "example_exit_code=%errorlevel%"
pause
exit /b %example_exit_code%
