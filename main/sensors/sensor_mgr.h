/**
 * @file sensor_mgr.h
 * @brief 传感器管理模块
 * @author SmartJet Team
 * @date 2024
 */

#ifndef SENSOR_MGR_H
#define SENSOR_MGR_H

#include "smartjet_common.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// 传感器采样参数
#define SENSOR_SAMPLE_RATE_MS           100     // 传感器采样间隔
#define SENSOR_FILTER_SAMPLES           5       // 滤波样本数
#define SENSOR_ADC_SAMPLES              32      // ADC多重采样数

// 温度传感器参数
#define TEMP_SENSOR_BETA               3950     // NTC温度系数
#define TEMP_SENSOR_R25                10000    // 25°C时的电阻值
#define TEMP_SENSOR_R_PULLUP           10000    // 上拉电阻值
#define TEMP_SENSOR_VCC                3300     // 供电电压(mV)

// RPM传感器参数
#define RPM_PULSE_TIMEOUT_MS           2000     // RPM脉冲超时时间
#define RPM_MIN_PULSE_INTERVAL_US      5000     // 最小脉冲间隔(防抖)
#define RPM_FILTER_ALPHA               0.3f     // 一阶低通滤波系数

// 频率传感器参数
#define FREQ_SAMPLE_WINDOW_MS          1000     // 频率采样窗口
#define FREQ_MIN_PULSE_COUNT           10       // 最小脉冲计数

// 传感器状态
typedef enum {
    SENSOR_STATUS_OK = 0,           // 正常
    SENSOR_STATUS_ERROR,            // 错误
    SENSOR_STATUS_TIMEOUT,          // 超时
    SENSOR_STATUS_OUT_OF_RANGE,     // 超出范围
    SENSOR_STATUS_NOT_CONNECTED     // 未连接
} sensor_status_t;

// 传感器数据结构
typedef struct {
    float value;                    // 传感器值
    sensor_status_t status;         // 状态
    uint64_t last_update_time;      // 最后更新时间
    bool data_valid;               // 数据有效标志
} sensor_data_item_t;

// 滤波器结构
typedef struct {
    float samples[SENSOR_FILTER_SAMPLES];   // 样本缓冲区
    uint8_t index;                          // 当前索引
    uint8_t count;                          // 有效样本数
    float filtered_value;                   // 滤波后的值
} sensor_filter_t;

// RPM传感器结构
typedef struct {
    volatile uint32_t pulse_count;          // 脉冲计数
    volatile uint64_t last_pulse_time;      // 上次脉冲时间
    volatile uint64_t pulse_period_sum;     // 脉冲周期累计
    volatile uint8_t valid_pulse_count;     // 有效脉冲计数
    sensor_filter_t filter;                 // 滤波器
    uint16_t rpm_value;                     // 计算的RPM值
    bool engine_running;                    // 发动机运行标志
} rpm_sensor_t;

// 频率传感器结构  
typedef struct {
    volatile uint32_t pulse_count;          // 脉冲计数
    volatile uint64_t last_count_time;      // 上次计数时间
    sensor_filter_t filter;                 // 滤波器
    float frequency_value;                  // 计算的频率值
} frequency_sensor_t;

// 传感器管理器结构
typedef struct {
    // 传感器数据
    sensor_data_item_t oil_pressure;       // 机油压力
    sensor_data_item_t board_temperature;  // 主板温度
    sensor_data_item_t usb_power;          // USB电源状态
    sensor_data_item_t charge_status;      // 充电状态
    sensor_data_item_t starter_relay;      // 启动继电器状态
    
    // 复杂传感器
    rpm_sensor_t rpm_sensor;               // RPM传感器
    frequency_sensor_t freq_sensor;        // 频率传感器
    
    // 采样定时器
    esp_timer_handle_t sample_timer;       // 采样定时器
    
    // 同步原语
    SemaphoreHandle_t sensor_mutex;        // 传感器数据锁
    
    // 状态标志
    bool initialized;                      // 初始化标志
    bool sampling_enabled;                 // 采样使能标志
} sensor_manager_t;

// 传感器事件回调类型
typedef void (*sensor_event_callback_t)(int type, float value, sensor_status_t status);

// 函数声明
esp_err_t sensor_manager_init(void);
void sensor_manager_deinit(void);

// 采样控制
esp_err_t sensor_manager_start_sampling(void);
esp_err_t sensor_manager_stop_sampling(void);
esp_err_t sensor_manager_set_sample_rate(uint32_t rate_ms);

// 数据获取接口
sensor_data_item_t sensor_get_oil_pressure(void);
sensor_data_item_t sensor_get_board_temperature(void);
sensor_data_item_t sensor_get_usb_power_status(void);
sensor_data_item_t sensor_get_charge_status(void);
sensor_data_item_t sensor_get_starter_relay_status(void);
uint16_t sensor_get_engine_rpm(void);
float sensor_get_generator_frequency(void);

// 状态查询
bool sensor_is_oil_alarm_active(void);
bool sensor_is_engine_running(void);
bool sensor_is_usb_powered(void);
bool sensor_is_charging(void);
sensor_status_t sensor_get_overall_status(void);

// 校准和配置
esp_err_t sensor_calibrate_temperature_sensor(float reference_temp);
esp_err_t sensor_set_rpm_threshold(uint16_t threshold);
esp_err_t sensor_set_oil_alarm_threshold(bool active_low);

// 事件回调
esp_err_t sensor_register_event_callback(sensor_event_callback_t callback);
esp_err_t sensor_unregister_event_callback(void);

// 诊断接口
void sensor_dump_all_values(void);
esp_err_t sensor_self_test(void);

// 内部函数声明 - 预留用于后续传感器功能扩展
/*
static void sensor_sample_timer_callback(void* arg);
static void rpm_gpio_isr_handler(void* arg);
static void freq_gpio_isr_handler(void* arg);
static esp_err_t sensor_sample_analog_sensors(void);
static esp_err_t sensor_sample_digital_sensors(void);
static esp_err_t sensor_update_rpm_calculation(void);
static esp_err_t sensor_update_frequency_calculation(void);
static float sensor_filter_update(sensor_filter_t* filter, float new_value);
static float sensor_ntc_resistance_to_temperature(uint32_t adc_value);
*/

#endif // SENSOR_MGR_H 