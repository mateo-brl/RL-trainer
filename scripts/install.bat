@echo off
rem RL Trainer : lance install.ps1 sans changer la politique d'exécution de la machine.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
pause
