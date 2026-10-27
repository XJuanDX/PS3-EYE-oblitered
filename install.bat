@echo off
title Instalador PS3 EYE oblitered
echo ========================================================
echo       Instalando PS3 EYE oblitered y Virtual Camera
echo ========================================================
echo.
powershell.exe -ExecutionPolicy Bypass -File "%~dp0register.ps1"
echo.
echo Proceso finalizado. Presiona cualquier tecla para salir...
pause >nul
