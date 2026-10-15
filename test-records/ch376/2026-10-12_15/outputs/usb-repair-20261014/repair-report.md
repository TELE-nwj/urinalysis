# U盘文件系统修复结果（2026-10-14）

已完成文件系统修复，没有格式化、重新分区或修改固件。

目标为Disk1 / H: / General UDisk / 4026531840字节，MBR单FAT32分区；每个介质写阶段重新核对型号、USB类型、容量、盘符及分区起点。

## 备份

完整H:分区镜像：`F:\ESP32\niaio\outputs\usb-repair-20261014\General-UDisk-H-partition-before-repair.img`，4026499072字节。

SHA256：`2B04438A9499DFC53FF8EC3AA52EB6FB61EC2887EC526218C5502B912C81F419`，顺序读取和独立文件哈希均一致，备份过程没有读取错误。当前普通进程物理盘只读访问被Windows拒绝，因此镜像包含完整分区但不含MBR；分区偏移32768字节及容量另存device-before.json。

用户已经授权修复，并在Windows管理员提示中点击“是”。已提升的repair-admin.ps1在备份长度与哈希通过后才执行CHKDSK。

## 修复和恢复

- CHKDSK H: /F /X已更正文件系统，退出1（修复完成），将6条丢失簇链、24576字节保存到`H:\FOUND.000`。
- 修复后只读CHKDSK退出0；恢复文件并读写校验之后再次CHKDSK退出0，均报告未发现问题。
- 只读原始FAT审计显示两份FAT现已完全一致，保留区及分区结构不变。
- 从修复前镜像提取6个可识别文本（5条历史分钟记录及1个仅表头文件），写回`H:\Recovered_20261014`。6个文件均重新读取并与电脑恢复副本SHA256比较一致。
- 临时文件使用CreateNew创建，Flush(true)后重新读取，内容匹配；仅删除该临时文件，未删除用户文件或FOUND.000。

恢复文件：

| 文件 | 字节 | 一分钟尿量 | 流速 |
|---|---:|---:|---:|
| REC0001.TXT | 56 | 仅表头 | — |
| REC0002.TXT | 103 | 7.5ml | 450.0ml/h |
| REC0003.TXT | 103 | 6.0ml | 360.0ml/h |
| REC0004.TXT | 104 | 26.0ml | 1560.0ml/h |
| U0000001.TXT | 103 | 10.0ml | 600.0ml/h |
| U0000002.TXT | 103 | 5.5ml | 330.0ml/h |

U0000001/2名称由异常区域的目录项识别；其他记录原始名称不可靠，使用REC编号，不能推断它们的实际先后测量编号。记录是从镜像提取后写回的文件，保留了可确认的表头与完整分钟行；原始镜像保留其余全部字节。

## 验证范围

这证明当前Windows文件系统结构及已检查文件的电脑读写正常。没有做全盘破坏性写入测试、断电/拔插持久性测试或ESP32实机复测，不能据此断言U盘所有闪存健康。

CH376误写到提前1024扇区区域的根因仍未修复。当前BPB保留区仍1056；不要用当前ESP32固件再次写该盘，以免重现目录/FAT损坏。修复本身不代表设备端写盘问题已解决。

证据：backup-manifest.json、backup-verified.json、chkdsk-repair.txt、chkdsk-after-repair.txt、chkdsk-final.txt、repair-completed.json、recovery-hashes.json、after-repair-audit/metadata.json。
