#include "screen_uart.h"
#include "bsp/pin_map.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>

static const char *TAG = "screen_uart";

/*
 * 【为什么只发不收】
 * 本工程屏端不再向 MCU 发任何命令，MCU 侧也没有解析屏数据的逻辑，收进来的字节
 * 只会白占内存和 CPU。因此这一版：
 *   - 删除了接收回调与同步读接口，以及原来的接收任务(含 4KB 任务栈)；
 *   - 固件从不调用 uart_read_bytes、也不注册任何 RX 事件。
 *
 * ⚠ 注意：**不能**靠"把 rx_buffer_size 传 0"来省掉 RX 缓冲。
 *   ESP-IDF 在 uart_driver_install() 里做了硬性校验
 *   (components/esp_driver_uart/src/uart.c 的 uart_driver_install)：
 *       rx_buffer_size 必须 > UART_HW_FIFO_LEN(端口)，否则直接返回 ESP_FAIL；
 *       tx_buffer_size 才允许为 0。
 *   ESP32-S3 的 UART1 FIFO 是 128 字节，所以 RX 缓冲必须给 >128 的值(这里给 256)。
 *   真正的"只收不发"是靠"分配了缓冲但永远不读"实现的：屏端若发数据，只会把这
 *   256 字节填满，之后由驱动丢弃，固件侧完全无感。
 * 好处：仍然省掉了一个接收任务与"RX 回调里发屏"和"其它模块发屏"之间的并发竞争
 *   ——发送路径只剩一条。
 */
#define RX_BUF_SIZE  (256)   /* 必须 > UART FIFO(128)；固件不读它，仅满足驱动校验 */
#define TX_BUF_SIZE  (256)   /* 发送环形缓冲：屏指令都很短，256 字节足够 */

err_t screen_uart_init(void)
{
    /* 1. 串口参数：115200 8N1 无流控(与屏端工程设置一致) */
    uart_config_t cfg = {
        .baud_rate  = BSP_UART_SCREEN_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 2. 安装驱动：RX 缓冲必须 > 128(见文件头说明，传 0 会被驱动直接拒绝)，
     *    这个缓冲固件永远不读——"只发不收"是靠不读实现的，不是靠不分配。
     *    发送仍需缓冲故 tx_buffer_size 给 256；不用事件队列，传 0/NULL。 */
    esp_err_t e = uart_driver_install(BSP_UART_SCREEN_NUM, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "UART 驱动安装失败: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    /* 3. 配置参数与绑定引脚：这两步都必须判返回值——波特率/引脚配错了，屏上一个
     *    字都不会显示，日志却写着"ready"，现场根本无从判断。失败就地卸载驱动
     *    (回滚安装)再上报，避免留下一个半配置的 UART。 */
    e = uart_param_config(BSP_UART_SCREEN_NUM, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "UART 参数配置失败: %s", esp_err_to_name(e));
        uart_driver_delete(BSP_UART_SCREEN_NUM);
        return ERR_FAIL;
    }

    /* TX 必须绑；RX 按引脚表照样绑上，但固件永远不读它(数据被驱动丢弃)，
     * 保留绑定只是为了将来排查硬件时不用改配置。 */
    e = uart_set_pin(BSP_UART_SCREEN_NUM, BSP_PIN_UART_SCREEN_TX,
                     BSP_PIN_UART_SCREEN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "UART 引脚绑定失败: %s", esp_err_to_name(e));
        uart_driver_delete(BSP_UART_SCREEN_NUM);
        return ERR_FAIL;
    }

    ESP_LOGI(TAG, "串口屏 UART%d 就绪(只发不收): TX=GPIO%d RX=GPIO%d @%d",
             BSP_UART_SCREEN_NUM, BSP_PIN_UART_SCREEN_TX,
             BSP_PIN_UART_SCREEN_RX, BSP_UART_SCREEN_BAUD);
    return ERR_OK;
}

err_t screen_uart_send(const void *data, size_t len)
{
    if (!data || len == 0) {
        return ERR_PARAM;
    }
    int w = uart_write_bytes(BSP_UART_SCREEN_NUM, data, len);
    return (w < 0) ? ERR_FAIL : ERR_OK;
}

err_t screen_uart_send_str(const char *str)
{
    if (!str) {
        return ERR_PARAM;
    }
    return screen_uart_send(str, strlen(str));
}
