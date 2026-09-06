# driver 层 — 底层硬件驱动

> 每个外设一个独立子目录，向 service 层提供功能接口，内部实现硬件细节。

| 子模块 | 目录 | 状态 | 说明文档 |
|---|---|---|---|
| U盘(USB主机) | `ch376/` | 可用 | `ch376/README.md` |
| 液滴检测 | `drop/` | 可用 | `drop/README.md` |
| 按键×4 | `key/` | 可用(**暂未接入业务**) | `key/README.md` |
| 串口屏 UART | `screen_uart/` | 可用 | `screen_uart/README.md` |
| 电池 | `battery/` | **占位 stub** | `battery/README.md` |
| 时间服务 | `time_svc/` | 可用(底层取时) | — |

引脚/外设号统一在 `bsp/pin_map.h` 收敛，换板只改 bsp。

## 各驱动被谁使用
- `ch376` ← `service/storage/storage.c`
- `drop` ← `app/monitor.c`（经 service 使用或直接？——由 monitor 读液滴，见下）
- `screen_uart` ← `service/screen/screen.c`
- `time_svc` ← `main.c` / `app/monitor.c`
- `key`、`battery` ← 暂未接入业务（key 接口保留在驱动层，battery 为占位）

## 依赖方向
`app → service → driver`；driver 只被 service/main 使用，不反向依赖。
