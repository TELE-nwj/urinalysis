# NIAIO 手机协议 V1（冻结合同）

用户于2026-10-08批准工作计划。目标Android APK；ESP32 SoftAP，手机连接设备Wi-Fi；WebSocket地址ws://192.168.4.1/ws。解析、校验和打包归service/mobile。单控制会话。

## 通用约定

UTF-8 JSON对象，每帧最大1024字节；v必须为整数1。拒绝非法JSON、重复对象键、未知type/命令、不符合类型的字段、非有限数、越界数值。允许服务器消息增加非关键扩展字段，App忽略未知字段。请求未知字段拒绝，避免拼写错误变成无效控制。

ID为1..64字符ASCII标识（字母、数字、短横线、下划线）。req_id为1..2147483647整数，App按会话递增；不得使用浮点req_id。boot_id在设备启动时生成，session_id在WS连接建立时生成；旧会话消息不能改变新会话。所有时间戳sample_uptime_ms/storage_checked_uptime_ms为设备单调毫秒。

## ESP32→App

hello：{"v":1,"type":"hello","device_id":"NIAIO-1234","boot_id":"boot-1234","session_id":"session-1234"}

status：{"v":1,"type":"status","boot_id":"boot-1234","session_id":"session-1234","seq":1,"state_seq":0,"sample_uptime_ms":1500,"total_ml":125.0,"rate_mlh":60.0,"total_drops":250,"elapsed_s":2538,"state":"MEASURING","alert":"A","battery_percent":74,"storage_state":"UNKNOWN","storage_checked_uptime_ms":null,"clock_hms":"18:30:25","time_synced":true}

seq为每个新快照递增，重复发送同一快照不能递增；state_seq为完成一次改变测量状态或复位的操作后递增。设备重启后可归零，App按boot_id重新建状态。电量为0..100整数或null；storage_checked_uptime_ms未知为null。累计量、滴数、elapsed_s非负，elapsed_s不按24h回绕。

state：IDLE/MEASURING/PAUSED。alert：A正常/B无尿/C少尿/D多尿/E尿袋满，沿用monitor实际输出。storage_state：UNKNOWN/ABSENT/READY/ERROR，错误不能伪装缺盘。clock_hms严格HH:mm:ss，墙钟24h回绕。

ack：{"v":1,"type":"ack","boot_id":"boot-1234","session_id":"session-1234","req_id":1,"phase":"completed","result":"OK","state":"MEASURING","state_seq":1}

phase：accepted/completed/rejected。result：OK/BUSY/EXPIRED/INVALID_ARGUMENT/NOT_READY/INTERNAL_ERROR。accepted仅表明已入队；完成后才completed。无法解析出合法req_id的消息直接用error：{"v":1,"type":"error","result":"INVALID_ARGUMENT"}，不捏造req_id。

## App→ESP32

time_sync：{"v":1,"type":"time_sync","session_id":"session-1234","req_id":1,"hour":18,"minute":30,"second":25}

command：{"v":1,"type":"command","session_id":"session-1234","req_id":2,"action":"start"}

action仅start/pause/reset。start在IDLE开始、PAUSED继续、MEASURING不变；pause仅MEASURING暂停，其他不变；reset在任何状态清零并回IDLE。App禁止使用toggle；实体K1可有内部toggle命令。

通信建立后App发送一次时分秒；严格hour0..23、minute/second0..59。收到completed/OK后同会话不再对时。未获完成允许同req_id最多3次尝试；去重保证同一请求只对时一次。新会话重新对时。对时不影响有效测量时长。

同会话同req_id同内容返回缓存结果，不重复执行。相同ID不同内容拒绝INVALID_ARGUMENT。最近32条缓存；连接结束清理会话缓存。排队上限8，入队时赋5秒执行前有效期；开始执行后不强制中断存储事务。App5秒未收到completed显示执行未确认，断线不重放旧控制命令。

每秒发送当前完整快照，状态改变安排立即发送。若旧快照重复发送，保留seq和sample_uptime_ms；App超过3秒没有新快照显示数据陈旧，WS断开显示离线。ACK不得令App回退state_seq。网络发送与快照复制互不持锁。

## 网络层公共接口（F0/F2合同）

driver/wifi/wifi.h：wifi_ap_config_t包含ssid/password/channel/max_connections；err_t wifi_ap_start(const wifi_ap_config_t*); void wifi_ap_stop(void); bool wifi_ap_is_running(void)。参数由service传入，不include上层service头文件。重复init处理已有event loop；NVS初始化失败必须报告，禁止自动erase用户NVS。

driver/mobile_link/mobile_link.h：mobile_link_peer_t为uint32_t代数令牌，不是裸fd；mobile_link_callbacks_t含on_open(peer,ctx)、on_text(peer,const char*,size_t,ctx)、on_close(peer,ctx)和void*ctx。mobile_link_config_t含port(uint16_t)、ws_path(const char*)、max_frame_size(size_t)。err_t mobile_link_start(const mobile_link_config_t*,const mobile_link_callbacks_t*); err_t mobile_link_send_text(peer,const char*,size_t); void mobile_link_stop(void)。只支持一会话，第二会话拒绝；发送在httpd上下文安排并再次检查peer代数，不能把旧请求发到重用fd的新连接。回调不能在驱动锁内调用。收到binary/超长帧明确拒绝或关闭该连接，不阻塞主任务。

F0独立测试工程允许应用测试main以明确SIMULATED标记生成hello/测试数据，做echo连接测试；不把测试业务塞进生产driver。F0不触及尿量硬件、按键、CH376或生产main。
