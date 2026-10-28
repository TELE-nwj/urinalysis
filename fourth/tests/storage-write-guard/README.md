# 写盘保护回归测试

用已安装ESP GCC将实际C编译为RV32IM，通过确定性CPU执行器验证控制流；GPIO、RTOS和日志接口使用替身，不进行设备写盘或串口操作。

在fourth目录执行：

```powershell
& 'C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe' -X utf8 ./tests/storage-write-guard/run_tests.py
```

（必须用该venv的python：运行器依赖pyelftools解析ELF，系统自带python会报 `No module named 'elftools'`。）

12项测试包含存储成功/打开失败/写失败/关闭失败、嵌套仅外层恢复、待开始不误开启采集、保留采集间隔、暂停状态保持、三态命令与快照、容量/超时/对时、按键状态机（monitor 9项 + capture 3项）。

Wi-Fi/手机链路删除后，原 mobile 套件（`mobile_test.c` 与 `mobile-stubs/`）已一并移除。写盘事务内"推屏已暂停"这一不变量改由 S1 自己的 `s_ui_pause`（经 `s1_ui_push_paused()`）校验，不再依赖 `service_mobile_set_push_paused` 替身；变异验证表明删掉 `s_ui_pause` 的置位或恢复会让 4~5 个用例失败，断言非空。

这些测试不证明U盘真实写入或真实GPIO时序，只验证写盘保护的控制流不变量。
