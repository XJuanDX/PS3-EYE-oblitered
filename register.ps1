# register.ps1 — Registra el Filtro DirectShow de PS3 Eye en Windows.
# Soporta registro de usuario actual (HKCU) y registro completo para toda la máquina (HKLM).

$dllPath = (Resolve-Path (Join-Path $PSScriptRoot "build\ps3eye_dshow.dll")).Path

if (-not (Test-Path $dllPath)) {
    Write-Error "No se encontró $dllPath. Ejecuta primero .\build.ps1."
    exit 1
}

$clsid = "{E85B697D-4F12-4C67-B029-7928C3D82490}"
$legacyClsid = "{B19A293E-B4A1-4B52-87B6-8B2E098D7123}"
$category = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"
$friendlyName = "PS3 Eye Camera"
$legacyFriendlyName = "PS3 Eye Universal"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin) {
    Write-Host "Registrando para toda la máquina (HKLM)..." -ForegroundColor Cyan

    # 1. Registro directo en HKLM\Software\Classes\CLSID
    New-Item -Path "HKLM:\Software\Classes\CLSID\$clsid" -Value $friendlyName -Force | Out-Null
    New-Item -Path "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32" -Value $dllPath -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

    # 2. Registrar bajo Video Input Device Category (CLSID_VideoInputDeviceCategory)
    New-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FriendlyName" -Value $friendlyName
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "CLSID" -Value $clsid

    # 3. Registrar alias de compatibilidad 'PS3 Eye Universal' para escenas existentes de OBS
    New-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FriendlyName" -Value $legacyFriendlyName
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "CLSID" -Value $clsid

    # 4. Ejecutar regsvr32 para mapeo nativo COM e IFilterMapper2 (genera FilterData)
    Start-Process regsvr32.exe -ArgumentList "/s `"$dllPath`"" -Wait

    # 5. Obtener FilterData generado por IFilterMapper2 y replicar en todas las instancias
    $filterData = $null
    $item1 = Get-ItemProperty "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -ErrorAction SilentlyContinue
    if ($item1 -and $item1.FilterData) {
        $filterData = $item1.FilterData
    } else {
        $item2 = Get-ItemProperty "HKLM:\Software\Classes\CLSID\$category\Instance\PS3 Eye Camera" -ErrorAction SilentlyContinue
        if ($item2 -and $item2.FilterData) {
            $filterData = $item2.FilterData
        }
    }
    if ($filterData) {
        Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FilterData" -Value $filterData
        Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FilterData" -Value $filterData
    }

    # Limpiar entrada antigua sin GUID si existía
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\PS3 Eye Camera" -Recurse -Force -ErrorAction SilentlyContinue
}

# Siempre registramos también en HKCU para asegurar disponibilidad inmediata en cualquier contexto de usuario
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Value $friendlyName -Force | Out-Null
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Value $dllPath -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

New-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FriendlyName" -Value $friendlyName
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "CLSID" -Value $clsid

# Alias de compatibilidad en HKCU
New-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FriendlyName" -Value $legacyFriendlyName
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "CLSID" -Value $clsid

# Replicar FilterData en HKCU si está disponible
if ($filterData) {
    Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FilterData" -Value $filterData
    Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FilterData" -Value $filterData
}
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\PS3 Eye Camera" -Recurse -Force -ErrorAction SilentlyContinue

# Configurar autoarranque y poner en marcha PS3 EYE oblitered en la bandeja del sistema
$appExe = Join-Path $PSScriptRoot "build\PS3EYEOblitered.exe"
if (-not (Test-Path $appExe)) { $appExe = Join-Path $PSScriptRoot "build\PS3EyeApp.exe" }
if (Test-Path $appExe) {
    $appPath = (Resolve-Path $appExe).Path
    Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "PS3EyeCamera" -Value "`"$appPath`" --minimized"
    $running = Get-Process -Name "PS3EYEOblitered", "PS3EyeApp" -ErrorAction SilentlyContinue
    if (-not $running) {
        Start-Process -FilePath $appPath -ArgumentList "--minimized"
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " [EXITO] PS3 Eye Camera registrada como webcam DirectShow!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Ya puedes abrir OBS Studio, VLC, Discord o tu navegador:"
Write-Host "  -> Video : 'PS3 Eye Camera' (y 'PS3 Eye Universal')"
Write-Host "  -> Audio : 'Micrófono (USB Camera-B4.09.24.1)'"
