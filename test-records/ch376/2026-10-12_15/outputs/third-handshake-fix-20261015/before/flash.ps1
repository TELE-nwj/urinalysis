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
        throw "缺少烧录文件：$required。请先运行 build.ps1，或使用 -UsePrebuilt 选择交付固件。"
    }
}
$ThirdMetadata = Get-Content -LiteralPath $ThirdMetadataFile -Raw | ConvertFrom-Json
if ($ThirdMetadata.extra_esptool_args.chip -ne 'esp32s3' -or $ThirdMetadata.app.file -ne 'third.bin') {
    throw '烧录元数据与当前 third ESP32-S3 工程不匹配。'
}
$ThirdImageRoot = [IO.Path]::GetFullPath($ThirdImages).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
foreach ($flashFile in $ThirdMetadata.flash_files.PSObject.Properties) {
    $imagePath = [IO.Path]::GetFullPath((Join-Path $ThirdImages ([string]$flashFile.Value)))
    if (!$imagePath.StartsWith($ThirdImageRoot, [StringComparison]::OrdinalIgnoreCase) -or
        !(Test-Path -LiteralPath $imagePath -PathType Leaf)) {
        throw "镜像不存在或超出固件目录：$imagePath"
    }
    if ((Get-Item -LiteralPath $imagePath).Length -eq 0) { throw "镜像文件为空：$imagePath" }
}
if (!$UsePrebuilt) {
    $ThirdConfig = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sdkconfig') -Raw
    if ($ThirdConfig -notmatch '(?m)^CONFIG_IDF_TARGET="esp32s3"\r?$') {
        throw 'sdkconfig 的目标芯片必须为 esp32s3。'
    }
}
# 不自动选择串口；调用者必须明确指定实际的 ESP32-S3 下载口。
# CH340 日志口 COM21 不应直接当作固件下载口。
Push-Location -LiteralPath $ThirdImages
try {
    & $ThirdPython -m esptool --chip esp32s3 --port $Port --baud $Baud `
        --before default_reset --after hard_reset write_flash '@flash_args'
    $ThirdFlashExit = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $ThirdFlashExit
