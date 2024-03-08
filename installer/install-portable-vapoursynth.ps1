$PythonVersionMajor = 3
$PythonVersionMid = 12
$PythonVersionMinor = 2

$Answer = "y"
$ProgressPreference = 'SilentlyContinue'
$VSGithubVersion = "Unknown"

$InstallExists = Test-Path -Path @(".\portable.vs") -PathType Leaf
if ($InstallExists) {
    $Answer = Read-Host "There appears to already exist a portable VapourSynth install in the current directory.`nProceed and overwrite? (y/n)"
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

if ($VSGithubVersion -eq "Unknown") {
    $Answer = Read-Host "Failed to retrieve VapourSynth version information from GitHub.`nProceed with install of R$VSVersion anyway? (y/n)"
} elseif ($VSGithubVersion -eq "R$VSVersion") {
    $Answer = Read-Host "Script will install the latest VapourSynth version.`nProceed with install of R$VSVersion`? (y/n)"
} else {
    $Answer = Read-Host "Script is for VapourSynth R$VSVersion but the latest version available is $VSGithubVersion.`nProceed with install of R$VSVersion anyway? (y/n)"
}

if ($Answer -eq "y") {
    Write-Host "Installing..."
} else {
    Write-Host "Aborted by user"
    exit 0
}

Write-Host "Determining latest Python $PythonVersionMajor.$PythonVersionMid.x version..."

for ($i = $PythonVersionMinor + 1; $i -le 10; $i++) {
    $PyUri = "https://www.python.org/ftp/python/$PythonVersionMajor.$PythonVersionMid..$i/python-$PythonVersionMajor.$PythonVersionMid..$i-embed-amd64.zip"
    try {
        $PythonReply = Invoke-WebRequest -Uri $PyUri -Method head
        $PythonVersionMinor = $i
    } catch {
        break
    }
}

Write-Host "Python version $PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor will be used for installation"

Start-Sleep -Second 2

New-Item -Path ".\" -Name "Downloads" -ItemType Directory -Force | Out-Null

$ProgressPreference = 'Continue'

Write-Host "Downloading Python..."
Invoke-WebRequest -Uri "https://www.python.org/ftp/python/$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor/python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip" -OutFile ".\Downloads\python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip"
Write-Host "Downloading VapourSynth..."
Invoke-WebRequest -Uri "https://github.com/vapoursynth/vapoursynth/releases/download/R$VSVersion/VapourSynth64-Portable-R$VSVersion.zip" -OutFile ".\Downloads\VapourSynth64-Portable-R$VSVersion.zip"
Write-Host "Downloading Pip..."
Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile ".\Downloads\get-pip.py"

# Expand-Archive requires the global scope variable to be set and not just the local one because why not?
$global:ProgressPreference = 'SilentlyContinue'

Write-Host "Extracting Python..."
Expand-Archive -LiteralPath ".\Downloads\python-$PythonVersionMajor.$PythonVersionMid.$PythonVersionMinor-embed-amd64.zip" -DestinationPath ".\" -Force
Add-Content -Path ".\python$PythonVersionMajor$PythonVersionMid._pth" -Encoding UTF8 -Value "..\Scripts" | Out-Null
Add-Content -Path ".\python$PythonVersionMajor$PythonVersionMid._pth" -Encoding UTF8 -Value "Scripts" | Out-Null
Add-Content -Path ".\python$PythonVersionMajor$PythonVersionMid._pth" -Encoding UTF8 -Value "Lib\site-packages" | Out-Null
Write-Host "Installing Pip..."
& ".\python.exe" ".\Downloads\get-pip.py" "--no-warn-script-location"
Remove-Item -Path ".\Scripts\*.exe"
Remove-Item -Path ".\VSScriptPython38.dll"
Write-Host "Extracting VapourSynth..."
Expand-Archive -LiteralPath ".\Downloads\VapourSynth64-Portable-R$VSVersion.zip" -DestinationPath ".\" -Force
Write-Host "Installing VapourSynth..."
& ".\python.exe" "-m" "pip" "install" ".\wheel\VapourSynth-$VSVersion-cp$PythonVersionMajor$PythonVersionMid-cp$PythonVersionMajor$PythonVersionMid-win_amd64.whl"

Write-Host "Installation complete" -ForegroundColor Green

pause