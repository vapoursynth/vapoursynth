$GLSLC = "C:\VulkanSDK\1.4.357.0\Bin\glslc.exe"
if (-not (Test-Path $GLSLC)) { $GLSLC = "glslc.exe" }

$variants = @(
    @("boxblur8",  "boxblur8Spv",  @("-DSAMPLE_T=uint8_t")),
    @("boxblur16", "boxblur16Spv", @("-DSAMPLE_T=uint16_t")),
    @("boxblurs",  "boxblursSpv",  @("-DSAMPLE_T=float", "-DFLOAT_SAMPLES")),
    @("boxblurh",  "boxblurhSpv",  @("-DSAMPLE_T=float16_t", "-DFLOAT_SAMPLES"))
)

foreach ($v in $variants) {
    & $GLSLC -fshader-stage=compute -O --target-env=vulkan1.4 @($v[2]) boxblur.comp -o "$($v[0]).spv"
    if ($LASTEXITCODE -ne 0) { throw "glslc failed for $($v[0])" }
    $bytes = [System.IO.File]::ReadAllBytes("$PWD\$($v[0]).spv")
    $words = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $bytes.Length; $i += 4) { $words.Add("0x{0:x8}" -f [BitConverter]::ToUInt32($bytes, $i)) }
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("/* Generated from boxblur.comp by glslc (see shaders/regenerate.ps1), committed so builds")
    $lines.Add("   do not require the Vulkan SDK. */")
    $lines.Add("#include <cstdint>")
    $lines.Add("")
    $lines.Add("static const uint32_t $($v[1])[] = {")
    for ($i = 0; $i -lt $words.Count; $i += 8) { $lines.Add("    " + ($words[$i..([Math]::Min($i + 7, $words.Count - 1))] -join ", ") + ",") }
    $lines.Add("};")
    [System.IO.File]::WriteAllLines("$PWD\$($v[0])_spv.h", $lines)
    Remove-Item "$PWD\$($v[0]).spv"
}
