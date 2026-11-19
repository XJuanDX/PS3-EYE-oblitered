@echo off
setlocal EnableDelayedExpansion
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
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process cmd.exe -ArgumentList '/c \"\"%~f0\"\"' -Verb RunAs"
    exit /b
)

:: Paso 1: Instalar certificado digital
echo [1/4] Registrando certificado digital en el sistema...
if not exist "%~dp0ps3eye_driver.cer" (
    echo.
    echo ========================================================
    echo   [ERROR] No se encontro el archivo de certificado:
    echo   "%~dp0ps3eye_driver.cer"
    echo ========================================================
    goto :error
)

certutil.exe -addstore -f "Root" "%~dp0ps3eye_driver.cer" >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ========================================================
    echo   [ERROR] Fallo al instalar el certificado en 'Root'.
    echo ========================================================
    goto :error
)

certutil.exe -addstore -f "TrustedPublisher" "%~dp0ps3eye_driver.cer" >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ========================================================
    echo   [ERROR] Fallo al instalar en 'TrustedPublisher'.
    echo ========================================================
    goto :error
)
echo Certificado registrado correctamente en almacenes de confianza.

:: Paso 2: Limpieza din?mica de drivers OEM en el nodo ra?z USB\VID_1415&PID_2000
echo.
echo [2/4] Verificando y limpiando drivers OEM en nodo raiz USB...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$rootDevs = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'USB\VID_1415&PID_2000*' -and $_.InstanceId -notlike '*&MI_*' }; $oems = @(); foreach ($d in $rootDevs) { $inf = (Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction SilentlyContinue).Data; if ($inf -and ($inf -like 'oem*.inf') -and ($inf -ne 'usb.inf')) { $oems += $inf } }; $oems = $oems | Select-Object -Unique; foreach ($o in $oems) { Write-Host \"[AVISO] Detectado driver OEM anterior ($o) en nodo raiz. Removiendo para restaurar binding nativo a usbccgp.sys...\"; & pnputil.exe /delete-driver $o /uninstall /force }; if ($oems.Count -gt 0) { Write-Host '[INFO] Rescaneando bus USB...'; & pnputil.exe /scan-devices | Out-Null }"
if %errorlevel% neq 0 (
    echo [ADVERTENCIA] La comprobacion de drivers OEM finalizo con advertencias menores.
)

:: Paso 3: Instalar driver WinUSB de video
echo.
echo [3/4] Instalando driver WinUSB nativo para la camara de video (MI_00)...
if not exist "%~dp0drivers\ps3eye_video.inf" (
    echo.
    echo ========================================================
    echo   [ERROR] No se encontro el archivo INF del driver:
    echo   "%~dp0drivers\ps3eye_video.inf"
    echo ========================================================
    goto :error
)

pnputil.exe /add-driver "%~dp0drivers\ps3eye_video.inf" /install
set PNP_RESULT=%errorlevel%
if %PNP_RESULT% neq 0 (
    if %PNP_RESULT% neq 3010 (
        echo.
        echo ========================================================
        echo   [ERROR] Fallo la instalacion del driver ps3eye_video.inf
        echo   Codigo de retorno pnputil: %PNP_RESULT%
        echo ========================================================
        goto :error
    )
)

:: Paso 4: Registrar filtro DirectShow y autoarranque
echo.
echo [4/4] Registrando filtro DirectShow y autoarranque...
if not exist "%~dp0register.ps1" (
    echo.
    echo ========================================================
    echo   [ERROR] No se encontro el script register.ps1 en:
    echo   "%~dp0register.ps1"
    echo ========================================================
    goto :error
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1"
if %errorlevel% neq 0 (
    echo.
    echo ========================================================
    echo   [ERROR] Fallo el registro de DirectShow o autoarranque.
    echo ========================================================
    goto :error
)

echo.
echo ========================================================
echo   [EXITO] Controlador y Camara Virtual Instalados!
echo ========================================================
echo La camara PS3 Eye y el microfono estan listos para usar
echo en Chrome, Edge, Meet, Discord, Zoom, OBS Studio y VLC.
echo.
pause
exit /b 0

:error
echo.
echo ========================================================
echo   [ERROR] La instalacion no pudo completarse con exito.
echo ========================================================
echo Revisa los mensajes de error mostrados arriba.
echo.
pause
exit /b 1
