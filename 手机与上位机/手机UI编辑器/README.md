# Android 界面编辑器

运行 `start.ps1`，在侧边栏浏览器打开 `http://127.0.0.1:8768/`。服务仅监听本机。

点击预览中的标题、指标、状态或按钮选择文案；调整颜色、字号、间距、圆角；拖动四个指标或使用上下按钮排序。监测 / 设备两页都可以查看。“交互预览”仅改变示例状态，不访问ESP32。

“保存设计”写入本目录 `ui-design.json`，再次打开继续编辑。“同步到 APP 源码”只生成显示参数 `F:/ESP32/niaio/.worktrees/mobile-app-integration/mobile-app/app/src/main/java/com/niaio/mobile/UiDesign.kt`；不改变网络、协议、测量或时间同步逻辑。然后在对应Android工程重新运行 `gradlew.bat :app:assembleDebug`，安装新APK才会更新手机界面。

这里只复现界面结构和可编辑显示参数，浏览器文字度量与原生Compose可能有差异。真实安卓字体、触摸和设备适配仍需在手机上确认。所有预览数据均是示例。

固件工程位于 `F:/ESP32/niaio/git/third`，与此编辑器独立。

在本目录运行 `build-app.ps1` 可使用现有本机工具链构建；成功后生成 `niaio-mobile-ui-debug.apk`。它不会安装手机APK，也不会操作串口或烧录固件。
