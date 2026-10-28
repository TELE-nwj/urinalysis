# 尿量监测仪 Android V2

安装文件：niaio-mobile-v2-debug.apk；版本1.1-v2，versionCode2，Android8及以上。包名com.niaio.mobile，与此前V1使用同一个debug签名，可覆盖安装。这是可安装测试版。

已实现检测/记录/设置三页、手机本地记录及分钟/小时图、记录改名/删除、独立警报删除/查看、CSV保存和分享、前台设备连接与状态栏警报。首次使用先连接ESP32热点，再点快速连接；通知权限需在手机上允许。此版本只保留APP更新和设备固件更新入口，远程更新尚未开放；已取消更新来源和模拟演示入口。

检查：Gradle testDebugUnitTest、lintDebug、assembleDebug成功；28项单元测试全部通过；Lint0错误、2项警告（既有Compose参数顺序和仅保留applicationContext的静态引用提示）。APK签名校验通过、签名与V1一致。

SHA256：FFF4276C61EA3980120AD6D81BEB88370F54912CF2FB954ABB8AC45F205CA808

手机与U盘独立保存，手机清除不改U盘或复位设备。断线不补传历史，缺失显示缺失或部分数据。进程重新启动时此前未结束记录标状态未确认；新接入以首次接入时间记录，不能冒充设备真实开始时间。系统强制停止可能丢失最后约10秒未落盘更新。手机真机安装、nova14布局、实际ESP32联机与锁屏通知未执行验证；本轮未修改或烧录ESP32固件。

源码在source/mobile-app，构建脚本build-v2.ps1使用本机已有Java17/AndroidSDK及缓存。报告在reports。侧边网页仍为样例设计预览，并非APK真机画面。
