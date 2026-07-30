$GLSLC = "C:\VulkanSDK\1.4.357.0\Bin\glslc.exe"
if (-not (Test-Path $GLSLC)) { $GLSLC = "glslc.exe" }

$variants = @(
    @("gpu_planestats_pass1.comp", "planeStats1Spv8",  @("-DSAMPLE_T=uint8_t")),
    @("gpu_planestats_pass1.comp", "planeStats1Spv16", @("-DSAMPLE_T=uint16_t")),
    @("gpu_planestats_pass2.comp", "planeStats2Spv",   @())
)

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("/* Generated from gpu_planestats_pass1.comp and gpu_planestats_pass2.comp by glslc")
$lines.Add("   (see regenerate_example_shaders.ps1), committed so building the example does not")
$lines.Add("   require the Vulkan SDK. */")
$lines.Add("#include <stdint.h>")

foreach ($v in $variants) {
    & $GLSLC -fshader-stage=compute -O --target-env=vulkan1.4 @($v[2]) $v[0] -o "tmp.spv"
    if ($LASTEXITCODE -ne 0) { throw "glslc failed for $($v[1])" }
    $bytes = [System.IO.File]::ReadAllBytes("$PWD\tmp.spv")
    $words = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $bytes.Length; $i += 4) { $words.Add("0x{0:x8}" -f [BitConverter]::ToUInt32($bytes, $i)) }
    $lines.Add("")
    $lines.Add("static const uint32_t $($v[1])[] = {")
    for ($i = 0; $i -lt $words.Count; $i += 8) { $lines.Add("    " + ($words[$i..([Math]::Min($i + 7, $words.Count - 1))] -join ", ") + ",") }
    $lines.Add("};")
    Remove-Item "$PWD\tmp.spv"
}
[System.IO.File]::WriteAllLines("$PWD\gpu_planestats_spv.h", $lines)
