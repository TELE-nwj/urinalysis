# third 交付验证 — 2026-10-09

交付目录：`F:/ESP32/niaio/git/third`。
源目录：`F:/ESP32/niaio/.worktrees/mobile-app-integration/second sol`。
创建前确认目标不存在；创建时再次检查，并拒绝覆盖已有目录。原源目录及其他目录未修改。

## 源码一致性

- `main/` 共 60 个文件逐文件 SHA256 相同，既无缺失文件也无额外文件。
- `sdkconfig` 和 `sdkconfig.defaults` 与最终集成目录相同；构建后再次确认一致。
- 顶层 `CMakeLists.txt` 仅由 `project(project)` 改为 `project(third)`，其余内容相同。
- 未复制任何原构建目录、CMake 缓存、原 `flash.bat`、IDE 配置或串口默认设置。
- 新增的 README、环境/构建/烧录脚本及校验报告只服务于本目录交付。

逐文件记录：`source-comparison.json`。

## 真实构建

执行 `F:/ESP32/niaio/git/third/build.ps1`，退出码 **0**。
脚本实际使用已安装的 ESP-IDF 5.5.5/Python/Xtensa 工具链，执行：

```text
idf.py -C F:/ESP32/niaio/git/third -B F:/ESP32/niaio/git/third/build -D IDF_TARGET=esp32s3 build
```

项目名：`third`；芯片：ESP32-S3；`MINIMAL_BUILD` 开启。
本地生成 `build/third.bin`：828688 字节（`0xca510`）。
1 MiB app 分区剩余 `0x35af0` 字节，即构建报告所示 21%。
构建日志：`build.log`，没有编译 warning/error。

## 匹配镜像

`firmware/` 的三个镜像与本目录该次构建输出逐文件一致；并复制该次构建实际生成的
`flash_args` 和 `flasher_args.json`。不是其他工程镜像与新缓存混用。

| 文件 | 偏移 | 字节数 | SHA256 |
| --- | --- | --- | --- |
| `third.bin` | `0x10000` | 828688 | `B69439B320EAA030D4BAA34DEAE320E5759257B24A01548C72DF0029F8AA3E9C` |
| `bootloader/bootloader.bin` | `0x0` | 20832 | `A2C8EF73163C7AF5DBE782474B270164C3701D277379C4E577D37E8F85E2F5ED` |
| `partition_table/partition-table.bin` | `0x8000` | 3072 | `7F00B6C042A89B15B0CAC534F82ED988CAF29278FF5700B0C511EB1B5BB7C820` |

Flash 参数：DIO、80 MHz、2 MB。分区表：NVS `0x9000`/24 KiB，PHY `0xf000`/4 KiB，
factory app `0x10000`/1 MiB。预编译校验清单位于 `firmware/manifest.json` 和
`firmware/SHA256SUMS.txt`。

只读执行 `esptool --chip esp32s3 image_info firmware/third.bin`，退出码 0：镜像 checksum
及 validation hash 均 valid，见 `image-info.txt`。只读解析分区表退出码 0，
见 `partition-table.txt`。元数据芯片为 esp32s3、app 文件为 third.bin，所有引用镜像存在。

## 脚本检查与硬件边界

三个 PowerShell 脚本均经 AST 语法检查通过，见 `script-validation.txt`。
`flash.ps1` 的 Port 参数为 Mandatory 且无默认值；不猜测 COM21，不枚举串口。
默认使用本目录 `build/flash_args`，`-UsePrebuilt` 显式改用匹配交付镜像。
烧录脚本先检查目标芯片、镜像名称及文件范围/存在性。**该烧录脚本未执行**。

下载串口识别、板上启动、Android 手机连板、真实 Wi-Fi/USB/按键、RTOS 高水位均
**NOT_RUN**。未烧录、未占串口、未改变 Git 历史或其他工程。源码已有的软件测试与
独立审查结果来自最终集成版本；第三目录此次只变位置和构建名称，没有业务改动。
