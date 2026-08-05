# Packages the contents of a directory as a ZIP whose entry names are separated
# by the forward slashes the format requires (PKWARE APPNOTE, 4.4.17.1).
#
# Windows PowerShell's Compress-Archive writes the platform separator instead
# (PowerShell/Microsoft.PowerShell.Archive#62). Windows tools happen to accept
# that, but every other extractor reads the backslash as an ordinary character
# in the name, so unpacking a release under Linux or in MSYS2 produces a file
# called "wheel\VapourSynth-....whl" instead of the directory the archive meant.

param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$DestinationPath
)

$ErrorActionPreference = "Stop"

# Windows PowerShell loads neither by default, and they are separate assemblies:
# ZipArchiveMode and CompressionLevel come from the first, ZipFile from the second.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$source = (Resolve-Path -LiteralPath $SourceDirectory).ProviderPath.TrimEnd('\')
$destination = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).ProviderPath, $DestinationPath))

# ZipFile.Open refuses to overwrite, and a stale archive from an earlier build
# would otherwise stop the release being packaged.
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Force
}

$zip = [System.IO.Compression.ZipFile]::Open($destination, [System.IO.Compression.ZipArchiveMode]::Create)

try {
    # No -Force: hidden and system files stay out, which is what the wildcard
    # this replaced (Compress-Archive buildp64\*) also did.
    foreach ($item in Get-ChildItem -LiteralPath $source -Recurse) {
        $name = $item.FullName.Substring($source.Length).TrimStart('\').Replace('\', '/')
        if ($item.PSIsContainer) {
            # A directory holding files is implied by their own entry names;
            # only an empty one needs an entry to survive the round trip.
            if (-not (Get-ChildItem -LiteralPath $item.FullName)) {
                $zip.CreateEntry($name + '/') | Out-Null
            }
        } else {
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $item.FullName, $name, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    }
} finally {
    $zip.Dispose()
}
