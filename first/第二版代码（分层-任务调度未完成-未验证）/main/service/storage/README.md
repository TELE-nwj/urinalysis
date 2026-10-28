# service/storage — U盘存储服务（桥接 driver/ch376）

> 向 app 层(monitor) 提供极简"初始化 + 写文本"接口，屏蔽 CH376 底层细节。
> 是 `driver/ch376` 的唯一上层调用方。

## 接口
| 接口 | 说明 |
|---|---|
| `storage_init()` | CH376 进主机模式 + 检测磁盘，成功置就绪 |
| `storage_write_text(text)` | 打开 `/ASD.TXT` → 写入 → 关闭 |

## 已知缺口(TODO)
- 为"创建(覆盖)后写"，每次会**覆盖整个 `ASD.TXT`**，无法累积历史日志；
  如需追加需先定位文件末尾再写。
- 文件名 `/ASD.TXT` 硬编码在 ch376 驱动内，storage 无法更换文件名。
