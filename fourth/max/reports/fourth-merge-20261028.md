# fourth 合并记录（2026-10-28）

## 做了什么

以 `third`（2026-10-12 交付状态）为基线，把测试工程 `third_logtest_20261012`
的 **E 版写盘握手修复**保真合并进 `fourth`。

- `third` **未改动**（源文件 mtime 仍为 2026-10-12 17:10）
- `third_logtest_20261012` **未改动**（作为只读参考源）
- 合并后 `fourth` 的相关源码与测试版**逐字节一致**

## 合并范围（11 个文件）

| 文件 | 说明 |
|---|---|
| `main/driver/ch376/ch376_diag.c` / `.h` | 新增（401 + 16 行） |
| `main/driver/ch376/ch376_spi.c` | +83 行 |
| `main/service/storage/storage.c` | +49 行 |
| `main/service/storage/storage.h` | +2 行 |
| `main/service/storage/README.md` | +5 行 |
| `main/app/S1/S1.c` | +15 行 |
| `main/app/monitor/monitor.c` | +2 行 |
| `main/main.c` | +1 行 |
| `main/app/README.md` | +1 行 |
| `main/CMakeLists.txt` | +1 行（登记 ch376_diag.c） |

顶层 `CMakeLists.txt`、`build.ps1`、`flash.ps1`、`idf-env.ps1`、`sdkconfig`、
`sdkconfig.defaults`、`.gitignore` 均与 `third` 相同，未改。

## 逐字节一致性核对

对以上 11 个文件执行 `diff -u fourth/<file> third_logtest_20261012/<file>`，
**全部无输出（exit 0）**。

## 构建

用本机 ESP-IDF v5.5.5 构建（esp32s3，MINIMAL_BUILD）：

```
Project build complete.
third.bin binary size 0xceeb0 bytes (847,536)
Smallest app partition is 0x100000 bytes. 0x31150 bytes (19%) free.
```

与测试版 `firmware/third.bin` **同尺寸 847,536 字节**（SHA 不同属正常：
编译时刻被嵌入固件）。

> 注：构建前删除了从 `third` 整目录复制过来的 `build/`，
> 因为其 `CMakeCache.txt` 绑死的是 `F:/ESP32/niaio/git/third`，
> 直接复用会让 CMake 报缓存目录不匹配。

## 交付镜像已更新

`firmware/` 已用本次构建产物覆盖，并重算校验：

| 偏移 | 文件 | 字节 | SHA256 |
|---|---|---|---|
| `0x0` | `bootloader/bootloader.bin` | 20,832 | `0887C2A1…8668132` |
| `0x8000` | `partition_table/partition-table.bin` | 3,072 | `7F00B6C0…5BB7C820` |
| `0x10000` | `third.bin` | 831,472 | `1E63ABEF…37D6F32D` |

`flash_args` / `flasher_args.json` 与测试版一致，无需改动。

> 注：上表是**清理日志后**重新构建的产物（详见下节）。

## 日志清理（同日后续）

交付后用户提出：测试版为排查加的日志太吵，只保留 `third` 原版的简洁日志。

| 文件 | 清理前 | 清理后 | third 原版 |
|---|---|---|---|
| `ch376_diag.c` | 52 | **0** | （新增模块，无对应文件） |
| `ch376_spi.c` | 53 | **50** | 50 |
| `storage.c` | 57 | **21** | 29 |
| `S1.c` | 17 | **9** | 9 |
| `main.c` | 11 | **10** | 10 |
| `monitor.c` | 4 | **3** | 3 |

说明：

- `storage.c` 少 8 条是**功能改写导致**，不是删多了：`文件不存在，现场创建`、`磁盘挂载失败继续尝试`、
  `落盘验证通过/失败` 等分支被 E 版的新逻辑替换
- `ch376_diag.c` 删日志时连带删掉了 2 个**纯日志函数**（`diag_geometry_snapshot`、`CH376_DiagFile`），
  该文件 `return` 语句数量前后均为 **38**，所有写盘拦截守卫未动
- `S1.c` 的 CH376 日志级别已从 `INFO` 改回 `WARN`
- `monitor.c` 现已与 `third` **逐字节一致**

安全性佐证：

- `wait()` 与 `third` **逐字节相同**（原版 4 条中断日志已恢复）
- `0x2E→0x3D` 数据交接窗口内 `CH376_Diag*` 调用数为 **0**（D 版失败的根本原因未被重新引入）
- 编译 exit 0，`third.bin` 847,536 → **831,472 B**（third 原版 829,424 B，差 2 KB 即 E 版功能代码）

## 尚未完成

- **未烧录，未做实机验证**。真机验收步骤见 `README.md` 与测试版的 E 版说明。
- U 盘上已有的 FAT 损坏不会被固件修复，仍需单独处理。
