
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "dorp.h"
#include "freertos/portmacro.h"
#include "ch376_spi.h"

// 定义锁
portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

static const char *TAG = "MAIN";
// 此处为全局变量
volatile bool interrupt_flag = false;
volatile uint32_t interrupt_count = 0;
volatile uint64_t last_interrupt_time = 0;
volatile uint64_t current_time = 0;
volatile uint8_t Time[255];
volatile bool Lack_flag = 0;


void Lack()
{
    uint16_t sum = 0; 
    for (uint16_t i=0;i<255;i++)
    {
        if (Time[i]>4)sum = sum+1;
    }
    if (sum > 100)
    Lack_flag = 1;
}

void CH376T_WR()
{
    // 进入临界区（关中断）
    portENTER_CRITICAL(&my_spinlock);
    //检查芯片是否存在
    uint8_t count = 10;
    while(count > 0)
    {
        if(CH376T_CheckExist()==1)break;
        else {
            ESP_LOGI(TAG, "芯片不在线");
            count--;
        }
    } 
    while(count > 0)
    {
        if(CH376T_SetUsbMode()==1)break;
        else {
            ESP_LOGI(TAG, "设置芯片模式失败");
            count--;
        }
    }     
    while(count > 0)
    {
        if(CH376T_CMD_DISK_CONNECT()==1)break;
        else {
            ESP_LOGI(TAG, "USB未连接");
            count--;
        }
    }    
    while(count > 0)
    {
        if(CH376T_SET_FILE_NAME()==1)break;
        else {
            ESP_LOGI(TAG, "文件打开失败");
            count--;
        }
    }    
    while(count > 0)
    {
        if(CH376T_BYTE_WRITE()==1)break;
        else {
            ESP_LOGI(TAG, "写文件失败");
            count--;
        }
    }
    while(count > 0)
    {
        if(CH376T_FILE_CLOSE()==1)break;
        else {
            ESP_LOGI(TAG, "关闭文件失败");
            count--;
        }
    }      
    //进入睡眠模式
    CH376T_EnterSleep();
    // 退出临界区（开中断）
    portEXIT_CRITICAL(&my_spinlock);
}

void Time_Calculation()
{
    
    if (interrupt_flag)
    {
        interrupt_flag = false;
        Time[interrupt_count] = Time_Difference(current_time,last_interrupt_time);
        if (interrupt_count == 255)
        {
            interrupt_count = 0;
            Lack();
        }
    }    
}
void app_main(void)
{
    ESP_LOGI(TAG, "GPIO中断示例启动");
    CH376_PORT_INIT();
    Z_Init_IT_GPIO();
    CH376T_Get_IC_Ver();
    
    
    while (1) {
      CH376T_FILE_CREATE();
    //   Time_Calculation();
    //   vTaskDelay(pdMS_TO_TICKS(1000));  // 延时1秒
    }
}