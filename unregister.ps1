# unregister.ps1 — Desregistra el Filtro DirectShow de PS3 Eye.
# Limpia las entradas tanto de usuario (HKCU) como del sistema (HKLM), y detiene los procesos asociados.

$candidatePaths = @(
    (Join-Path $PSScriptRoot "bin\ps3eye_dshow.dll"),
    (Join-Path $PSScriptRoot "build\ps3eye_dshow.dll"),
    (Join-Path $PSScriptRoot "ps3eye_dshow.dll")
)

$dllPath = $null
foreach ($path in $candidatePaths) {
    if (Test-Path $path) {
        $dllPath = (Resolve-Path $path).Path
        break
    }
}

$clsid = "{E85B697D-4F12-4C67-B029-7928C3D82490}"
$legacyClsid = "{B19A293E-B4A1-4B52-87B6-8B2E098D7123}"
$category = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"
$friendlyName = "PS3 Eye Obliterated Virtual Camera"
$legacyFriendlyName = "PS3 Eye Universal"

Write-Host "Deteniendo procesos activos (PS3EYEOblitered, PS3EyeApp)..." -ForegroundColor Cyan
Get-Process -Name "PS3EYEOblitered", "PS3EyeApp" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin) {
    if ($dllPath) {
        Write-Host "Ejecutando desregistro COM con regsvr32 /u..." -ForegroundColor Cyan
        Start-Process regsvr32.exe -ArgumentList "/u /s `"$dllPath`"" -Wait -ErrorAction SilentlyContinue
    }

    Write-Host "Limpiando entradas de registro del sistema (HKLM y HKCR)..." -ForegroundColor Cyan
    # Eliminar CLSIDs en HKLM y HKCR
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue

    # Eliminar instancias en DirectShow Video Input Device Category
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$friendlyName" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyFriendlyName" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$category\Instance\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$category\Instance\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$category\Instance\$friendlyName" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$category\Instance\$legacyFriendlyName" -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Limpiando entradas de registro de usuario (HKCU)..." -ForegroundColor Cyan
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$friendlyName" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyFriendlyName" -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "Removiendo clave de inicio automatico..." -ForegroundColor Cyan
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "PS3EyeCamera" -ErrorAction SilentlyContinue
Remove-ItemProperty -Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "PS3EyeCamera" -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " [EXITO] PS3 Eye Camera desregistrada limpiamente." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
exit 0
