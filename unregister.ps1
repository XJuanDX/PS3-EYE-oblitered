# unregister.ps1 — Desregistra el Filtro DirectShow de PS3 Eye.
# Limpia las entradas tanto de usuario (HKCU) como del sistema.

$dllPath = (Resolve-Path (Join-Path $PSScriptRoot "build\ps3eye_dshow.dll")).Path
$clsid = "{E85B697D-4F12-4C67-B029-7928C3D82490}"
$category = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin -and (Test-Path $dllPath)) {
    Start-Process regsvr32.exe -ArgumentList "/u /s `"$dllPath`"" -Wait
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path "Registry::HKEY_CLASSES_ROOT\CLSID\$category\Instance\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
}

# Limpieza en HKCU
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "PS3EyeCamera" -ErrorAction SilentlyContinue

# Detener proceso PS3 EYE oblitered si está en ejecución
Get-Process -Name "PS3EYEOblitered", "PS3EyeApp" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " [EXITO] PS3 Eye Camera desregistrada limpiamente." -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
