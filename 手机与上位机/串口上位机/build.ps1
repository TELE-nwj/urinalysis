# 把 serial-monitor 打包成单文件 exe
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File build.ps1
#
# 产物：dist\SerialMonitor.exe
#
# 说明：
#   · 使用系统 Python D:\python\python.exe（已装 pyserial + PyQt5）
#   · PyInstaller 若未安装，本脚本会自动装一次
#   · 单文件模式启动稍慢（需解包），但便于分发
#   · style.qss 作为数据文件一起打包，程序运行时从 exe 同级或解包目录读取
#
# 注意：本文件必须保存为 UTF-8 with BOM。PowerShell 5.1 会把无 BOM 的 UTF-8
#       按 GBK 读取，中文注释会变成乱码并引发语法错误（已踩过这个坑）。

$here = $PSScriptRoot
$py   = 'D:\python\python.exe'

if (-not (Test-Path $py)) {
    Write-Host "找不到 Python：$py ；请修改本脚本里的 `$py 变量" -ForegroundColor Red
    exit 1
}

# 探测外部程序时不能用 $ErrorActionPreference='Stop'：
# python 未装某模块时会把 Traceback 写到 stderr，Stop 模式会把它当成致命错误
# 直接终止脚本（这正是之前打包"没走到安装步骤就退出"的原因）。
$probe = & $py -c "import importlib.util,sys; sys.stdout.write('yes' if importlib.util.find_spec('PyInstaller') else 'no')" 2>&1

if ($probe -notmatch 'yes') {
    Write-Host '[1/4] 未安装 PyInstaller，正在安装 …' -ForegroundColor Yellow
    & $py -m pip install pyinstaller --quiet 2>&1 | Out-Null
    $probe = & $py -c "import importlib.util,sys; sys.stdout.write('yes' if importlib.util.find_spec('PyInstaller') else 'no')" 2>&1
    if ($probe -notmatch 'yes') {
        Write-Host '      PyInstaller 安装失败（检查网络或 pip 源）' -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host '[1/4] PyInstaller 已就绪' -ForegroundColor Cyan
}

Write-Host '[2/4] 清理旧产物 …' -ForegroundColor Cyan
foreach ($p in @("$here\build", "$here\dist", "$here\SerialMonitor.spec")) {
    if (Test-Path $p) { Remove-Item $p -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host '[3/4] 打包中（首次约 1-3 分钟）…' -ForegroundColor Cyan
& $py -m PyInstaller `
    --noconfirm `
    --onefile `
    --windowed `
    --name SerialMonitor `
    --add-data "$here\style.qss;." `
    --paths $here `
    --hidden-import serial.tools.list_ports `
    --exclude-module tkinter `
    --exclude-module matplotlib `
    --exclude-module numpy `
    --exclude-module pandas `
    --exclude-module PyQt5.QtWebEngineWidgets `
    --exclude-module PyQt5.QtMultimedia `
    --exclude-module PyQt5.QtQml `
    --exclude-module PyQt5.QtQuick `
    $here\main.py 2>&1 | Select-Object -Last 8

$exe = Join-Path $here 'dist\SerialMonitor.exe'
if (Test-Path $exe) {
    $size = [math]::Round((Get-Item $exe).Length / 1MB, 1)
    Write-Host '[4/4] 完成' -ForegroundColor Green
    Write-Host ("      产物: {0}  ({1} MB)" -f $exe, $size) -ForegroundColor Green
    Write-Host '      双击即可运行；也可用 --replay <日志文件> 做无硬件演示'
} else {
    Write-Host '[4/4] 未找到产物 exe，打包失败' -ForegroundColor Red
    exit 1
}
