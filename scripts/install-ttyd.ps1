# ttyd Windows Installer Script
# Run: powershell -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/pxuanbach/ttyd/main/scripts/install-ttyd.ps1 | iex"

param(
    [string]$Version = "1.7.8",
    [string]$InstallDir = "$env:LOCALAPPDATA\ttyd",
    [switch]$NoPath,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$Repo = "pxuanbach/ttyd"
$ZipName = "ttyd-win.x64.zip"
$Url = "https://github.com/$Repo/releases/download/v$Version/$ZipName"
$ZipPath = "$env:TEMP\$ZipName"

function Install-Ttyd {
    Write-Host "Downloading ttyd v$Version..." -ForegroundColor Cyan
    try {
        Invoke-WebRequest -Uri $Url -OutFile $ZipPath -UserAgent "ttyd-installer"
    } catch {
        Write-Host "Error: Failed to download from $Url" -ForegroundColor Red
        Write-Host "Details: $_" -ForegroundColor Red
        exit 1
    }

    Write-Host "Installing to $InstallDir..." -ForegroundColor Cyan
    if (Test-Path $InstallDir) {
        Remove-Item "$InstallDir\*" -Recurse -Force
    } else {
        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    }

    Expand-Archive -Path $ZipPath -DestinationPath $InstallDir -Force
    Remove-Item $ZipPath -Force

    if (-not $NoPath) {
        $currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
        if ($currentPath -notlike "*$InstallDir*") {
            [Environment]::SetEnvironmentVariable("PATH", "$InstallDir;$currentPath", "User")
            $env:PATH = "$InstallDir;$env:PATH"
            Write-Host "Added $InstallDir to PATH" -ForegroundColor Green
        }
    }

    Write-Host "`nInstallation complete!" -ForegroundColor Green
    Write-Host "ttyd installed at: $InstallDir\ttyd.exe" -ForegroundColor White
    Write-Host "`nRun: ttyd.exe -p 8080" -ForegroundColor Yellow
}

function Uninstall-Ttyd {
    Write-Host "Uninstalling ttyd..." -ForegroundColor Cyan
    
    $currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($currentPath -like "*$InstallDir*") {
        $newPath = ($currentPath -split ';' | Where-Object { $_ -ne $InstallDir }) -join ';'
        [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
        Write-Host "Removed $InstallDir from PATH" -ForegroundColor Green
    }

    if (Test-Path $InstallDir) {
        Remove-Item $InstallDir -Recurse -Force
        Write-Host "Removed $InstallDir" -ForegroundColor Green
    }
    
    Write-Host "Uninstall complete!" -ForegroundColor Green
}

if ($Uninstall) {
    Uninstall-Ttyd
} else {
    Install-Ttyd
}
