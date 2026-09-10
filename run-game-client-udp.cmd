@echo off
setlocal
title ServerEngine - Game UDP client
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-game-client.ps1" -Protocol Udp %*
set "client_exit_code=%errorlevel%"
pause
exit /b %client_exit_code%
