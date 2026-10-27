@echo off
setlocal EnableDelayedExpansion
title Instalador PS3 EYE oblitered

echo ========================================================
echo       Instalador Automatico PS3 EYE oblitered
echo ========================================================
echo.

:: Verificar permisos de Administrador
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo Solicitando permisos de Administrador para instalar el controlador...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

:: Ejecutando con permisos de Administrador
cd /d "%~dp0"

echo [1/3] Instalando certificado digital en el almacen del sistema...
certutil -addstore -f "Root" "%~dp0ps3eye_driver.cer" >nul 2>&1
certutil -addstore -f "TrustedPublisher" "%~dp0ps3eye_driver.cer" >nul 2>&1

echo [2/3] Instalando controlador USB nativo para la camara...
pnputil /add-driver "%~dp0usb_device_video_only.inf" /install

echo [3/3] Registrando Camara Virtual DirectShow y configurando autoarranque...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1"

echo.
echo ========================================================
echo   [EXITO] Instalacion 1-Click Completada!
echo ========================================================
echo El controlador, la aplicacion y la camara virtual ya estan
echo instalados y listos para usar en Chrome, Meet, Discord, Zoom y OBS.
echo.
echo Presiona cualquier tecla para cerrar...
pause >nul
