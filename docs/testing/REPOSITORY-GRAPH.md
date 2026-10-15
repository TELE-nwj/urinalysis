# 仓库分支与工程版本图

图形版见 [交互式节点连线图](graph/index.html)。下载 HTML 后可直接在浏览器打开，无需安装依赖；可切换工程版本和真实 Git 历史，点击节点查看详情，并调整缩放。

图形页面记录生成时的 Git 快照。需要刷新时，在仓库根目录运行 `python docs/testing/graph/render_visual_graph.py`。工程阶段为归档材料的版本概览，Git 连线只采用真实提交的父子关系。

此文件是归档提交之前的本机全引用快照；原生 Git 图中的引用是真实分支，工程目录单独列出。侧边栏展示的是推送完成后重新生成的最新快照。

## 真实 Git 分支（归档提交前）

```text
* 6cad8dd (HEAD -> codex/ch376-test-records-20261015, origin/main, origin/HEAD) scend sol版本：安卓APK、软件源码与ESP32固件完整资料
| * 501b7c7 (codex/mobile-app-v1) docs: record final independent firmware acceptance
| * e45b436 feat: add Android controller and ESP32 SoftAP mobile service
| * 6153f29 (codex/mobile-fw-f0, codex/mobile-dsh-simple, codex/mobile-android-e0) docs: freeze mobile app v1 contracts and approved work plan
| * 574f06c (main) second sol: 电量检测/实时时间/串口屏协议重构 + 分层隔离
| * ee753c4 second sol: 两键版固件基线快照（电量/实时时间/串口屏重构前）
|/
* 29a43b7 feat(key): 按键改为两键短按版（GPIO8/GPIO9）
* b6d1655 feat(硬件配置): 适配正式版引脚与 INA228 电量计
* b8b63e7 当前进度：分层版固件完成（串口屏 S1~S4 + U盘每分钟记录）
* ae55783 新增：测量状态机与指令协议技术文档
* bdceacf 新增：测试文档（滴落检测与串口屏）
* 096e39c 新增：第二版代码（分层 任务调度未完成 未验证）
* f697906 新增：第二版代码（分层 串口屏改完未验证 未修改U盘）
*   3e78c98 Merge remote-tracking branch 'origin/main'
|\
* | f668b00 将工程整理到 第一版代码（未分层分区） 目录
| * 6b80869 Initial commit
* 01996d9 初始化ESP32项目：GPIO落料中断检测 + CH376T U盘读写
```

## 工程目录及测试阶段（不是 Git 分支）

```text
工程目录与测试阶段（按项目版本整理，不是额外的 Git 分支）
├─ 第一版代码（未分层分区）
├─ 第二版代码（分层；任务调度未完成）
├─ 第二版代码（分层；串口屏阶段）
├─ second                           分层参考工程
├─ second sol                       本机已有目录，本次未改动
├─ scend sol版本                    10月9日 Android / ESP32 交付
├─ third                            正式基线；101文件哈希一致
└─ third_logtest_20261012            独立测试副本
   └─ 日志副本 → A地址诊断 → B兼容验证 → C会话修复
                                      → D保护候选 → E握手修复
                                                    └─ 最新：一次暂停继续＋1分钟写入回读通过
                                                       待验证：电脑TXT／连续多分钟／完整FAT
```

[测试记录总索引](CH376-20261012-15.md) · [GitHub远端分支图](https://github.com/TELE-nwj/urinalysis/network)
