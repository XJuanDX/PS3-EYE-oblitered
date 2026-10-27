@echo off
setlocal EnableDelayedExpansion
title Desinstalador PS3 EYE oblitered

echo ========================================================
echo      Desinstalador PS3 EYE oblitered y Driver
echo ========================================================
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Solicitando permisos de Administrador para desinstalar...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

cd /d "%~dp0"

echo [1/2] Deteniendo procesos y desregistrando camara virtual...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister.ps1"

echo [2/2] Removiendo certificados de desarrollo...
certutil -delstore "TrustedPublisher" "89493F39482BB30B2284AB4B30FF5B0E966278FB" >nul 2>&1
certutil -delstore "Root" "89493F39482BB30B2284AB4B30FF5B0E966278FB" >nul 2>&1

echo.
echo ========================================================
echo   [EXITO] Desinstalacion completada limpiamente.
echo ========================================================
echo Presiona cualquier tecla para cerrar...
pause >nul
