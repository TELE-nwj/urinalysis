# 三页安卓 APK 实现计划

> 子智能体按下述职责执行；主智能体负责规格和代码验收、集成、构建与交付。

目标：将审核完成的三页设计制作成真实安卓 APK；新增两个更新入口，取消更新来源；不开发网络更新或 ESP32 OTA。

架构：沿用 Kotlin/Compose/OkHttp 和现有 v1 协议。独立构建副本位于 F:/ESP32/niaio/.build/mobile-ui-v2/mobile-app，不覆盖 V1 或固件。手机本地记录引擎消费已校验快照，私有文件原子持久化；前台服务保持用户发起的设备连接并通知警报。Compose 三页读取服务状态和记录，不使用预览样例。

技术栈：Android API26–34、Java17、Kotlin2.0.21、Compose Material3、OkHttp；不引入需额外下载的图表或数据库库。

## 文件及共享接口
- records/RecordModels.kt、RecordEngine.kt、RecordCsv.kt：纯 Kotlin 记录、增量及警报，records/RecordRepository.kt 为 Android 原子持久层。
- runtime/MonitorService.kt、MonitorRuntime.kt：后台连接、记录更新、通知、外部动作。
- ui/AppScreen.kt、RecordsScreen.kt、AnalysisScreen.kt、SettingsScreen.kt：真实三页与子页。
- MainActivity.kt：服务启动/绑定、通知权限、系统保存/分享；主智能体负责。
- AndroidManifest.xml、build.gradle.kts：主智能体配置权限、服务、版本2/1.1-v2。

## 任务
- [ ] 1. 核对用户设计规则并建立下面的稳定接口；并行任务只改各自目录，禁止修改固件/串口/远程账户。
- [ ] 2. 数据引擎：暂停继续同条；复位确认结束；重命名联动警报；独立删警报、清空两类记录；分钟增量/累计与小时汇总；间断不填0或均摊；持久化与CSV。以去重、暂停、复位、重命名、清除、缺口为单元测试。
- [ ] 3. Compose UI：检测单屏、连接电量仅首页；记录整框简洁卡、详情警报弹窗、分钟/小时图；四个设置入口及两个更新入口，清除更新来源/模拟演示。
- [ ] 4. Runtime：接入已有真实连接，前台 connectedDevice 服务与通知渠道；警报仅新事件通知；通知跳转关联记录；用户断开后停止连接，Activity退后台不主动断开。
- [ ] 5. 主集成：Activity、Manifest、文件保存/分享、权限；保留同应用ID和原debug签名；配置versionCode2/versionName1.1-v2。
- [ ] 6. 先规格验收后代码质量审查，修复必要问题；离线 Gradle testDebugUnitTest、lintDebug、assembleDebug。检查APK包名、版本、签名、权限、入口及哈希。
- [ ] 7. 交付APK、源码及简短安装说明；明确手机/ESP32真机测试未执行。保留侧边设计预览，不烧录固件、不安装用户手机，不重新发送旧邮件。

## 领域接口（各子任务使用同一合同）
包 com.niaio.mobile.records：
MinuteEntry(timeMs:Long, volumeMl:Double?, drops:Long?, rateMlh:Double?, cumulativeMl:Double?, cumulativeDrops:Long?, coverageSeconds:Double, partial:Boolean)
LocalRecord(id:Long,name:String,startedAtMs:Long,startKnown:Boolean,status:String,totalMl:Double,totalDrops:Long,elapsedSeconds:Long,minutes:List<MinuteEntry>)；status 为 MEASURING/ENDED/UNCONFIRMED。
LocalAlarm(id:Long,recordId:Long,timeMs:Long,code:String,endedAtMs:Long?,recordName:String)
RecordBookState(records:List<LocalRecord>,alarms:List<LocalAlarm>,nextRecordId:Long,nextAlarmId:Long)
RecordEngine(initial:RecordBookState=RecordBookState())，val state:RecordBookState；accept(deviceId:String,sample:Snapshot,wallMs:Long):List<LocalAlarm>（返回新警报）；disconnected()；rename(id:Long,name:String)；deleteRecord(id:Long):Boolean；deleteAlarm(id:Long)；clearAll()。
RecordRepository(context:Context)，load():RecordBookState，save(state:RecordBookState)。
RecordCsv.export(state:RecordBookState):String（所有检测分钟明细及警报，正确CSV转义/公式注入防护）；RecordAggregation.hourly(record:LocalRecord):List<MinuteEntry>。

包 com.niaio.mobile.runtime：
MonitorRuntime.probe: Compose State<ProbeState>；book: State<RecordBookState>；selectedRecord: State<Long?>。
MonitorRuntime.connect(context:Context)、disconnect(context)、command(action:String)、clearLog()、renameRecord(id,name)、deleteRecord(id)、deleteAlarm(id)、clearAll()、selectRecord(id:Long?)。

UI入口 com.niaio.mobile.ui.AppScreen(probe:ProbeState,book:RecordBookState,requestedRecordId:Long?,actions:AppActions)。
AppActions(onConnect:()->Unit,onDisconnect:()->Unit,onCommand:(String)->Unit,onWifiSettings:()->Unit,onNotificationSettings:()->Unit,onClearLog:()->Unit,onRename:(Long,String)->Unit,onDeleteRecord:(Long)->Unit,onDeleteAlarm:(Long)->Unit,onClearAll:()->Unit,onExport:()->Unit,onRecordOpened:()->Unit)。

边界：非真实开始只标首次接入时间；没有测量ID，离线状态变更不拼接确定记录。复位最终量只能最后已接收值，不伪称固件结束摘要。安卓权限/厂商省电不能承诺24h必达。清空本地不清零检测读数；下一次接收可建立接入记录。更新入口点击说明本版本尚未开放，不伪造检查最新版。
