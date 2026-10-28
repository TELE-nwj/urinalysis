# scend sol版本

ESP32-S3 尿量监测仪与安卓 APP 完整资料，整理日期：2026-10-09。

| 目录 | 内容 |
| --- | --- |
| android/ | 可安装 APK 1.1-v2、安卓源码、构建脚本、测试与 Lint 报告 |
| esp32/ | 当前 third 固件完整 ESP-IDF 工程、已编译镜像、烧录脚本、SHA256 与构建验证 |
| firmware-tests/ | 固件协议与分层测试资料 |
| docs/ | 手机通信协议、三页设计与实现规格 |
| ui-design/ | 可交互设计预览及截图，数据为样例，不是真机截图 |

## 安卓安装与使用

安装 [niaio-mobile-v2-debug.apk](android/niaio-mobile-v2-debug.apk)。包名 `com.niaio.mobile`，版本 `1.1-v2`，versionCode=2，最低 Android8。与此前 V1 的 debug 签名一致，可以覆盖安装。手机连接设备 Wi-Fi 后，APP 检测页点击快速连接；允许通知权限后可接收手机在线期间的新警报。

APP 包含检测、记录、设置三页，手机本地独立记录、分钟/小时图表、警报、CSV 导出与分享。APP 更新和设备固件更新目前只有入口，远程更新尚未开放，不提供更新来源选项。

## ESP32 编译与烧录

工程在 [esp32/](esp32/README.md)，内部工程名保持 `third`，避免改变已验证的镜像与烧录元数据。使用 ESP-IDF5.5.5、ESP32-S3；已编译镜像在 `esp32/firmware/`。先确认实际下载串口，在 PowerShell 的 esp32 目录执行 `./flash.ps1 -Port COM7 -UsePrebuilt`，COM7 必须替换为实际下载口。其他电脑需调整 `idf-env.ps1` 中本机工具链路径。完整步骤与镜像偏移见固件 README，不能只烧录应用 bin。

## 验证及边界

Android：testDebugUnitTest、lintDebug、assembleDebug 成功，28项单元测试全部通过，Lint0错误、2警告；APK签名校验通过。ESP32：此处为此前已真实编译的匹配固件，APP V2 制作没有修改或重新烧录固件。

手机和 CH376T U 盘记录完全独立，手机改名/删除/导出不改 U 盘。断线不补传历史，数据缺失不伪造补零。现有固件没有稳定检测编号与结束摘要，重新接入与复位最终值存在已说明边界。手机进程恢复时此前未结束记录标状态未确认；强杀可能丢失最近约10秒尚未落盘更新。

本版本是可安装测试版，未执行华为 nova14 真机安装、设备联机与锁屏通知验证。未包含签名私钥、个人凭据、构建缓存或工具链；开发热点测试密码为固件自带配置。

文件完整性见 VERSION_MANIFEST.json，各主要二进制亦有各自 SHA256 文件。后续修改以此版本为起点。
