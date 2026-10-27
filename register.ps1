# register.ps1 — Registra el Filtro DirectShow de PS3 Eye en Windows.
# Soporta registro de usuario actual (HKCU, sin necesidad de Administrador)
# y registro para toda la máquina si se ejecuta como Administrador.

$dllPath = (Resolve-Path (Join-Path $PSScriptRoot "build\ps3eye_dshow.dll")).Path

if (-not (Test-Path $dllPath)) {
    Write-Error "No se encontró $dllPath. Ejecuta primero .\build.ps1."
    exit 1
}

$clsid = "{E85B697D-4F12-4C67-B029-7928C3D82490}"
$category = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"
$friendlyName = "PS3 Eye Camera"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin) {
    Write-Host "Registrando para toda la máquina (HKLM) vía regsvr32..." -ForegroundColor Cyan
    Start-Process regsvr32.exe -ArgumentList "/s `"$dllPath`"" -Wait
}

# Siempre registramos también en HKCU para asegurar disponibilidad inmediata
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Value $friendlyName -Force | Out-Null
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Value $dllPath -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

New-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FriendlyName" -Value $friendlyName
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "CLSID" -Value $clsid

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " [EXITO] PS3 Eye Camera registrada como webcam DirectShow!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Ya puedes abrir OBS Studio, VLC, Discord o tu navegador:"
Write-Host "  -> Video : 'PS3 Eye Camera'"
Write-Host "  -> Audio : 'Micrófono (USB Camera-B4.09.24.1)'"
