#include "screen_uart.h"
#include "bsp/pin_map.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>

static const char *TAG = "screen_uart";

/* 接收缓冲大小 */
#define RX_BUF_SIZE       (1024)
#define TX_BUF_SIZE       (256)
/* 注意：screen 服务的按行解析 + monitor 的命令处理(状态机、snprintf、主动推屏)
   全部跑在本 RX 任务上下文里，所以栈不能给小，否则会栈溢出/静默卡死。 */
#define RX_TASK_STACK     (4096)
#define RX_TASK_PRIO      (6)

static screen_uart_rx_cb_t s_rx_cb = NULL;

static void uart_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[128];
    for (;;) {
        /* 阻塞读取(最长等一段空闲)，取到数据就回调 */
        int len = uart_read_bytes(BSP_UART_SCREEN_NUM, buf, sizeof(buf),
                                  pdMS_TO_TICKS(50));
        if (len > 0 && s_rx_cb) {
            s_rx_cb(buf, (size_t)len);
        }
    }
}

err_t screen_uart_init(void)
{
    /* 1. 串口参数：默认与参考实现一致(115200 8N1 无流控) */
    uart_config_t cfg = {
        .baud_rate  = BSP_UART_SCREEN_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* 2. 安装驱动 */
    esp_err_t e = uart_driver_install(BSP_UART_SCREEN_NUM, RX_BUF_SIZE,
                                      TX_BUF_SIZE, 0, NULL, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(e));
        return ERR_FAIL;
    }
    uart_param_config(BSP_UART_SCREEN_NUM, &cfg);

    /* 3. 绑定引脚 */
    uart_set_pin(BSP_UART_SCREEN_NUM, BSP_PIN_UART_SCREEN_TX,
                 BSP_PIN_UART_SCREEN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    ESP_LOGI(TAG, "screen uart%d ready: TX=GPIO%d RX=GPIO%d @%d",
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

int screen_uart_read(uint8_t *buf, size_t size, TickType_t timeout)
{
    if (!buf || size == 0) {
        return ERR_PARAM;
    }
    int len = uart_read_bytes(BSP_UART_SCREEN_NUM, buf, size, timeout);
    return len;
}

void screen_uart_set_rx_cb(screen_uart_rx_cb_t cb)
{
    s_rx_cb = cb;
    if (cb && !xTaskCreate(uart_rx_task, "uart_rx", RX_TASK_STACK, NULL,
                           RX_TASK_PRIO, NULL)) {
        ESP_LOGE(TAG, "create uart_rx task failed");
    }
}
