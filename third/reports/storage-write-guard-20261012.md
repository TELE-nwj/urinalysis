# 写盘保护更新（2026-10-12）

正式工程已实现写盘时暂关液滴中断与状态推送，Wi-Fi/WebSocket连接保持，storage日志和写盘完成/失败提示保留。退出后恢复原采集开关，不清除采集间隔。

15项主机回归测试通过、ESP32-S3真实编译通过、独立只读审查通过。新镜像已同步firmware及SHA清单，第三方驱动与记录格式未改。详细修改备份在F:/ESP32/niaio/outputs/storage-write-guard-20261012。

未烧录或执行U盘写入验证，已有FAT损坏未修复，不能宣称无文件问题已解决。此前delivery-verification.md为V1历史记录；当前镜像以firmware/manifest.json及SHA256SUMS.txt为准。
