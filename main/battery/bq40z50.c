/**
 * @file bq40z50.c
 * @brief BQ40Z50-R2 电池管理芯片驱动实现
 * @author SmartJet Team
 * @date 2024
 */

#include "bq40z50.h"
#include "sys_manager.h"
#include "event_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "BQ40Z50";

// 前向声明
static esp_err_t bq40z50_i2c_init(void);
static esp_err_t bq40z50_i2c_deinit(void);
static esp_err_t bq40z50_parse_battery_status(uint16_t status);
static esp_err_t bq40z50_parse_battery_mode(uint16_t mode);
static float bq40z50_temperature_k_to_c(uint16_t temp_k);

// 全局BQ40Z50控制器实例
static bq40z50_controller_t g_bq40z50_ctrl = {0};
static bool g_bq40z50_initialized = false;

/**
 * @brief BQ40Z50初始化
 */
esp_err_t bq40z50_init(void)
{
    if (g_bq40z50_initialized) {
        ESP_LOGW(TAG, "BQ40Z50已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化BQ40Z50电池管理芯片...");
    
    // 初始化I2C
    esp_err_t ret = bq40z50_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建数据互斥锁
    g_bq40z50_ctrl.data_mutex = xSemaphoreCreateMutex();
    if (!g_bq40z50_ctrl.data_mutex) {
        ESP_LOGE(TAG, "创建数据互斥锁失败");
        bq40z50_i2c_deinit();
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化控制器参数
    g_bq40z50_ctrl.initialized = true;
    g_bq40z50_ctrl.i2c_port = BQ40Z50_I2C_MASTER_NUM;
    g_bq40z50_ctrl.device_address = BQ40Z50_I2C_ADDR;
    g_bq40z50_ctrl.update_interval_ms = 5000;  // 默认5秒更新一次
    g_bq40z50_ctrl.low_voltage_threshold = 12000;  // 12V
    g_bq40z50_ctrl.high_voltage_threshold = 16800; // 16.8V
    g_bq40z50_ctrl.low_soc_threshold = 20;     // 20%
    g_bq40z50_ctrl.critical_soc_threshold = 5; // 5%
    
    // 初始化电池数据
    memset(&g_bq40z50_ctrl.battery_data, 0, sizeof(bq40z50_data_t));
    
    // 检查设备是否存在
    uint16_t voltage;
    ret = bq40z50_read_word(BQ40Z50_CMD_VOLTAGE, &voltage);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BQ40Z50设备通信失败，检查硬件连接");
        bq40z50_deinit();
        return ret;
    }
    
    // 读取基本设备信息
    uint16_t design_voltage, serial_number;
    bq40z50_read_word(BQ40Z50_CMD_DESIGN_VOLTAGE, &design_voltage);
    bq40z50_read_word(BQ40Z50_CMD_SERIAL_NUMBER, &serial_number);
    
    ESP_LOGI(TAG, "BQ40Z50设备检测成功");
    ESP_LOGI(TAG, "  电压: %d mV", voltage);
    ESP_LOGI(TAG, "  设计电压: %d mV", design_voltage);
    ESP_LOGI(TAG, "  序列号: 0x%04X", serial_number);
    
    // 更新初始数据
    bq40z50_update_all_data();
    
    g_bq40z50_initialized = true;
    ESP_LOGI(TAG, "BQ40Z50初始化完成");
    
    return ESP_OK;
}

/**
 * @brief BQ40Z50反初始化
 */
void bq40z50_deinit(void)
{
    if (!g_bq40z50_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化BQ40Z50...");
    
    // 删除互斥锁
    if (g_bq40z50_ctrl.data_mutex) {
        vSemaphoreDelete(g_bq40z50_ctrl.data_mutex);
        g_bq40z50_ctrl.data_mutex = NULL;
    }
    
    // 反初始化I2C
    bq40z50_i2c_deinit();
    
    g_bq40z50_ctrl.initialized = false;
    g_bq40z50_initialized = false;
    
    ESP_LOGI(TAG, "BQ40Z50反初始化完成");
}

/**
 * @brief 读取16位寄存器
 */
esp_err_t bq40z50_read_word(uint8_t command, uint16_t* data)
{
    if (!g_bq40z50_initialized || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t read_data[2];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // 写命令
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ40Z50_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, command, true);
    
    // 重新开始读取数据
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ40Z50_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &read_data[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &read_data[1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(BQ40Z50_I2C_MASTER_NUM, cmd, 
                                        pdMS_TO_TICKS(BQ40Z50_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        // SMBus是小端序
        *data = (uint16_t)read_data[0] | ((uint16_t)read_data[1] << 8);
    }
    
    return ret;
}

/**
 * @brief 写入16位寄存器
 */
esp_err_t bq40z50_write_word(uint8_t command, uint16_t data)
{
    if (!g_bq40z50_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ40Z50_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, command, true);
    i2c_master_write_byte(cmd, data & 0xFF, true);        // 低字节
    i2c_master_write_byte(cmd, (data >> 8) & 0xFF, true); // 高字节
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(BQ40Z50_I2C_MASTER_NUM, cmd, 
                                        pdMS_TO_TICKS(BQ40Z50_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

/**
 * @brief 读取块数据
 */
esp_err_t bq40z50_read_block(uint8_t command, uint8_t* data, size_t* length)
{
    if (!g_bq40z50_initialized || !data || !length) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t block_length;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // 写命令
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ40Z50_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, command, true);
    
    // 重新开始读取长度
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BQ40Z50_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &block_length, I2C_MASTER_ACK);
    
    // 读取数据
    for (int i = 0; i < block_length && i < *length; i++) {
        bool ack = (i < block_length - 1) ? I2C_MASTER_ACK : I2C_MASTER_NACK;
        i2c_master_read_byte(cmd, &data[i], ack);
    }
    
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(BQ40Z50_I2C_MASTER_NUM, cmd, 
                                        pdMS_TO_TICKS(BQ40Z50_I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        *length = block_length;
    }
    
    return ret;
}

/**
 * @brief 更新所有电池数据
 */
esp_err_t bq40z50_update_all_data(void)
{
    if (!g_bq40z50_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_bq40z50_ctrl.data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取数据锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    
    do {
        // 读取基本电池信息
        if (bq40z50_read_word(BQ40Z50_CMD_VOLTAGE, &data->voltage_mv) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        
        if (bq40z50_read_word(BQ40Z50_CMD_CURRENT, (uint16_t*)&data->current_ma) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        
        if (bq40z50_read_word(BQ40Z50_CMD_AVERAGE_CURRENT, (uint16_t*)&data->average_current_ma) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        
        if (bq40z50_read_word(BQ40Z50_CMD_TEMPERATURE, &data->temperature_k) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        
        // 转换温度
        data->temperature_c = bq40z50_temperature_k_to_c(data->temperature_k);
        
        // 读取电量信息
        uint16_t temp_soc;
        if (bq40z50_read_word(BQ40Z50_CMD_RELATIVE_SOC, &temp_soc) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        data->relative_soc = (uint8_t)temp_soc;
        
        if (bq40z50_read_word(BQ40Z50_CMD_ABSOLUTE_SOC, &temp_soc) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        data->absolute_soc = (uint8_t)temp_soc;
        
        bq40z50_read_word(BQ40Z50_CMD_REMAINING_CAPACITY, &data->remaining_capacity_mah);
        bq40z50_read_word(BQ40Z50_CMD_FULL_CHARGE_CAPACITY, &data->full_charge_capacity_mah);
        bq40z50_read_word(BQ40Z50_CMD_DESIGN_CAPACITY, &data->design_capacity_mah);
        
        // 读取时间信息
        bq40z50_read_word(BQ40Z50_CMD_AVERAGE_TIME_TO_EMPTY, &data->time_to_empty_min);
        bq40z50_read_word(BQ40Z50_CMD_AVERAGE_TIME_TO_FULL, &data->time_to_full_min);
        
        // 读取状态信息
        if (bq40z50_read_word(BQ40Z50_CMD_BATTERY_STATUS, &data->battery_status) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
        
        bq40z50_read_word(BQ40Z50_CMD_BATTERY_MODE, &data->battery_mode);
        bq40z50_read_word(BQ40Z50_CMD_CYCLE_COUNT, &data->cycle_count);
        
        // 读取充电信息
        bq40z50_read_word(BQ40Z50_CMD_CHARGING_CURRENT, &data->charging_current_ma);
        bq40z50_read_word(BQ40Z50_CMD_CHARGING_VOLTAGE, &data->charging_voltage_mv);
        
        // 读取设计规格
        bq40z50_read_word(BQ40Z50_CMD_DESIGN_VOLTAGE, &data->design_voltage_mv);
        bq40z50_read_word(BQ40Z50_CMD_SERIAL_NUMBER, &data->serial_number);
        bq40z50_read_word(BQ40Z50_CMD_MFG_DATE, &data->manufacture_date);
        
        // 解析状态标志
        bq40z50_parse_battery_status(data->battery_status);
        bq40z50_parse_battery_mode(data->battery_mode);
        
        // 验证数据有效性
        data->data_valid = bq40z50_validate_data();
        data->last_update_time = esp_timer_get_time();
        
    } while (0);
    
    xSemaphoreGive(g_bq40z50_ctrl.data_mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "电池数据更新成功: 电压=%dmV, 电流=%dmA, SOC=%d%%", 
                data->voltage_mv, data->current_ma, data->relative_soc);
    } else {
        ESP_LOGE(TAG, "电池数据更新失败");
        data->data_valid = false;
    }
    
    return ret;
}

/**
 * @brief 更新基本电池数据（快速更新）
 */
esp_err_t bq40z50_update_basic_data(void)
{
    if (!g_bq40z50_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_bq40z50_ctrl.data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_OK;
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    
    // 只读取关键数据
    if (bq40z50_read_word(BQ40Z50_CMD_VOLTAGE, &data->voltage_mv) != ESP_OK ||
        bq40z50_read_word(BQ40Z50_CMD_CURRENT, (uint16_t*)&data->current_ma) != ESP_OK ||
        bq40z50_read_word(BQ40Z50_CMD_RELATIVE_SOC, (uint16_t*)&data->relative_soc) != ESP_OK) {
        ret = ESP_FAIL;
        data->data_valid = false;
    } else {
        data->data_valid = true;
        data->last_update_time = esp_timer_get_time();
    }
    
    xSemaphoreGive(g_bq40z50_ctrl.data_mutex);
    return ret;
}

/**
 * @brief I2C初始化
 */
static esp_err_t bq40z50_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BQ40Z50_I2C_MASTER_SDA_IO,
        .scl_io_num = BQ40Z50_I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = BQ40Z50_I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(BQ40Z50_I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return i2c_driver_install(BQ40Z50_I2C_MASTER_NUM, conf.mode,
                             BQ40Z50_I2C_MASTER_RX_BUF_LEN, 
                             BQ40Z50_I2C_MASTER_TX_BUF_LEN, 0);
}

/**
 * @brief I2C反初始化
 */
static esp_err_t bq40z50_i2c_deinit(void)
{
    return i2c_driver_delete(BQ40Z50_I2C_MASTER_NUM);
}

/**
 * @brief 解析电池状态寄存器
 */
static esp_err_t bq40z50_parse_battery_status(uint16_t status)
{
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    
    data->is_charging = !(status & BQ40Z50_STATUS_DISCHARGING);
    data->is_discharging = (status & BQ40Z50_STATUS_DISCHARGING) != 0;
    data->is_fully_charged = (status & BQ40Z50_STATUS_FULLY_CHARGED) != 0;
    data->is_fully_discharged = (status & BQ40Z50_STATUS_FULLY_DISCHARGED) != 0;
    
    data->over_charged_alarm = (status & BQ40Z50_STATUS_OVER_CHARGED_ALARM) != 0;
    data->over_temp_alarm = (status & BQ40Z50_STATUS_OVER_TEMP_ALARM) != 0;
    data->remaining_capacity_alarm = (status & BQ40Z50_STATUS_REMAINING_CAPACITY_ALARM) != 0;
    data->remaining_time_alarm = (status & BQ40Z50_STATUS_REMAINING_TIME_ALARM) != 0;
    
    // 检查是否有任何报警
    data->has_error = data->over_charged_alarm || data->over_temp_alarm || 
                     data->remaining_capacity_alarm || data->remaining_time_alarm;
    
    return ESP_OK;
}

/**
 * @brief 解析电池模式寄存器
 */
static esp_err_t bq40z50_parse_battery_mode(uint16_t mode)
{
    // 电池模式解析可以根据需要扩展
    return ESP_OK;
}

/**
 * @brief 温度转换（开尔文转摄氏度）
 */
static float bq40z50_temperature_k_to_c(uint16_t temp_k)
{
    // BQ40Z50温度单位是0.1开尔文
    return (float)temp_k / 10.0f - 273.15f;
}

/**
 * @brief 验证数据有效性
 */
static bool bq40z50_validate_data(void)
{
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    
    // 检查电压范围（8V - 20V合理）
    if (data->voltage_mv < 8000 || data->voltage_mv > 20000) {
        return false;
    }
    
    // 检查电流范围（-30A 到 +30A合理，适合int16_t范围）
    if (data->current_ma < -30000 || data->current_ma > 30000) {
        return false;
    }
    
    // 检查SOC范围
    if (data->relative_soc > 100) {
        return false;
    }
    
    // 检查温度范围（-40°C 到 85°C合理）
    if (data->temperature_c < -40.0f || data->temperature_c > 85.0f) {
        return false;
    }
    
    return true;
}

// 电池信息获取接口实现
bq40z50_data_t bq40z50_get_battery_data(void)
{
    if (xSemaphoreTake(g_bq40z50_ctrl.data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bq40z50_data_t data = g_bq40z50_ctrl.battery_data;
        xSemaphoreGive(g_bq40z50_ctrl.data_mutex);
        return data;
    }
    
    // 返回无效数据
    bq40z50_data_t invalid_data = {0};
    invalid_data.data_valid = false;
    return invalid_data;
}

uint16_t bq40z50_get_voltage_mv(void) 
{ 
    return g_bq40z50_ctrl.battery_data.voltage_mv; 
}

int16_t bq40z50_get_current_ma(void) 
{ 
    return g_bq40z50_ctrl.battery_data.current_ma; 
}

uint8_t bq40z50_get_soc_percent(void) 
{ 
    return g_bq40z50_ctrl.battery_data.relative_soc; 
}

float bq40z50_get_temperature_c(void) 
{ 
    return g_bq40z50_ctrl.battery_data.temperature_c; 
}

bool bq40z50_is_charging(void) 
{ 
    return g_bq40z50_ctrl.battery_data.is_charging; 
}

bool bq40z50_is_fully_charged(void) 
{ 
    return g_bq40z50_ctrl.battery_data.is_fully_charged; 
}

/**
 * @brief 检查电池是否存在
 */
bool bq40z50_is_battery_present(void)
{
    if (!g_bq40z50_initialized) {
        return false;
    }
    
    uint16_t voltage;
    esp_err_t ret = bq40z50_read_word(BQ40Z50_CMD_VOLTAGE, &voltage);
    return (ret == ESP_OK && voltage > 0);
}

/**
 * @brief 检查是否有错误
 */
bool bq40z50_has_error(void)
{
    return g_bq40z50_ctrl.battery_data.has_error;
}

/**
 * @brief 检查是否有报警
 */
bool bq40z50_has_alarm(void)
{
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    return data->over_charged_alarm || data->over_temp_alarm || 
           data->remaining_capacity_alarm || data->remaining_time_alarm;
}

/**
 * @brief 安全状态检查
 */
esp_err_t bq40z50_check_safety_status(void)
{
    if (!g_bq40z50_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    bq40z50_data_t* data = &g_bq40z50_ctrl.battery_data;
    
    // 检查电压范围
    if (data->voltage_mv < g_bq40z50_ctrl.low_voltage_threshold) {
        ESP_LOGW(TAG, "电池电压过低: %d mV", data->voltage_mv);
        
        event_message_t event = {
            .type = EVENT_BATTERY_LOW,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
        
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data->voltage_mv > g_bq40z50_ctrl.high_voltage_threshold) {
        ESP_LOGW(TAG, "电池电压过高: %d mV", data->voltage_mv);
        
        event_message_t event = {
            .type = EVENT_BATTERY_HIGH,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
        
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查SOC
    if (data->relative_soc <= g_bq40z50_ctrl.critical_soc_threshold) {
        ESP_LOGW(TAG, "电池电量危险: %d%%", data->relative_soc);
        
        event_message_t event = {
            .type = EVENT_BATTERY_LOW,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
        
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

/**
 * @brief 打印电池信息
 */
void bq40z50_print_battery_info(void)
{
    if (!g_bq40z50_initialized) {
        ESP_LOGI(TAG, "BQ40Z50未初始化");
        return;
    }
    
    bq40z50_data_t data = bq40z50_get_battery_data();
    
    ESP_LOGI(TAG, "========== 电池信息 ==========");
    ESP_LOGI(TAG, "电压: %d mV", data.voltage_mv);
    ESP_LOGI(TAG, "电流: %d mA", data.current_ma);
    ESP_LOGI(TAG, "电量: %d%%", data.relative_soc);
    ESP_LOGI(TAG, "温度: %.1f°C", data.temperature_c);
    ESP_LOGI(TAG, "剩余容量: %d mAh", data.remaining_capacity_mah);
    ESP_LOGI(TAG, "满充容量: %d mAh", data.full_charge_capacity_mah);
    ESP_LOGI(TAG, "循环次数: %d", data.cycle_count);
    ESP_LOGI(TAG, "充电状态: %s", data.is_charging ? "充电中" : "未充电");
    ESP_LOGI(TAG, "状态: %s", data.data_valid ? "正常" : "异常");
    ESP_LOGI(TAG, "==============================");
}

// 删除重复的函数定义 