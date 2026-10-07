# service/storage：按测量编号保存记录

新测量时扫描 U 盘已有的 `U0000001.TXT` 等大写 8.3 文件名，分配下一个空编号并写入表头。暂停继续沿用同一文件；每个整分钟打开、定位末尾、追加一行并关闭。记录包含有效测量时间、本分钟尿量及本分钟平均速率。

无 U 盘时仍可测量。应用层约每 5 秒重试介质连接与文件准备，恢复后从当前分钟起继续记录；此前未写入的分钟不缓存、不补写。`storage_create_session` 分配文件，`storage_ensure_session` 恢复文件，`storage_open_session`、`storage_write_open` 和 `storage_close_session` 完成每分钟追加。

CH376 通过 GPIO 软 SPI 访问 U 盘。检测介质的 `0x30` 命令后必须重新挂载 `0x31`，而且只能在无文件打开时执行。动态中文表头和 8.3 文件名在电脑端仍须检查实际 U 盘内容。
