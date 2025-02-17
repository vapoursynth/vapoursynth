    [string]$TargetFolder = ".\vapoursynth-portable",
    [switch]$Python38,
    [switch]$Unattended
)

$PythonVersionMajor = 3
$PythonVersionMid = 13
$PythonVersionMinor = 2

if ($Python38 -or ([System.Environment]::OSVersion.Version.Major -lt 10)) {
    $Python38 = $true
    $PythonVersionMid = 8
    $PythonVersionMinor = 10
}

$DownloadFolder = "$TargetFolder\vs-temp-dl"

$Answer = "y"
$ProgressPreference = 'SilentlyContinue'
$VSGithubVersion = "Unknown"

if (!$Unattended -and (Test-Path -Path @("$TargetFolder\portable.vs") -PathType Leaf)) {
    $Answer = Read-Host "There appears to already exist a portable VapourSynth install in the target directory.`nProceed and overwrite? (y/n)"
}

if ($Answer -ne "y") {
    Write-Host "Aborted by user"
    exit 0
}

Write-Host "Fetching latest version information..."

$ErrorActionPreference = "SilentlyContinue"

$GithubVersion = Invoke-WebRequest -Uri "https://api.github.com/repos/vapoursynth/vapoursynth/releases" | ConvertFrom-Json 

foreach ($Version in $GithubVersion) {
    if (-Not ($Version.prerelease)) {
        $VSGithubVersion = $Version.name
        break
    }
}

$ErrorActionPreference = "Continue"

if (!$Unattended) {
    if ($VSGithubVersion -eq "Unknown") {
        $Answer = Read-Host "Failed to retrieve VapourSynth version information from GitHub.`nProceed with install of R$VSVersion anyway? (y/n)"
    } elseif ($VSGithubVersion -eq "R$VSVersion") {
        $Answer = Read-Host "Script will install the latest VapourSynth version.`nProceed with install of R$VSVersion`? (y/n)"
    } else {
        $Answer = Read-Host "Script is for VapourSynth R$VSVersion but the latest version available is $VSGithubVersion.`nProceed with install of R$VSVersion anyway? (y/n)"
    }
}

if ($Answer -eq "y") {
    Write-Host "Installing..."
} else {
    Write-Host "Aborted by user"
    exit 0
}

New-Item -Path "$TargetFolder" -ItemType Directory -Force | Out-Null
if (-Not (Test-Path "$TargetFolder")) {
    Write-Host "Could not create '$TargetFolder' folder, aboring"
    exit 1
}

Write-Host "Determining latest Python $PythonVersionMajor.$PythonVersionMid.x version..."

for ($i = $PythonVersionMinor + 1; $i -le 10; $i++) {
    $PyUri = "https://www.python.org/ftp/python/$PythonVersionMajor.$PythonVersionMid.$i/python-$PythonVersionMajor.$PythonVersionMid.$i-embed-amd64.zip"
    try {
        $PythonReply = Invoke-WebRequest -Uri $PyUri -Method head
        $PythonVersionMinor = $i
    } catch {
        break
    }
}

Write-Host "Python version $PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor will be used for installation"

Start-Sleep -Second 2

New-Item -Path "$DownloadFolder" -ItemType Directory -Force | Out-Null

$ProgressPreference = 'Continue'

Write-Host "Downloading Python..."
Invoke-WebRequest -Uri "https://www.python.org/ftp/python/$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor/python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip" -OutFile "$DownloadFolder\python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip"
Write-Host "Downloading VapourSynth..."
Invoke-WebRequest -Uri "https://github.com/vapoursynth/vapoursynth/releases/download/R$VSVersion/VapourSynth64-Portable-R$VSVersion.zip" -OutFile "$DownloadFolder\VapourSynth64-Portable-R$VSVersion.zip"
Write-Host "Downloading Pip..."
Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile "$DownloadFolder\get-pip.py"

# Expand-Archive requires the global scope variable to be set and not just the local one because why not?
$global:ProgressPreference = 'SilentlyContinue'

Write-Host "Extracting Python..."
Expand-Archive -LiteralPath "$DownloadFolder\python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip" -DestinationPath "$TargetFolder" -Force
Add-Content -Path "$TargetFolder\python$PythonVersionMajor$PythonVersionMid._pth" -Encoding UTF8 -Value "vs-scripts" | Out-Null
Add-Content -Path "$TargetFolder\python$PythonVersionMajor$PythonVersionMid._pth" -Encoding UTF8 -Value "Lib\site-packages" | Out-Null
New-Item -Path "$TargetFolder\vs-plugins" -ItemType Directory -Force | Out-Null
New-Item -Path "$TargetFolder\vs-scripts" -ItemType Directory -Force | Out-Null
Write-Host "Installing Pip..."
& "$TargetFolder\python.exe" "$DownloadFolder\get-pip.py" "--no-warn-script-location"
Remove-Item -Path "$TargetFolder\Scripts\*.exe"
Write-Host "Extracting VapourSynth..."
Expand-Archive -LiteralPath "$DownloadFolder\VapourSynth64-Portable-R$VSVersion.zip" -DestinationPath "$TargetFolder" -Force
if ($Python38) {
    Move-Item -Path "$TargetFolder\VSScriptPython38.dll" -Destination "$TargetFolder\VSScript.dll" -Force
} else {
    Remove-Item -Path "$TargetFolder\VSScriptPython38.dll"
}
Write-Host "Installing VapourSynth..."
& "$TargetFolder\python.exe" "-m" "pip" "install" "$TargetFolder\wheel\VapourSynth-$VSVersion-cp$PythonVersionMajor$PythonVersionMid-cp$PythonVersionMajor$PythonVersionMid-win_amd64.whl"

Write-Host "Installation complete" -ForegroundColor Green

if (!$Unattended) {
    pause
}