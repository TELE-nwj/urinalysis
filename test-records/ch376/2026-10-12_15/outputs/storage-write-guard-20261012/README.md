# third 写盘保护修改记录

用户2026-10-12确认正式实现：写盘期间Wi-Fi只暂停状态推送，不断联；关闭液滴中断；日志对齐second。

修改5个源码文件：main/app/S1/S1.c、main/service/urine/urine.c/.h、main/service/mobile/mobile.c/.h。所有S1新建/恢复/分钟写盘事务使用相同外层保护，SPI递归互斥量不关闭RTOS调度。保存采集原状态，临时只关GPIO中断，不清滴间隔和缓冲，退出时不会误开启暂停/待开始采集。手机仅暂停新status入队，握手/控制ACK/TCP连接保持，最终发送闸门复核暂停竞争；已排队状态帧及网络底层可继续。

storage阶段INFO日志保留；CH376保留WARN/ERROR，日志等级退出恢复；写入后明确打印“退出临界区: 第X分钟已写入U盘”，失败标出打开/写入/关闭阶段。所有返回路径恢复现场，嵌套仅最外层恢复。原CH376指令时序、写块算法、文件格式、五秒重试及状态机语义未改。

验证：原实现6项红测试失败；实施后15项实际C主机测试全绿（tests-green.json），包含嵌套/各阶段失败/采集服务不清缓冲/暂停态恢复/同会话ACK/暂停竞争及原手机协议回归。ESP-IDF5.5.5真实ESP32-S3编译成功，应用829424字节（0xca7f0），1MB分区剩余21%。独立storage_guard_review只读先规格后质量审查通过，无阻止交付问题。

本轮镜像已同步到git/third/firmware，包括匹配bootloader、partition_table、third.bin、flash_args、flasher_args.json、manifest.json和SHA256SUMS.txt；旧镜像备份firmware-before/、原源码before/。应用SHA256：90CD90026B3F94AFCA5B660DFCBCED2247AF8D0C294409580899DFD16BAAC2D0。

未烧录、未打开串口、未操作U盘、未修复已存在的FAT损坏，未推送GitHub。不能凭编译/主机模拟称U盘已经恢复写入或Wi-Fi/液滴时序实机验证通过。液滴中断关闭期间仍可能丢滴，写盘耗时采用用户实测较短前提，异常等待仍需留意；没有新增全局中断禁用或强制Wi-Fi断联。
