# 尿量监测仪 Android APP V1

包名 `com.niaio.mobile`，版本 `1.0-v1`，Android 8.0+（min26、target/compile34）。冻结合同：`../docs/mobile/protocol-v1.md`、`../docs/mobile/ui-v1.md`。

两页：实时监测、设备连接。展示累计尿量、流速、滴数、有效时长、测量状态、报警、电量、U盘缓存/检查时间、设备时钟。开始/暂停/继续以设备状态为准；复位需确认；请求等待阶段禁止重复点击。5秒未完成显示执行未确认，可重新连接，旧控制不重放。

先在系统设置连接设备热点，默认 `NIAIO-Monitor`（可配置）。WebSocket 固定 `ws://192.168.4.1/ws`。仅选择 `192.168.4.2..254` 的 Wi-Fi Network，OkHttp socketFactory/DNS 都绑定该 Network，禁代理和自动失败重试；不使用蜂窝fallback。后台停止释放socket/network callback/timer/executor；已请求连接的前台恢复创建新连接，重新接收完整状态。

每个新session只生成一个时分秒对时请求，无日期；未收到终态最多同req_id发送3次。accepted仅显示受理；completed/OK才显示完成。状态/ACK按boot/session隔离，seq重复不刷新数据，state_seq不可回退，超过3秒无新快照标数据陈旧。离线/陈旧保留最后数据和最后收到时间，禁用控制。

JSON解析拒绝重复键、错误类型、非法枚举、越界/非有限值、缺失字段、超1024字节；服务器非关键扩展允许。模拟演示为显著标记的静态样例，禁用测量控制；服务器SIMULATED标志/设备名亦被识别。未知电量显示--，0为0%；U盘UNKNOWN/ABSENT/READY/ERROR独立表达；时长不会24h回绕。

## 构建

使用现有工具链，不升级依赖。PowerShell：

```powershell
$env:JAVA_HOME='F:/ESP32/niaio/.toolchains/mobile-v1/jdk/jdk-17.0.20.1+1'
$env:ANDROID_HOME='F:/ESP32/niaio/.toolchains/mobile-v1/android-sdk'
$env:ANDROID_USER_HOME="$PWD/.android-user"
$env:GRADLE_USER_HOME="$PWD/.gradle-cache"
$env:JAVA_TOOL_OPTIONS='-Djavax.net.ssl.trustStoreType=Windows-ROOT -Djavax.net.ssl.trustStore=NONE'
./gradlew.bat testDebugUnitTest lintDebug assembleDebug --offline --console=plain
```

最终输出 `app/build/outputs/apk/debug/app-debug.apk`（debug签名，不含生产签名密钥）。保留本地缓存供构建，但源码交付排除 `.gradle-cache/`、`.android-user/`、`local.properties` 和任何密钥。报告和可交付APK位于 `../reports/mobile-app-v1/v1/`。

12个JVM测试通过，lint无问题，assembleDebug成功。手机未连接：实机兼容性、真实Wi-Fi/蜂窝路由、WebSocket/命令执行、测量/存储、字体/触摸/UI可视验收均 NOT_RUN。此结果只证明代码与构建验证，不代表硬件联调通过。V1不提供历史图表、账号、OTA或后台报警承诺。
