[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM[1-9][0-9]*$')]
    [string]$Port,
    [ValidateRange(9600, 2000000)]
    [int]$Baud = 460800,
    [switch]$UsePrebuilt
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf-env.ps1')
$ThirdImages = Join-Path $PSScriptRoot $(if ($UsePrebuilt) { 'firmware' } else { 'build' })
$ThirdArgsFile = Join-Path $ThirdImages 'flash_args'
$ThirdMetadataFile = Join-Path $ThirdImages 'flasher_args.json'
foreach ($required in @($ThirdArgsFile, $ThirdMetadataFile)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing flash artifact: $required. Run build.ps1 or select the delivered firmware with -UsePrebuilt."
    }
}
$ThirdMetadata = Get-Content -LiteralPath $ThirdMetadataFile -Raw | ConvertFrom-Json
if ($ThirdMetadata.extra_esptool_args.chip -ne 'esp32s3' -or $ThirdMetadata.app.file -ne 'third.bin') {
    throw 'Flash metadata does not describe this third ESP32-S3 build.'
}
$ThirdImageRoot = [IO.Path]::GetFullPath($ThirdImages).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($flashFile in $ThirdMetadata.flash_files.PSObject.Properties) {
    $imagePath = [IO.Path]::GetFullPath((Join-Path $ThirdImages ([string]$flashFile.Value)))
    if (!$imagePath.StartsWith($ThirdImageRoot, [StringComparison]::OrdinalIgnoreCase) -or
        !(Test-Path -LiteralPath $imagePath -PathType Leaf)) {
        throw "Missing or out-of-directory image: $imagePath"
    }
    if ((Get-Item -LiteralPath $imagePath).Length -eq 0) { throw "Empty image: $imagePath" }
}
if (!$UsePrebuilt) {
    $ThirdConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig') -Raw
    if ($ThirdConfig -notmatch '(?m)^CONFIG_IDF_TARGET="esp32s3"\r?$') {
        throw 'sdkconfig must target esp32s3.'
    }
}
# No automatic COM choice: the caller must select the actual ESP32-S3 download
# port. CH340 log port COM21 is not assumed to be a firmware download port.
Push-Location -LiteralPath $ThirdImages
try {
    & $ThirdPython -m esptool --chip esp32s3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset write_flash '@flash_args'
    $ThirdFlashExit = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $ThirdFlashExit
