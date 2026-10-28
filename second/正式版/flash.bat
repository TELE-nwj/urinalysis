@echo off
REM ============================================================
REM  一键：编译 + 烧录【本工程】(F:\ESP32\niaio\git\second)
REM  用法:  flash.bat          使用默认 COM12
REM         flash.bat COM5     指定串口
REM
REM  为什么要用这个脚本：
REM    VS Code 里 ESP-IDF 扩展的"当前工程"曾经指向参考工程
REM    F:\ESP32\niaio\project，点 IDE 的 Build/Flash 会烧错工程!
REM    这个脚本固定了目录和 SDK 路径，必定烧本工程。
REM ============================================================

set "IDF_PATH=F:\ESP32\v5.5.5\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif\tools"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v5.5.5\venv"
set "PATH=C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1;C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;%PATH%"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=COM12"

cd /d "%~dp0"
echo === 工程目录: %CD%
echo === 串口: %PORT%
"C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe" "%IDF_PATH%\tools\idf.py" -p %PORT% build flash
