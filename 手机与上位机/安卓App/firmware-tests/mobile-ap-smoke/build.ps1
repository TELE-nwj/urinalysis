$ErrorActionPreference = 'Stop'
$env:IDF_PATH = 'F:\ESP32\v5.5.5\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.5\venv'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20241011'
$env:PATH = @(
    'C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64',
    'C:\Espressif\tools\cmake\3.30.2\bin',
    'C:\Espressif\tools\ninja\1.12.1',
    'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin',
    'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin',
    $env:PATH
) -join ';'
& "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" `
    -C $PSScriptRoot -B "$PSScriptRoot\build" -D 'IDF_TARGET=esp32s3' build
exit $LASTEXITCODE
