[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$UsbRepairRoot = $PSScriptRoot
$UsbRepairImage = Join-Path $UsbRepairRoot 'General-UDisk-H-partition-before-repair.img'
$UsbRepairReport = [ordered]@{ Started=(Get-Date).ToString('o'); Target='H:'; Completed=$false; Formatted=$false }

function Assert-UsbRepairTarget {
    $UsbRepairDisk = Get-Disk -Number 1
    $UsbRepairPart = Get-Partition -DriveLetter H
    $UsbRepairVolume = Get-Volume -DriveLetter H
    if ($UsbRepairDisk.FriendlyName -ne 'General UDisk' -or $UsbRepairDisk.BusType -ne 'USB' -or
        $UsbRepairDisk.Size -ne 4026531840 -or $UsbRepairPart.DiskNumber -ne 1 -or
        $UsbRepairPart.PartitionNumber -ne 1 -or $UsbRepairPart.Offset -ne 32768 -or
        $UsbRepairPart.Size -ne 4026499072 -or $UsbRepairVolume.FileSystem -ne 'FAT32') {
        throw '目标U盘身份或盘符映射改变，终止修复。'
    }
}

try {
    if (Test-Path -LiteralPath (Join-Path $UsbRepairRoot 'repair-completed.json')) {
        throw '本次修复已经完成，禁止重复执行。'
    }
    $UsbRepairAdmin = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (!$UsbRepairAdmin) { throw 'Windows要求管理员权限运行文件系统修复。' }
    Assert-UsbRepairTarget
    $UsbRepairBackup = Get-Content -LiteralPath (Join-Path $UsbRepairRoot 'backup-manifest.json') -Raw | ConvertFrom-Json
    if (!$UsbRepairBackup.completed -or $UsbRepairBackup.bytes -ne 4026499072 -or
        (Get-Item -LiteralPath $UsbRepairImage).Length -ne 4026499072) { throw '完整分区镜像尚未完成。' }
    $UsbRepairImageHash = (Get-FileHash -LiteralPath $UsbRepairImage -Algorithm SHA256).Hash
    if ($UsbRepairImageHash -ne $UsbRepairBackup.sha256_stream) { throw '完整备份哈希校验失败。' }
    $UsbRepairReport.BackupSHA256 = $UsbRepairImageHash
    Assert-UsbRepairTarget
    # 用户已授权修复；保存丢失簇链为文件，不格式化或改分区。
    'Y' | & "$env:SystemRoot\System32\chkdsk.exe" H: /F /X 2>&1 |
        Tee-Object -FilePath (Join-Path $UsbRepairRoot 'chkdsk-repair.txt')
    $UsbRepairReport.RepairExitCode = $LASTEXITCODE
    if ($UsbRepairReport.RepairExitCode -notin @(0,1)) { throw "CHKDSK修复未成功，退出码=$($UsbRepairReport.RepairExitCode)" }
    Assert-UsbRepairTarget
    & "$env:SystemRoot\System32\chkdsk.exe" H: 2>&1 |
        Tee-Object -FilePath (Join-Path $UsbRepairRoot 'chkdsk-after-repair.txt')
    $UsbRepairReport.CheckExitCode = $LASTEXITCODE
    if ($UsbRepairReport.CheckExitCode -ne 0) { throw '修复后仍存在文件系统错误，停止后续写入。' }

    Assert-UsbRepairTarget
    $UsbRepairRecovery = 'H:\Recovered_20261014'
    if (Test-Path -LiteralPath $UsbRepairRecovery) { throw '恢复目录已存在，禁止覆盖。' }
    $UsbRepairSource = Join-Path $UsbRepairRoot 'recovered-records'
    $UsbRepairRecords = @(Get-ChildItem -LiteralPath $UsbRepairSource -File -Filter '*.TXT')
    if (!$UsbRepairRecords.Count) { throw '没有已经确认的恢复记录，停止后续写入。' }
    New-Item -ItemType Directory -Path $UsbRepairRecovery | Out-Null
    foreach ($UsbRepairRecord in $UsbRepairRecords) {
        $UsbRepairDestination = Join-Path $UsbRepairRecovery $UsbRepairRecord.Name
        Copy-Item -LiteralPath $UsbRepairRecord.FullName -Destination $UsbRepairDestination
        if ((Get-FileHash -LiteralPath $UsbRepairRecord.FullName).Hash -ne (Get-FileHash -LiteralPath $UsbRepairDestination).Hash) {
            throw '恢复文件写回后哈希不一致。'
        }
    }
    $UsbRepairReport.RecoveredRecords = $UsbRepairRecords.Count
    $UsbRepairReport.RecoveryDirectory = $UsbRepairRecovery
    Assert-UsbRepairTarget
    $UsbRepairTest = 'H:\Recovered_20261014\READBACK.TMP'
    if (Test-Path -LiteralPath $UsbRepairTest) { throw '测试文件已存在，禁止覆盖。' }
    $UsbRepairBytes = [Text.Encoding]::UTF8.GetBytes('U盘文件系统修复后读写验证：2026-10-14。')
    $UsbRepairStream = [IO.File]::Open($UsbRepairTest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $UsbRepairStream.Write($UsbRepairBytes,0,$UsbRepairBytes.Length); $UsbRepairStream.Flush($true) }
    finally { $UsbRepairStream.Dispose() }
    $UsbRepairReadback = [IO.File]::ReadAllBytes($UsbRepairTest)
    if ([Convert]::ToBase64String($UsbRepairReadback) -ne [Convert]::ToBase64String($UsbRepairBytes)) { throw '读写测试失败。' }
    # 精确删除本脚本刚创建的单个临时文件，不递归删除或触碰用户文件。
    Remove-Item -LiteralPath $UsbRepairTest
    $UsbRepairReport.ReadWriteVerified = $true
    & "$env:SystemRoot\System32\chkdsk.exe" H: 2>&1 |
        Tee-Object -FilePath (Join-Path $UsbRepairRoot 'chkdsk-final.txt')
    $UsbRepairReport.FinalCheckExitCode = $LASTEXITCODE
    if ($UsbRepairReport.FinalCheckExitCode -ne 0) { throw '恢复记录之后文件系统最终检查未通过。' }
    Get-ChildItem -LiteralPath H:\ -Force | Select-Object Name,Length,Attributes |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $UsbRepairRoot 'root-after.json') -Encoding utf8
    $UsbRepairReport.Completed=$true
    $UsbRepairReport.Finished=(Get-Date).ToString('o')
    $UsbRepairReport | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $UsbRepairRoot 'repair-completed.json') -Encoding utf8
} catch {
    $UsbRepairReport.Error=$_.Exception.Message
    $UsbRepairReport | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $UsbRepairRoot 'repair-failed.json') -Encoding utf8
    exit 1
}
