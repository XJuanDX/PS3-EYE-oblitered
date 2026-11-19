# register.ps1 — Registra el Filtro DirectShow de PS3 Eye en Windows.
# Soporta registro de usuario actual (HKCU) y registro completo para toda la maquina (HKLM).

# Busqueda resiliente de la DLL ps3eye_dshow.dll
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

if (-not $dllPath) {
    Write-Host ""
    Write-Host "==========================================================" -ForegroundColor Red
    Write-Host " [ERROR] No se encontro ps3eye_dshow.dll" -ForegroundColor Red
    Write-Host "==========================================================" -ForegroundColor Red
    Write-Host "Rutas verificadas:" -ForegroundColor Yellow
    foreach ($cand in $candidatePaths) {
        Write-Host "  - $cand" -ForegroundColor Yellow
    }
    Write-Host "Ejecuta primero .\build.ps1 o asegurate de compilar el proyecto." -ForegroundColor Yellow
    exit 1
}

$clsid = "{E85B697D-4F12-4C67-B029-7928C3D82490}"
$legacyClsid = "{B19A293E-B4A1-4B52-87B6-8B2E098D7123}"
$category = "{860BB310-5D01-11d0-BD3B-00A0C911CE86}"
$friendlyName = "PS3 Eye Camera"
$legacyFriendlyName = "PS3 Eye Universal"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$filterData = $null

if ($isAdmin) {
    Write-Host "Registrando para toda la maquina (HKLM)..." -ForegroundColor Cyan

    # 1. Registro directo en HKLM\Software\Classes\CLSID para $clsid y $legacyClsid
    New-Item -Path "HKLM:\Software\Classes\CLSID\$clsid" -Value $friendlyName -Force | Out-Null
    New-Item -Path "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32" -Value $dllPath -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$clsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

    New-Item -Path "HKLM:\Software\Classes\CLSID\$legacyClsid" -Value $legacyFriendlyName -Force | Out-Null
    New-Item -Path "HKLM:\Software\Classes\CLSID\$legacyClsid\InprocServer32" -Value $dllPath -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$legacyClsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

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
    $item1 = Get-ItemProperty "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -ErrorAction SilentlyContinue
    if ($item1 -and $item1.FilterData) {
        $filterData = $item1.FilterData
    } else {
        $item2 = Get-ItemProperty "HKLM:\Software\Classes\CLSID\$category\Instance\$friendlyName" -ErrorAction SilentlyContinue
        if ($item2 -and $item2.FilterData) {
            $filterData = $item2.FilterData
        }
    }

    # Limpiar entrada antigua sin GUID si existia
    Remove-Item -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$friendlyName" -Recurse -Force -ErrorAction SilentlyContinue
}

# Si no obtuvimos FilterData desde HKLM, intentar buscarlo en HKCU o usar fallback canonico
if (-not $filterData) {
    $itemUser = Get-ItemProperty "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -ErrorAction SilentlyContinue
    if ($itemUser -and $itemUser.FilterData) {
        $filterData = $itemUser.FilterData
    } else {
        # Fallback canonico de FilterData para DirectShow video capture filter (640x480 YUY2 / Capture Pin)
        $filterData = [byte[]]@(
            0x02,0x00,0x00,0x00,0x00,0x00,0x20,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
            0x30,0x70,0x69,0x33,0x08,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x30,0x74,0x79,0x33,0x00,0x00,0x00,0x00,
            0x48,0x00,0x00,0x00,0x58,0x00,0x00,0x00,0x81,0x42,0x6c,0xfb,0x53,0x03,0xd1,0x11,
            0x90,0x5f,0x00,0x00,0xc0,0xcc,0x16,0xba,0x76,0x69,0x64,0x73,0x00,0x00,0x10,0x00,
            0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71,0x59,0x55,0x59,0x32,0x00,0x00,0x10,0x00,
            0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71
        )
    }
}

# Replicar FilterData en HKLM si somos admin
if ($isAdmin -and $filterData) {
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FilterData" -Value $filterData
    Set-ItemProperty -Path "HKLM:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FilterData" -Value $filterData
}

# Siempre registramos tambien en HKCU para asegurar disponibilidad inmediata en cualquier contexto de usuario
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid" -Value $friendlyName -Force | Out-Null
New-Item -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Value $dllPath -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$clsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

New-Item -Path "HKCU:\Software\Classes\CLSID\$legacyClsid" -Value $legacyFriendlyName -Force | Out-Null
New-Item -Path "HKCU:\Software\Classes\CLSID\$legacyClsid\InprocServer32" -Value $dllPath -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$legacyClsid\InprocServer32" -Name "ThreadingModel" -Value "Both"

New-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FriendlyName" -Value $friendlyName
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "CLSID" -Value $clsid

# Alias de compatibilidad en HKCU
New-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FriendlyName" -Value $legacyFriendlyName
Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "CLSID" -Value $clsid

# Replicar FilterData en HKCU
if ($filterData) {
    Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$clsid" -Name "FilterData" -Value $filterData
    Set-ItemProperty -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$legacyClsid" -Name "FilterData" -Value $filterData
}
Remove-Item -Path "HKCU:\Software\Classes\CLSID\$category\Instance\$friendlyName" -Recurse -Force -ErrorAction SilentlyContinue

# Busqueda resiliente de la aplicacion PS3EYEOblitered.exe / PS3EyeApp.exe
$appCandidates = @(
    (Join-Path $PSScriptRoot "bin\PS3EYEOblitered.exe"),
    (Join-Path $PSScriptRoot "build\PS3EYEOblitered.exe"),
    (Join-Path $PSScriptRoot "PS3EYEOblitered.exe"),
    (Join-Path $PSScriptRoot "bin\PS3EyeApp.exe"),
    (Join-Path $PSScriptRoot "build\PS3EyeApp.exe"),
    (Join-Path $PSScriptRoot "PS3EyeApp.exe")
)

$appPath = $null
foreach ($cand in $appCandidates) {
    if (Test-Path $cand) {
        $appPath = (Resolve-Path $cand).Path
        break
    }
}

if ($appPath) {
    Write-Host "Configurando autoarranque de aplicacion: $appPath" -ForegroundColor Cyan
    Set-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "PS3EyeCamera" -Value "`"$appPath`" --minimized"
    $running = Get-Process -Name "PS3EYEOblitered", "PS3EyeApp" -ErrorAction SilentlyContinue
    if (-not $running) {
        Write-Host "Iniciando servicio de bandeja del sistema en segundo plano..." -ForegroundColor Cyan
        Start-Process -FilePath $appPath -ArgumentList "--minimized"
    } else {
        Write-Host "El servicio ya se encuentra en ejecucion." -ForegroundColor Green
    }
} else {
    Write-Host "[AVISO] No se encontro ejecutable de aplicacion (PS3EYEOblitered.exe / PS3EyeApp.exe). Omitiendo autoarranque." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host " [EXITO] PS3 Eye Camera registrada como webcam DirectShow!" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "Ya puedes abrir OBS Studio, VLC, Discord o tu navegador:"
Write-Host "  -> Video : 'PS3 Eye Camera' (y 'PS3 Eye Universal')"
Write-Host "  -> Audio : 'Microfono (USB Camera-B4.09.24.1)'"
exit 0
