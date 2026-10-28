# 写盘保护回归测试

用已安装ESP GCC将实际C编译为RV32IM，通过确定性CPU执行器验证控制流；GPIO、网络和RTOS接口使用替身，不进行设备写盘或串口操作。

在third目录执行：

```powershell
& 'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe' -X utf8 ./tests/storage-write-guard/run_tests.py
```

15项测试包含存储成功/打开失败/写失败/关闭失败、嵌套仅外层恢复、待开始不误开启采集、保留采集间隔、暂停状态保持、Wi-Fi会话和控制ACK保持、状态暂停与发送竞争、原手机协议/命令/计时回归。最初在原实现上6项失败，实施后全部通过。

这些测试不证明U盘真实写入或Wi-Fi射频不中断GPIO时序；已排队状态帧以及TCP控制流量可在暂停期间完成。新开关暂停新状态帧入队，不关闭热点和会话。
