/**
 * @file sensor_mgr.c
 * @brief 传感器管理模块实现
 * @author SmartJet Team
 * @date 2024
 */

#include "sensor_mgr.h"
#include "engine_ctrl.h"
#include "sys_manager.h"
#include "event_system.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>

static const char* TAG = "SENSOR_MGR";

// 前向声明
static void sensor_sample_timer_callback(void* arg);
static void IRAM_ATTR rpm_gpio_isr_handler(void* arg);
static void IRAM_ATTR freq_gpio_isr_handler(void* arg);
static esp_err_t sensor_sample_analog_sensors(void);
static esp_err_t sensor_sample_digital_sensors(void);
static esp_err_t sensor_update_rpm_calculation(void);
static esp_err_t sensor_update_frequency_calculation(void);
static float sensor_filter_update(sensor_filter_t* filter, float new_value);
static float sensor_ntc_resistance_to_temperature(uint32_t voltage_mv);

// 全局传感器管理器实例
static sensor_manager_t g_sensor_mgr = {0};
static bool g_sensor_initialized = false;
// 事件回调功能预留，暂未使用
// static sensor_event_callback_t g_sensor_event_callback = NULL;

// ADC校准句柄
static esp_adc_cal_characteristics_t adc1_chars;

/**
 * @brief 传感器管理器初始化
 */
esp_err_t sensor_manager_init(void)
{
    if (g_sensor_initialized) {
        ESP_LOGW(TAG, "传感器管理器已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化传感器管理器...");
    
    // 创建互斥锁
    g_sensor_mgr.sensor_mutex = xSemaphoreCreateMutex();
    if (!g_sensor_mgr.sensor_mutex) {
        ESP_LOGE(TAG, "创建传感器互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 配置ADC
    esp_err_t ret = adc1_config_width(ADC_WIDTH_BIT_12);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置ADC位宽失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 配置所有ADC通道
    ret = adc1_config_channel_atten(ADC_CHANNEL_BOARD_TEMP, ADC_ATTEN_DB_12); // 板载温度传感器
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置温度ADC通道失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc1_config_channel_atten(ADC_CHANNEL_BATTERY_VOLTAGE, ADC_ATTEN_DB_12); // 电池电压检测
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置电池电压ADC通道失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc1_config_channel_atten(ADC_CHANNEL_FUEL_LEVEL, ADC_ATTEN_DB_12); // 燃油液位检测
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置燃油液位ADC通道失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = adc1_config_channel_atten(ADC_CHANNEL_CHARGE_VOLTAGE, ADC_ATTEN_DB_12); // 充电电压检测
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置充电电压ADC通道失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // ADC校准
    esp_adc_cal_value_t cal_type = esp_adc_cal_characterize(
        ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &adc1_chars);
    
    if (cal_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC校准基于eFuse中的Vref");
    } else if (cal_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC校准基于eFuse中的双点校准");
    } else {
        ESP_LOGI(TAG, "ADC校准基于默认Vref");
    }
    
    // 配置数字输入GPIO
    gpio_config_t io_conf = {0};
    
    // 机油压力、USB电源、充电状态、启动继电器状态输入
    io_conf.pin_bit_mask = (1ULL << GPIO_ENGINE_OIL_ALARM) |
                          (1ULL << GPIO_USB_DETECT) |
                          (1ULL << GPIO_ENGINE_CHARGE_STATUS) |
                          (1ULL << GPIO_STARTER_RELAY_DETECT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置数字输入GPIO失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 配置RPM输入中断
    io_conf.pin_bit_mask = (1ULL << GPIO_ENGINE_RPM_INPUT);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置RPM输入GPIO失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 配置频率输入中断
    io_conf.pin_bit_mask = (1ULL << GPIO_GENERATOR_FREQ_INPUT);
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置频率输入GPIO失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 安装GPIO中断服务
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "安装GPIO中断服务失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 添加RPM中断处理程序
    ret = gpio_isr_handler_add(GPIO_ENGINE_RPM_INPUT, rpm_gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加RPM中断处理程序失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 添加频率中断处理程序
    ret = gpio_isr_handler_add(GPIO_GENERATOR_FREQ_INPUT, freq_gpio_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加频率中断处理程序失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 创建采样定时器
    esp_timer_create_args_t timer_args = {
        .callback = sensor_sample_timer_callback,
        .arg = NULL,
        .name = "sensor_sample_timer"
    };
    ret = esp_timer_create(&timer_args, &g_sensor_mgr.sample_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建采样定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化传感器数据
    memset(&g_sensor_mgr.oil_pressure, 0, sizeof(sensor_data_item_t));
    memset(&g_sensor_mgr.board_temperature, 0, sizeof(sensor_data_item_t));
    memset(&g_sensor_mgr.usb_power, 0, sizeof(sensor_data_item_t));
    memset(&g_sensor_mgr.charge_status, 0, sizeof(sensor_data_item_t));
    memset(&g_sensor_mgr.starter_relay, 0, sizeof(sensor_data_item_t));
    
    // 初始化RPM和频率传感器
    memset(&g_sensor_mgr.rpm_sensor, 0, sizeof(rpm_sensor_t));
    memset(&g_sensor_mgr.freq_sensor, 0, sizeof(frequency_sensor_t));
    
    g_sensor_mgr.initialized = true;
    g_sensor_mgr.sampling_enabled = false;
    
    g_sensor_initialized = true;
    ESP_LOGI(TAG, "传感器管理器初始化完成");
    
    return ESP_OK;
    
cleanup:
    sensor_manager_deinit();
    return ret;
}

/**
 * @brief 传感器管理器反初始化
 */
void sensor_manager_deinit(void)
{
    if (!g_sensor_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化传感器管理器...");
    
    // 停止采样
    sensor_manager_stop_sampling();
    
    // 移除中断处理程序
    gpio_isr_handler_remove(GPIO_ENGINE_RPM_INPUT);
    gpio_isr_handler_remove(GPIO_GENERATOR_FREQ_INPUT);
    
    // 删除定时器
    if (g_sensor_mgr.sample_timer) {
        esp_timer_stop(g_sensor_mgr.sample_timer);
        esp_timer_delete(g_sensor_mgr.sample_timer);
        g_sensor_mgr.sample_timer = NULL;
    }
    
    // 删除互斥锁
    if (g_sensor_mgr.sensor_mutex) {
        vSemaphoreDelete(g_sensor_mgr.sensor_mutex);
        g_sensor_mgr.sensor_mutex = NULL;
    }
    
    g_sensor_initialized = false;
    ESP_LOGI(TAG, "传感器管理器反初始化完成");
}

/**
 * @brief 开始传感器采样
 */
esp_err_t sensor_manager_start_sampling(void)
{
    if (!g_sensor_initialized) {
        ESP_LOGE(TAG, "传感器管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_sensor_mgr.sampling_enabled) {
        ESP_LOGW(TAG, "传感器采样已启动");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "启动传感器采样");
    
    // 启动定时器
    esp_err_t ret = esp_timer_start_periodic(g_sensor_mgr.sample_timer, 
                                           SENSOR_SAMPLE_RATE_MS * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动采样定时器失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_sensor_mgr.sampling_enabled = true;
    return ESP_OK;
}

/**
 * @brief 停止传感器采样
 */
esp_err_t sensor_manager_stop_sampling(void)
{
    if (!g_sensor_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!g_sensor_mgr.sampling_enabled) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止传感器采样");
    
    esp_timer_stop(g_sensor_mgr.sample_timer);
    g_sensor_mgr.sampling_enabled = false;
    
    return ESP_OK;
}

/**
 * @brief 采样定时器回调函数
 */
static void sensor_sample_timer_callback(void* arg)
{
    if (!g_sensor_initialized || !g_sensor_mgr.sampling_enabled) {
        return;
    }
    
    sys_manager_feed_watchdog("sensor_sample");
    
    // 采样模拟传感器
    sensor_sample_analog_sensors();
    
    // 采样数字传感器
    sensor_sample_digital_sensors();
    
    // 更新RPM计算
    sensor_update_rpm_calculation();
    
    // 更新频率计算
    sensor_update_frequency_calculation();
    
    // 更新全局传感器数据
    if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        g_smartjet.sensors.oil_pressure_alarm = sensor_is_oil_alarm_active();
        g_smartjet.sensors.rpm = g_sensor_mgr.rpm_sensor.rpm_value;
        g_smartjet.sensors.generator_frequency = g_sensor_mgr.freq_sensor.frequency_value;
        g_smartjet.sensors.board_temperature = g_sensor_mgr.board_temperature.value;
        g_smartjet.sensors.usb_power_connected = sensor_is_usb_powered();
        g_smartjet.sensors.generator_charging = sensor_is_charging();
        g_smartjet.sensors.timestamp_ms = esp_timer_get_time() / 1000;
        
        xSemaphoreGive(g_smartjet.system_mutex);
    }
}

/**
 * @brief 采样模拟传感器
 */
static esp_err_t sensor_sample_analog_sensors(void)
{
    uint32_t adc_reading = 0;
    uint32_t voltage = 0;
    
    if (xSemaphoreTake(g_sensor_mgr.sensor_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 多重采样以减少噪声
    for (int i = 0; i < SENSOR_ADC_SAMPLES; i++) {
        adc_reading += adc1_get_raw(ADC_CHANNEL_BOARD_TEMP);
    }
    adc_reading /= SENSOR_ADC_SAMPLES;
    
    // 转换为电压值
    voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc1_chars);
    
    // 计算温度值
    float temperature = sensor_ntc_resistance_to_temperature(voltage);
    float filtered_temp = temperature; // 简化：暂时不使用滤波
    
    // 更新板载温度传感器数据
    g_sensor_mgr.board_temperature.value = filtered_temp;
    g_sensor_mgr.board_temperature.status = SENSOR_STATUS_OK;
    g_sensor_mgr.board_temperature.last_update_time = esp_timer_get_time();
    g_sensor_mgr.board_temperature.data_valid = true;
    
    // 检查温度异常
    if (temperature < -20.0f || temperature > 70.0f) {
        g_sensor_mgr.board_temperature.status = SENSOR_STATUS_OUT_OF_RANGE;
        ESP_LOGW(TAG, "主板温度异常: %.1f°C", temperature);
    }
    
    xSemaphoreGive(g_sensor_mgr.sensor_mutex);
    return ESP_OK;
}

/**
 * @brief 采样数字传感器
 */
static esp_err_t sensor_sample_digital_sensors(void)
{
    if (xSemaphoreTake(g_sensor_mgr.sensor_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint64_t current_time = esp_timer_get_time();
    
    // 采样机油压力报警
    bool oil_alarm = !gpio_get_level(GPIO_ENGINE_OIL_ALARM); // 低电平有效
    g_sensor_mgr.oil_pressure.value = oil_alarm ? 1.0f : 0.0f;
    g_sensor_mgr.oil_pressure.status = SENSOR_STATUS_OK;
    g_sensor_mgr.oil_pressure.last_update_time = current_time;
    g_sensor_mgr.oil_pressure.data_valid = true;
    
    // 采样USB电源状态
    bool usb_power = gpio_get_level(GPIO_USB_DETECT);
    g_sensor_mgr.usb_power.value = usb_power ? 1.0f : 0.0f;
    g_sensor_mgr.usb_power.status = SENSOR_STATUS_OK;
    g_sensor_mgr.usb_power.last_update_time = current_time;
    g_sensor_mgr.usb_power.data_valid = true;
    
    // 采样充电状态
    bool charge_status = gpio_get_level(GPIO_ENGINE_CHARGE_STATUS);
    g_sensor_mgr.charge_status.value = charge_status ? 1.0f : 0.0f;
    g_sensor_mgr.charge_status.status = SENSOR_STATUS_OK;
    g_sensor_mgr.charge_status.last_update_time = current_time;
    g_sensor_mgr.charge_status.data_valid = true;
    
    // 采样启动继电器状态
    bool starter_relay = !gpio_get_level(GPIO_STARTER_RELAY_DETECT); // 低电平表示连接
    g_sensor_mgr.starter_relay.value = starter_relay ? 1.0f : 0.0f;
    g_sensor_mgr.starter_relay.status = SENSOR_STATUS_OK;
    g_sensor_mgr.starter_relay.last_update_time = current_time;
    g_sensor_mgr.starter_relay.data_valid = true;
    
    xSemaphoreGive(g_sensor_mgr.sensor_mutex);
    return ESP_OK;
}

/**
 * @brief RPM GPIO中断处理程序
 */
static void IRAM_ATTR rpm_gpio_isr_handler(void* arg)
{
    uint64_t current_time = esp_timer_get_time();
    
    // 防抖：检查与上次脉冲的时间间隔
    if (g_sensor_mgr.rpm_sensor.last_pulse_time > 0) {
        uint64_t interval = current_time - g_sensor_mgr.rpm_sensor.last_pulse_time;
        if (interval < RPM_MIN_PULSE_INTERVAL_US) {
            return; // 忽略抖动脉冲
        }
        
        // 累积脉冲周期
        g_sensor_mgr.rpm_sensor.pulse_period_sum += interval;
        g_sensor_mgr.rpm_sensor.valid_pulse_count++;
    }
    
    g_sensor_mgr.rpm_sensor.pulse_count++;
    g_sensor_mgr.rpm_sensor.last_pulse_time = current_time;
}

/**
 * @brief 频率GPIO中断处理程序
 */
static void IRAM_ATTR freq_gpio_isr_handler(void* arg)
{
    g_sensor_mgr.freq_sensor.pulse_count++;
}

/**
 * @brief 更新RPM计算
 */
static esp_err_t sensor_update_rpm_calculation(void)
{
    uint64_t current_time = esp_timer_get_time();
    static uint64_t last_calculation_time = 0;
    
    if (last_calculation_time == 0) {
        last_calculation_time = current_time;
        return ESP_OK;
    }
    
    uint64_t time_elapsed = current_time - last_calculation_time;
    if (time_elapsed < 500000) { // 每500ms计算一次
        return ESP_OK;
    }
    
    if (xSemaphoreTake(g_sensor_mgr.sensor_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint16_t rpm = 0;
    bool engine_running = false;
    
    // 检查是否超时
    if (g_sensor_mgr.rpm_sensor.last_pulse_time > 0) {
        uint64_t time_since_last_pulse = current_time - g_sensor_mgr.rpm_sensor.last_pulse_time;
        
        if (time_since_last_pulse < (RPM_PULSE_TIMEOUT_MS * 1000)) {
            // 计算RPM
            if (g_sensor_mgr.rpm_sensor.valid_pulse_count > 0) {
                uint64_t avg_period = g_sensor_mgr.rpm_sensor.pulse_period_sum / 
                                     g_sensor_mgr.rpm_sensor.valid_pulse_count;
                
                // RPM = 60,000,000 / (平均周期微秒)
                if (avg_period > 0) {
                    rpm = (uint16_t)(60000000ULL / avg_period);
                    engine_running = (rpm >= g_smartjet.config.engine_on_rpm_threshold);
                }
            }
        } else {
            // 超时，发动机停止
            rpm = 0;
            engine_running = false;
        }
    }
    
    // 应用低通滤波
    float filtered_rpm = sensor_filter_update(&g_sensor_mgr.rpm_sensor.filter, (float)rpm);
    g_sensor_mgr.rpm_sensor.rpm_value = (uint16_t)filtered_rpm;
    g_sensor_mgr.rpm_sensor.engine_running = engine_running;
    
    // 重置计数器
    g_sensor_mgr.rpm_sensor.pulse_period_sum = 0;
    g_sensor_mgr.rpm_sensor.valid_pulse_count = 0;
    last_calculation_time = current_time;
    
    // 通知发动机控制模块RPM变化
    if (rpm != (uint16_t)filtered_rpm) {
        engine_handle_rpm_change(g_sensor_mgr.rpm_sensor.rpm_value);
    }
    
    xSemaphoreGive(g_sensor_mgr.sensor_mutex);
    return ESP_OK;
}

/**
 * @brief 更新频率计算
 */
static esp_err_t sensor_update_frequency_calculation(void)
{
    static uint64_t last_calculation_time = 0;
    static uint32_t last_pulse_count = 0;
    
    uint64_t current_time = esp_timer_get_time();
    
    if (last_calculation_time == 0) {
        last_calculation_time = current_time;
        last_pulse_count = g_sensor_mgr.freq_sensor.pulse_count;
        return ESP_OK;
    }
    
    uint64_t time_elapsed = current_time - last_calculation_time;
    if (time_elapsed < (FREQ_SAMPLE_WINDOW_MS * 1000)) {
        return ESP_OK;
    }
    
    if (xSemaphoreTake(g_sensor_mgr.sensor_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint32_t pulse_count_diff = g_sensor_mgr.freq_sensor.pulse_count - last_pulse_count;
    
    float frequency = 0.0f;
    if (pulse_count_diff >= FREQ_MIN_PULSE_COUNT) {
        // 频率 = 脉冲数 / 时间(秒)
        frequency = (float)pulse_count_diff / (time_elapsed / 1000000.0f);
    }
    
    // 应用滤波
    float filtered_freq = sensor_filter_update(&g_sensor_mgr.freq_sensor.filter, frequency);
    g_sensor_mgr.freq_sensor.frequency_value = filtered_freq;
    
    last_calculation_time = current_time;
    last_pulse_count = g_sensor_mgr.freq_sensor.pulse_count;
    
    xSemaphoreGive(g_sensor_mgr.sensor_mutex);
    return ESP_OK;
}

/**
 * @brief 滤波器更新
 */
static float sensor_filter_update(sensor_filter_t* filter, float new_value)
{
    if (!filter) {
        return new_value;
    }
    
    // 添加新样本
    filter->samples[filter->index] = new_value;
    filter->index = (filter->index + 1) % SENSOR_FILTER_SAMPLES;
    
    if (filter->count < SENSOR_FILTER_SAMPLES) {
        filter->count++;
    }
    
    // 计算平均值
    float sum = 0.0f;
    for (int i = 0; i < filter->count; i++) {
        sum += filter->samples[i];
    }
    
    filter->filtered_value = sum / filter->count;
    return filter->filtered_value;
}

/**
 * @brief NTC电阻转换为温度
 */
static float sensor_ntc_resistance_to_temperature(uint32_t voltage_mv)
{
    // 计算NTC电阻值
    float vcc = TEMP_SENSOR_VCC;
    float r_pullup = TEMP_SENSOR_R_PULLUP;
    
    if (voltage_mv >= vcc) {
        return -273.15f; // 无效温度
    }
    
    float r_ntc = r_pullup * voltage_mv / (vcc - voltage_mv);
    
    // 使用Steinhart-Hart方程计算温度
    float temp_k = 1.0f / (1.0f / 298.15f + (1.0f / TEMP_SENSOR_BETA) * 
                          logf(r_ntc / TEMP_SENSOR_R25));
    
    return temp_k - 273.15f; // 转换为摄氏度
}

// 数据获取接口实现
sensor_data_item_t sensor_get_oil_pressure(void) { return g_sensor_mgr.oil_pressure; }
sensor_data_item_t sensor_get_board_temperature(void) { return g_sensor_mgr.board_temperature; }
sensor_data_item_t sensor_get_usb_power_status(void) { return g_sensor_mgr.usb_power; }
sensor_data_item_t sensor_get_charge_status(void) { return g_sensor_mgr.charge_status; }
sensor_data_item_t sensor_get_starter_relay_status(void) { return g_sensor_mgr.starter_relay; }
uint16_t sensor_get_engine_rpm(void) { return g_sensor_mgr.rpm_sensor.rpm_value; }
float sensor_get_generator_frequency(void) { return g_sensor_mgr.freq_sensor.frequency_value; }

// 状态查询接口实现
bool sensor_is_oil_alarm_active(void) { return g_sensor_mgr.oil_pressure.value > 0.5f; }
bool sensor_is_engine_running(void) { return g_sensor_mgr.rpm_sensor.engine_running; }
bool sensor_is_usb_powered(void) { return g_sensor_mgr.usb_power.value > 0.5f; }
bool sensor_is_charging(void) { return g_sensor_mgr.charge_status.value > 0.5f; }

/**
 * @brief 获取传感器整体状态
 */
sensor_status_t sensor_get_overall_status(void)
{
    if (!g_sensor_initialized) {
        return SENSOR_STATUS_ERROR;
    }
    
    // 检查各传感器状态
    if (g_sensor_mgr.oil_pressure.status != SENSOR_STATUS_OK ||
        g_sensor_mgr.board_temperature.status != SENSOR_STATUS_OK) {
        return SENSOR_STATUS_ERROR;
    }
    
    return SENSOR_STATUS_OK;
}

// 删除重复的函数定义 