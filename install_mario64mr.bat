@echo off
rem Mario 64 MR on Windows: double-click this. It asks for your ROM, sets up its own build tools the first time
rem (no admin, no WSL), makes the game and sends it to the headset. See windows\install_mario64mr.ps1 and the README.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0windows\install_mario64mr.ps1" %*
echo.
pause
