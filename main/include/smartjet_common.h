/**
 * @file smartjet_common.h
 * @brief SmartJet v2.0 公共头文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 定义系统全局数据结构、常量和枚举类型
 */

#ifndef SMARTJET_COMMON_H
#define SMARTJET_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================================
// 系统版本和配置常量
// ================================================================================
#define SMARTJET_VERSION_MAJOR      2
#define SMARTJET_VERSION_MINOR      0
#define SMARTJET_VERSION_PATCH      0
#define SMARTJET_VERSION_STRING     "2.0.0"
#define SMARTJET_BUILD_DATE         __DATE__ " " __TIME__

// 硬件版本
#define HARDWARE_VERSION            "v2.0"
#define MCU_TYPE                    "ESP32-S3"

// 系统配置
#define MAX_DEVICE_NAME_LEN         64
#define MAX_PRODUCT_ID_LEN          32
#define MAX_DEVICE_KEY_LEN          64
#define MAX_WIFI_SSID_LEN           32
#define MAX_WIFI_PASS_LEN           64
#define MAX_ERROR_MSG_LEN           128

// 任务优先级定义
#define TASK_PRIORITY_SYSTEM        24      // 系统管理任务（最高优先级）
#define TASK_PRIORITY_ENGINE        20      // 发动机控制任务
#define TASK_PRIORITY_SENSOR        15      // 传感器监控任务
#define TASK_PRIORITY_CLOUD         12      // 云通信任务
#define TASK_PRIORITY_DISPLAY       10      // 显示UI任务
#define TASK_PRIORITY_BATTERY       8       // 电池监控任务
#define TASK_PRIORITY_OTA           5       // OTA升级任务

// 任务栈大小定义 (字节)
#define TASK_STACK_SIZE_SYSTEM      4096
#define TASK_STACK_SIZE_ENGINE      3072
#define TASK_STACK_SIZE_SENSOR      3072    // 增加传感器任务堆栈大小以防止溢出
#define TASK_STACK_SIZE_CLOUD       4096
#define TASK_STACK_SIZE_DISPLAY     3072
#define TASK_STACK_SIZE_BATTERY     3072    // 增加电池监控任务堆栈大小以防止溢出
#define TASK_STACK_SIZE_OTA         8192

// ================================================================================
// 硬件GPIO定义 - 根据最新硬件设计更新
// ================================================================================

// 发动机控制
#define GPIO_ENGINE_STARTER_RELAY   GPIO_NUM_40     // 发动机启动继电器控制引脚
#define GPIO_ENGINE_KILL_OUTPUT     GPIO_NUM_9      // 熄火输出
#define GPIO_ENGINE_RPM_INPUT       GPIO_NUM_2      // 发动机转速脉冲输入
#define GPIO_ENGINE_OIL_ALARM       GPIO_NUM_8      // 机油状态检测
#define GPIO_ENGINE_CHARGE_STATUS   GPIO_NUM_7      // 引擎发电机充电电压检测
#define GPIO_FUEL_SOLENOID          GPIO_NUM_20     // 电磁阀启动输出引脚
#define GPIO_ENGINE_START_STATUS    GPIO_NUM_6      // 引擎启动状态检测引脚
#define GPIO_STARTER_RELAY_DETECT   GPIO_NUM_4      // 启动继电器检测引脚

// 步进电机(风门控制) - ULN2003驱动，4相
#define GPIO_STEPPER_MOTOR_A        GPIO_NUM_18     // A相
#define GPIO_STEPPER_MOTOR_B        GPIO_NUM_17     // B相  
#define GPIO_STEPPER_MOTOR_C        GPIO_NUM_16     // C相
#define GPIO_STEPPER_MOTOR_D        GPIO_NUM_15     // D相

// 用户交互
#define GPIO_BUTTON_START           GPIO_NUM_1      // 发动机启动按钮
#define GPIO_LED_START_RED          GPIO_NUM_42     // 启动按键红灯
#define GPIO_LED_START_GREEN        GPIO_NUM_41     // 启动按键绿灯
#define GPIO_WS2812_DATA            GPIO_NUM_12     // WS2812 RGB LED控制

// 显示器(ST7789) - SPI接口
#define GPIO_TFT_CS                 GPIO_NUM_14     // SPI CS
#define GPIO_TFT_DC                 GPIO_NUM_48     // SPI DC
#define GPIO_TFT_RST                GPIO_NUM_45     // TFT RES
#define GPIO_TFT_BACKLIGHT          GPIO_NUM_13     // TFT 背光控制
#define GPIO_TFT_MOSI               GPIO_NUM_47     // SPI MOSI
#define GPIO_TFT_SCLK               GPIO_NUM_21     // SPI CLK

// RF遥控和其他
#define GPIO_RF_DATA_OUT            GPIO_NUM_35     // SYN590R射频芯片
#define GPIO_BUZZER                 GPIO_NUM_19     // 蜂鸣器输出引脚
#define GPIO_USB_DETECT             GPIO_NUM_46     // USB充电状态检测引脚
#define GPIO_POWER_SAVE_CTRL        GPIO_NUM_11     // 省电模式输出

// ADC输入引脚
#define GPIO_BATTERY_VOLTAGE_ADC    GPIO_NUM_5      // 电池电压检测ADC
#define GPIO_GENERATOR_FREQ_INPUT   GPIO_NUM_7      // 发电机频率输入（与充电状态复用）
#define GPIO_FUEL_LEVEL_ADC         GPIO_NUM_3      // 模拟燃油检测ADC
#define GPIO_BOARD_TEMP_ADC         GPIO_NUM_10     // 板载温度ADC

// I2C设备 - BQ40Z50电池管理芯片
#define I2C_SCL_GPIO                GPIO_NUM_39     // 电池管理芯片SCL
#define I2C_SDA_GPIO                GPIO_NUM_38     // 电池管理芯片SDA
#define I2C_BQ40Z50_ADDR            0x16            // BQ40Z50电池管理芯片地址

// UART接口 - 4G模块(选配)
#define GPIO_4G_UART_TX             GPIO_NUM_37     // 4G模块UART TX (选配)
#define GPIO_4G_UART_RX             GPIO_NUM_36     // 4G模块UART RX (选配)
#define UART_4G_PORT_NUM            UART_NUM_1      // 4G模块使用UART1

// ADC通道映射
#define ADC_CHANNEL_BATTERY_VOLTAGE ADC1_CHANNEL_4  // GPIO5 -> ADC1_CH4
#define ADC_CHANNEL_FUEL_LEVEL      ADC1_CHANNEL_2  // GPIO3 -> ADC1_CH2  
#define ADC_CHANNEL_BOARD_TEMP      ADC1_CHANNEL_9  // GPIO10 -> ADC1_CH9
#define ADC_CHANNEL_CHARGE_VOLTAGE  ADC1_CHANNEL_6  // GPIO7 -> ADC1_CH6

// ================================================================================
// 系统状态枚举
// ================================================================================
typedef enum {
    SYS_STATE_BOOT,             // 系统启动中
    SYS_STATE_INITIALIZING,     // 初始化
    SYS_STATE_WIFI_CONFIG,      // WiFi配网
    SYS_STATE_NORMAL,           // 正常运行
    SYS_STATE_OTA_UPDATE,       // OTA升级
    SYS_STATE_ERROR_RECOVERY,   // 异常恢复
    SYS_STATE_LOW_POWER         // 低功耗模式
} sys_state_t;

typedef enum {
    ENGINE_STATE_STOPPED,       // 停机状态
    ENGINE_STATE_STARTING,      // 启动中
    ENGINE_STATE_RUNNING,       // 运行中
    ENGINE_STATE_STOPPING,      // 停机中
    ENGINE_STATE_FAULT          // 故障状态
} engine_state_t;

typedef enum {
    START_SOURCE_LOCAL,         // 本地按键启动
    START_SOURCE_REMOTE_RF,     // RF遥控启动
    START_SOURCE_CLOUD,         // 云端远程启动
    START_SOURCE_MANUAL         // 手拉启动（检测）
} start_source_t;

typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_SUBSCRIBED
} mqtt_state_t;

typedef enum {
    UI_PAGE_MAIN,               // 主状态页面
    UI_PAGE_ALARMS,             // 告警信息页面
    UI_PAGE_SETTINGS,           // 设置页面
    UI_PAGE_WIFI_CONFIG,        // WiFi配网页面
    UI_PAGE_OTA_UPDATE          // OTA升级页面
} ui_page_t;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_UPDATING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED
} ota_state_t;

// ================================================================================
// 配置类型定义
// ================================================================================

/**
 * @brief 设备配置结构
 */
typedef struct {
    char product_id[MAX_PRODUCT_ID_LEN];        // 产品ID
    char device_name[MAX_DEVICE_NAME_LEN];      // 设备名称
    char device_secret[MAX_DEVICE_KEY_LEN];     // 设备密钥
    uint32_t config_version;                    // 配置版本
    bool first_boot;                            // 是否首次启动
} device_config_t;

/**
 * @brief WiFi配置结构
 */
typedef struct {
    char ssid[MAX_WIFI_SSID_LEN];              // WiFi SSID
    char password[MAX_WIFI_PASS_LEN];          // WiFi密码
    bool auto_connect;                          // 自动连接
    uint8_t max_retry;                         // 最大重试次数
    uint32_t connect_timeout_ms;               // 连接超时时间
} smartjet_wifi_config_t;

/**
 * @brief OTA配置结构
 */
typedef struct {
    bool auto_update_enabled;                   // 自动更新使能
    uint32_t check_interval_minutes;           // 检查间隔（分钟）
    int min_rssi_threshold;                    // 最小WiFi信号强度
    uint32_t download_timeout_ms;              // 下载超时时间
    uint8_t max_retry_count;                   // 最大重试次数
} ota_config_t;

/**
 * @brief OTA状态结构
 */
typedef struct {
    ota_state_t state;                         // 当前状态
    esp_err_t last_error;                      // 最后错误码
    uint64_t last_check_time;                  // 最后检查时间
    size_t bytes_downloaded;                   // 已下载字节数
    uint64_t download_start_time;              // 下载开始时间
    int32_t current_version;                   // 当前版本
} ota_status_t;

// ================================================================================
// 事件系统定义
// ================================================================================
typedef enum {
    EVENT_SYSTEM_BOOT,
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_ENGINE_START_REQUEST,
    EVENT_ENGINE_STOP_REQUEST,
    EVENT_ENGINE_STARTED,
    EVENT_ENGINE_STOPPED,
    EVENT_OIL_ALARM,
    EVENT_OVERSPEED_ALARM,
    EVENT_BATTERY_LOW,
    EVENT_BATTERY_HIGH,
    EVENT_OTA_START,
    EVENT_OTA_SUCCESS,
    EVENT_OTA_FAILED,
    EVENT_BUTTON_PRESSED,
    EVENT_RF_REMOTE_RECEIVED,
    EVENT_CLOUD_COMMAND_RECEIVED,
    EVENT_MEMORY_WARNING,
    EVENT_SYSTEM_ERROR
} system_event_t;

typedef struct {
    system_event_t type;
    void *data;
    size_t data_len;
    uint64_t timestamp;
} event_message_t;

// ================================================================================
// 核心数据结构
// ================================================================================

/**
 * @brief 系统状态结构
 */
typedef struct {
    sys_state_t current_state;
    sys_state_t previous_state;
    uint64_t state_enter_time;
    uint32_t uptime_seconds;
    esp_reset_reason_t last_reset_reason;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint16_t task_count;
} sys_status_t;

/**
 * @brief 发动机状态结构
 */
typedef struct {
    engine_state_t state;
    start_source_t last_start_source;
    uint32_t rpm;
    bool oil_pressure_ok;
    bool engine_running_detected;
    uint32_t total_runtime_minutes;
    uint32_t start_count;
    uint64_t last_start_time;
    uint8_t start_attempts;         // 当前启动尝试次数
    uint8_t max_start_attempts;     // 最大启动尝试次数
} engine_status_t;

/**
 * @brief 传感器数据结构
 */
typedef struct {
    // 发动机相关传感器
    uint32_t rpm;                   // 发动机转速
    bool oil_pressure_alarm;        // 机油压力报警
    float engine_temperature;       // 发动机温度
    uint32_t generator_frequency;   // 发电机频率
    
    // 系统传感器
    float board_temperature;        // 主板温度
    int16_t wifi_rssi;             // WiFi信号强度
    bool usb_power_connected;       // USB充电状态
    bool rf_remote_signal;          // RF遥控信号
    bool generator_charging;        // 发电机充电状态
    
    // 时间戳
    uint64_t timestamp_ms;
} sensor_data_t;

/**
 * @brief 电池状态结构
 */
typedef struct {
    float voltage;                  // 电池电压(V)
    float current;                  // 充放电流(A) 正值=放电，负值=充电
    uint8_t soc;                   // 剩余电量(%)
    float temperature;              // 电池温度(°C)
    uint16_t cycle_count;           // 充电循环次数
    bool charging;                  // 充电状态
    bool protection_active;         // 保护功能激活
    uint32_t last_update_time;     // 最后更新时间
    bool communication_ok;          // BQ40Z50通信状态
} battery_status_t;

/**
 * @brief MQTT配置结构
 */
typedef struct {
    char product_id[MAX_PRODUCT_ID_LEN];
    char device_name[MAX_DEVICE_NAME_LEN];
    char device_key[MAX_DEVICE_KEY_LEN];
    mqtt_state_t state;
    uint32_t last_publish_time;
    uint32_t publish_interval;
    uint16_t message_id;
    bool auto_report;
    int16_t rssi_threshold;         // 最小信号强度要求
} mqtt_config_t;

/**
 * @brief 显示状态结构
 */
typedef struct {
    ui_page_t current_page;
    bool backlight_on;
    uint32_t last_activity_time;
    bool auto_sleep_enabled;
    uint8_t brightness_level;
    bool display_initialized;
} display_status_t;

/**
 * @brief OTA状态结构
 */
// OTA状态结构已在前面定义，此处删除重复定义

/**
 * @brief 系统配置结构
 */
typedef struct {
    // 发动机控制参数
    uint32_t engine_start_pull_time;       // 启动拉低时间(ms)
    uint32_t engine_start_wait_time;       // 启动后等待时间(ms)
    uint32_t engine_stop_pull_time;        // 停机拉低时间(ms)
    uint32_t engine_stop_wait_time;        // 停机后等待时间(ms)
    uint32_t wake_pulse_ms;                // 唤醒脉冲时间(ms)
    uint32_t wake_to_long_ms;              // 唤醒到长脉冲间隔(ms)
    
    // 传感器参数
    uint32_t engine_on_rpm_threshold;      // 发动机启动RPM阈值
    uint32_t rpm_alarm_threshold;          // 转速报警阈值
    float rpm_calibration;                 // 转速校准系数
    
    // 电压保护参数
    float high_voltage_warning_threshold;  // 高电压警告阈值
    float high_voltage_protect_threshold;  // 高电压保护阈值
    float low_voltage_warning_threshold;   // 低电压警告阈值
    float low_voltage_protect_threshold;   // 低电压保护阈值
    
    // 维护参数
    uint32_t maintenance_interval_hours;   // 维护间隔小时数
    
    // 网络参数
    int16_t wifi_rssi_weak_threshold;      // WiFi弱信号阈值
    uint32_t mqtt_report_interval_active;  // MQTT上报间隔(活跃状态)
    uint32_t mqtt_report_interval_idle;    // MQTT上报间隔(空闲状态)
    
    // OTA参数
    bool auto_update_enabled;              // 自动更新使能
    uint32_t ota_check_interval_minutes;   // OTA检查间隔
    int16_t ota_rssi_min;                 // OTA最小信号要求
    
    // 系统参数
    uint32_t memory_warning_threshold;     // 内存警告阈值
    uint32_t watchdog_timeout_ms;          // 看门狗超时时间
    bool debug_mode_enabled;               // 调试模式使能
} sys_config_t;

/**
 * @brief 统计数据结构
 */
typedef struct {
    // 运行时统计
    uint32_t total_runtime_minutes;        // 总运行时间(分钟)
    uint32_t engine_start_count;           // 发动机启动次数
    uint32_t successful_starts;            // 成功启动次数
    uint32_t failed_starts;               // 失败启动次数
    
    // 故障统计
    uint16_t oil_alarm_count;              // 机油报警次数
    uint16_t overspeed_count;              // 超速报警次数
    uint16_t wifi_disconnect_count;        // WiFi断线次数
    uint16_t mqtt_failure_count;           // MQTT连接失败次数
    uint16_t memory_warning_count;         // 内存警告次数
    uint16_t system_restart_count;         // 系统重启次数
    
    // 维护信息
    uint32_t last_maintenance_hours;      // 上次维护时的运行小时数
    uint32_t next_maintenance_hours;      // 下次维护的运行小时数
    
    // 时间信息
    uint32_t last_save_time;              // 上次保存时间
    bool stats_dirty;                     // 统计数据是否需要保存
} system_stats_t;

/**
 * @brief 全局系统状态结构
 */
typedef struct {
    sys_status_t system;
    engine_status_t engine;
    sensor_data_t sensors;
    battery_status_t battery;
    display_status_t display;
    mqtt_config_t cloud;
    ota_status_t ota;
    sys_config_t config;
    system_stats_t stats;
    
    // 同步原语
    SemaphoreHandle_t system_mutex;       // 系统状态互斥锁
    QueueHandle_t event_queue;            // 事件队列
    EventGroupHandle_t system_events;     // 系统事件组
    
    // 系统标志
    bool initialized;                     // 系统初始化完成标志
    bool shutdown_requested;              // 系统关闭请求标志
} smartjet_global_t;

// ================================================================================
// 全局变量声明
// ================================================================================
extern smartjet_global_t g_smartjet;

// ================================================================================
// 公共宏定义
// ================================================================================
#define SMARTJET_LOG_TAG            "SMARTJET"

// 时间转换宏
#define MS_TO_TICKS(ms)             (pdMS_TO_TICKS(ms))
#define SECONDS_TO_TICKS(sec)       (pdMS_TO_TICKS((sec) * 1000))
#define MINUTES_TO_TICKS(min)       (pdMS_TO_TICKS((min) * 60 * 1000))

// 内存对齐宏
#define ALIGN_SIZE(size, align)     (((size) + (align) - 1) & ~((align) - 1))

// 安全操作宏
#define SAFE_FREE(ptr)              do { if (ptr) { free(ptr); (ptr) = NULL; } } while(0)
#define SAFE_DELETE_TASK(handle)    do { if (handle) { vTaskDelete(handle); (handle) = NULL; } } while(0)

// 错误检查宏
#define CHECK_ERROR_RETURN(x, tag, msg) do { \
    esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { \
        ESP_LOGE(tag, "%s: %s", msg, esp_err_to_name(err_rc_)); \
        return err_rc_; \
    } \
} while(0)

#define CHECK_ERROR_GOTO(x, tag, msg, label) do { \
    esp_err_t err_rc_ = (x); \
    if (err_rc_ != ESP_OK) { \
        ESP_LOGE(tag, "%s: %s", msg, esp_err_to_name(err_rc_)); \
        goto label; \
    } \
} while(0)

// ================================================================================
// 函数声明
// ================================================================================

/**
 * @brief 获取系统时间戳(毫秒)
 * @return 当前时间戳
 */
uint64_t smartjet_get_timestamp_ms(void);

/**
 * @brief 获取系统运行时间(秒)
 * @return 系统运行时间
 */
uint32_t smartjet_get_uptime_seconds(void);

/**
 * @brief 安全延时函数
 * @param ms 延时时间(毫秒)
 */
void smartjet_delay_ms(uint32_t ms);

/**
 * @brief 重启系统
 * @param reason 重启原因
 */
void smartjet_restart_system(const char* reason);

#ifdef __cplusplus
}
#endif

#endif // SMARTJET_COMMON_H 