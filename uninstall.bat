@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
title Desinstalador PS3 EYE oblitered

echo ========================================================
echo      Desinstalador PS3 EYE oblitered y Driver
echo ========================================================
echo.

:: Verificar permisos de Administrador
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Solicitando permisos de Administrador para desinstalar...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd.exe -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

echo [1/3] Deteniendo procesos y desregistrando camara virtual DirectShow...
if exist "%~dp0unregister.ps1" (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister.ps1"
) else (
    echo [AVISO] unregister.ps1 no encontrado. Omitiendo paso de desregistro COM.
)

echo.
echo [2/3] Localizando y removiendo paquetes de drivers del DriverStore...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$raw = & pnputil.exe /enum-drivers; $pub = $null; $orig = $null; $found = 0; foreach ($line in ($raw -split '\r?\n')) { if ($line -match '^\s*([A-Za-z\s]+):\s*(.+)$') { $k = $matches[1].Trim(); $v = $matches[2].Trim(); if ($k -like '*publicado*' -or $k -like '*Published Name*') { $pub = $v } elseif ($k -like '*original*' -or $k -like '*Original Name*') { $orig = $v; if ($pub -and ($orig -like '*ps3eye_video.inf*' -or $orig -like '*usb_device_video_only.inf*')) { Write-Host \"[INFO] Eliminando driver $pub ($orig) del DriverStore...\"; & pnputil.exe /delete-driver $pub /uninstall /force; $found++; $pub = $null; $orig = $null } } } }; if ($found -eq 0) { Write-Host '[INFO] No se encontraron paquetes OEM previos en el DriverStore.' } else { & pnputil.exe /scan-devices | Out-Null }"

echo.
echo [3/3] Removiendo certificados del sistema...
certutil.exe -delstore "TrustedPublisher" "89493F39482BB30B2284AB4B30FF5B0E966278FB" >nul 2>&1
certutil.exe -delstore "Root" "89493F39482BB30B2284AB4B30FF5B0E966278FB" >nul 2>&1
certutil.exe -delstore "TrustedPublisher" "PS3 Eye Driver" >nul 2>&1
certutil.exe -delstore "Root" "PS3 Eye Driver" >nul 2>&1
echo Certificados removidos del almacen de certificados.

echo.
echo ========================================================
echo   [EXITO] Desinstalacion completada limpiamente.
echo ========================================================
echo Presiona cualquier tecla para cerrar...
pause >nul
exit /b 0
