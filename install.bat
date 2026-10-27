@echo off
setlocal
cd /d "%~dp0"
title Instalador PS3 EYE oblitered

echo ========================================================
echo       Instalador Automatico PS3 EYE oblitered
echo ========================================================
echo.

:: Verificar permisos de Administrador
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Solicitando permisos de Administrador para instalar el controlador...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo [1/3] Registrando certificado digital en el sistema...
certutil.exe -addstore -f "Root" "%~dp0ps3eye_driver.cer"
certutil.exe -addstore -f "TrustedPublisher" "%~dp0ps3eye_driver.cer"

echo.
echo [2/3] Instalando driver WinUSB nativo para la camara...
pnputil.exe /delete-driver oem56.inf /uninstall /force >nul 2>&1
pnputil.exe /add-driver "%~dp0usb_device_video_only.inf" /install

echo.
echo [3/3] Registrando camara virtual DirectShow y autoarranque...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1"

echo.
echo ========================================================
echo   [EXITO] Controlador y Camara Virtual Instalados!
echo ========================================================
echo La camara PS3 Eye y el microfono estan listos para usar
echo en Chrome, Edge, Meet, Discord, Zoom y OBS.
echo.
pause
