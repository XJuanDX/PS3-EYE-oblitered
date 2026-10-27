@echo off
title Desinstalador PS3 EYE oblitered
echo ========================================================
echo       Desinstalando PS3 EYE oblitered y Virtual Camera
echo ========================================================
echo.
powershell.exe -ExecutionPolicy Bypass -File "%~dp0unregister.ps1"
echo.
echo Desinstalacion completa. Presiona cualquier tecla para salir...
pause >nul
