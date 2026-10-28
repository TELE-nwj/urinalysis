#ifndef DRIVER_MOBILE_LINK_H
#define DRIVER_MOBILE_LINK_H

#include <stddef.h>
#include <stdint.h>
#include "bsp/err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明的代际令牌,绝不是套接字描述符。零值无效。 */
typedef uint32_t mobile_link_peer_t;

typedef struct {
    void (*on_open)(mobile_link_peer_t peer, void *ctx);
    void (*on_text)(mobile_link_peer_t peer, const char *text, size_t len, void *ctx);
    void (*on_close)(mobile_link_peer_t peer, void *ctx);
    void *ctx;
} mobile_link_callbacks_t;

typedef struct {
    uint16_t port;
    const char *ws_path;
    size_t max_frame_size;
} mobile_link_config_t;

/* 需要 CONFIG_HTTPD_WS_SUPPORT、CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT
 * 以及 CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT。
 * 所有字段均为必填;max_frame_size 必须落在 [1,1024] 内。配置
 * 结构与回调结构都会被复制。在 stop 返回前,回调 ctx 归调用者所有。
 * 文本指针仅在 on_text 期间有效;len 才是权威长度。
 * 回调运行在 HTTPD 任务上、位于驱动锁之外,并且可以调用 send。
 * start/stop 必须由唯一的外部生命周期所有者任务串行化。并发的
 * 生命周期调用不在本契约范围内(start 返回 ERR_BUSY,stop 会
 * 忽略状态转换)。从回调内部调用 stop 是显式的空操作,用于
 * 防止 HTTPD 自死锁。其它所有回调都可安全地调用 send。
 */
err_t mobile_link_start(const mobile_link_config_t *config,
                        const mobile_link_callbacks_t *callbacks);

/* 复制负载并入队投递。ERR_OK 只表示已入队,不表示已送达。
 * 最多四个未完成帧;ERR_BUSY 用于施加背压。若某个已入队帧的
 * 代际/会话在执行之前就已关闭,该帧会被丢弃。
 */
err_t mobile_link_send_text(mobile_link_peer_t peer, const char *text, size_t len);

/* 当串行化的外部生命周期所有者调用返回时,已无回调在执行中,
 * 已入队的负载也已被回收。若已处于停止状态则为空操作。
 * SDK 关闭失败会在保留全部回调状态的前提下重试;
 * 永久性的 SDK 失败将阻塞 stop(或 start 的注册回滚)。
 */
void mobile_link_stop(void);

#ifdef __cplusplus
}
#endif
#endif
