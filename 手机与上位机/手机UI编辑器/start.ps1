$ErrorActionPreference = 'Stop'
try {
    $existingEditor = Invoke-RestMethod -Uri 'http://127.0.0.1:8768/api/config' -TimeoutSec 1
    if ($existingEditor.androidSource -eq 'F:\ESP32\niaio\.worktrees\mobile-app-integration\mobile-app\app\src\main\java\com\niaio\mobile\UiDesign.kt') {
        Write-Output '编辑器已运行：http://127.0.0.1:8768/'
        return
    }
} catch { }
$pythonExe = 'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe'
$editorServer = Join-Path $PSScriptRoot 'server.py'
$serverProcess = Start-Process -FilePath $pythonExe -ArgumentList @($editorServer, '--port', '8768') -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $PSScriptRoot 'server-out.log') -RedirectStandardError (Join-Path $PSScriptRoot 'server-error.log')
$serverProcess.Id | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'server.pid')
Write-Output '编辑界面：http://127.0.0.1:8768/'
