$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'idf-env.ps1')
$ThirdBuild = Join-Path $PSScriptRoot 'build'
& $ThirdPython $ThirdIdf -C $PSScriptRoot -B $ThirdBuild -D 'IDF_TARGET=esp32s3' build
exit $LASTEXITCODE
