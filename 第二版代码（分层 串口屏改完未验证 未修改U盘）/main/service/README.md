# service 层 — app ↔ driver 桥接服务

> 每个可复用服务一个独立子目录（与 driver 的组织方式一致）。
> 本层把底层硬件封装成"语义化、可复用"的服务，供 app 层(monitor)调用。
> 均为**被动服务**：不建任务、不主动轮询、不依赖 app 层。

| 子模块 | 目录 | 职责 | 说明文档 |
|---|---|---|---|
| 串口屏服务 | `screen/` | 组帧发送 + 接收回传解码(桥接 driver/screen_uart) | `screen/README.md` |
| U盘存储 | `storage/` | U盘文件写入(桥接 driver/ch376) | `storage/README.md` |
| 尿量计量/判断 | `urine/` | 尿量解算+报警判断(纯计算) | `urine/README.md` |

依赖方向：`service 依赖 driver`；app 依赖 service；**service 不反向依赖 app**。

## 各模块被谁使用
- `screen` ← `app/monitor.c`（显示 + 注册回传事件回调）
- `storage` ← `app/monitor.c`
- `urine`   ← `app/monitor.c`
