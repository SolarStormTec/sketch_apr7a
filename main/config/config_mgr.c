/**
 * @file config_mgr.c
 * @brief 配置管理器实现
 * @author SmartJet Team
 * @date 2024
 * 
 * 配置管理器负责系统配置的加载、保存和管理
 */

#include "config_mgr.h"
#include "include/smartjet_common.h"
#include "nvs_flash.h"
#include "nvs.h"

// 日志标签
static const char* TAG = "CONFIG_MGR";

// ================================================================================
// 内部数据结构
// ================================================================================

// 配置管理器状态
static struct {
    bool initialized;
    nvs_handle_t device_handle;
    nvs_handle_t wifi_handle;
    nvs_handle_t system_handle;
    nvs_handle_t stats_handle;
    nvs_handle_t backup_handle;
    bool handles_opened;
} config_mgr = {0};

// 默认系统配置
static const sys_config_t default_system_config = {
    // 发动机控制参数
    .engine_start_pull_time = 3000,        // 3秒启动脉冲
    .engine_start_wait_time = 5000,        // 5秒等待时间
    .engine_stop_pull_time = 2000,         // 2秒停机脉冲
    .engine_stop_wait_time = 3000,         // 3秒停机等待
    .wake_pulse_ms = 200,                  // 200ms唤醒脉冲
    .wake_to_long_ms = 500,                // 500ms唤醒间隔
    
    // 传感器参数
    .engine_on_rpm_threshold = 1800,       // 1800 RPM启动阈值
    .rpm_alarm_threshold = 3600,           // 3600 RPM报警阈值
    .rpm_calibration = 1.0,                // 默认校准系数
    
    // 电压保护参数
    .high_voltage_warning_threshold = 15.0,  // 15V警告
    .high_voltage_protect_threshold = 16.0,  // 16V保护
    .low_voltage_warning_threshold = 11.0,   // 11V警告
    .low_voltage_protect_threshold = 10.0,   // 10V保护
    
    // 维护参数
    .maintenance_interval_hours = 200,      // 200小时维护间隔
    
    // 网络参数
    .wifi_rssi_weak_threshold = -75,        // -75dBm弱信号阈值
    .mqtt_report_interval_active = 1500,    // 1.5秒活跃上报间隔
    .mqtt_report_interval_idle = 60000,     // 60秒空闲上报间隔
    
    // OTA参数
    .auto_update_enabled = true,            // 启用自动更新
    .ota_check_interval_minutes = 60,       // 60分钟检查间隔
    .ota_rssi_min = -75,                   // -75dBm最小信号要求
    
    // 系统参数
    .memory_warning_threshold = 25600,      // 25KB内存警告阈值
    .watchdog_timeout_ms = 8000,           // 8秒看门狗超时
    .debug_mode_enabled = false            // 调试模式关闭
};

// ================================================================================
// 内部函数声明
// ================================================================================
static esp_err_t open_nvs_handles(void);
static void close_nvs_handles(void);
static esp_err_t load_default_device_config(device_config_t* config);
static esp_err_t load_default_wifi_config(smartjet_wifi_config_t* config);

// ================================================================================
// 公共函数实现
// ================================================================================

esp_err_t config_manager_init(void)
{
    if (config_mgr.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化配置管理器...");
    
    // 打开NVS句柄
    esp_err_t ret = open_nvs_handles();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS句柄失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 加载默认配置
    ret = config_manager_load_defaults();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "加载默认配置失败: %s", esp_err_to_name(ret));
        close_nvs_handles();
        return ret;
    }
    
    config_mgr.initialized = true;
    
    ESP_LOGI(TAG, "配置管理器初始化完成");
    return ESP_OK;
}

void config_manager_cleanup(void)
{
    if (!config_mgr.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "清理配置管理器...");
    
    // 保存当前配置
    config_manager_save_all();
    
    // 关闭NVS句柄
    close_nvs_handles();
    
    config_mgr.initialized = false;
    
    ESP_LOGI(TAG, "配置管理器清理完成");
}

esp_err_t config_manager_load_defaults(void)
{
    ESP_LOGI(TAG, "加载默认配置...");
    
    // 加载系统配置
    memcpy(&g_smartjet.config, &default_system_config, sizeof(sys_config_t));
    
    // 加载设备配置
    device_config_t device_config;
    esp_err_t ret = config_manager_load_device_config(&device_config);
    if (ret == ESP_OK) {
        // 复制到全局MQTT配置中
        strncpy(g_smartjet.cloud.product_id, device_config.product_id, MAX_PRODUCT_ID_LEN - 1);
        strncpy(g_smartjet.cloud.device_name, device_config.device_name, MAX_DEVICE_NAME_LEN - 1);
        strncpy(g_smartjet.cloud.device_key, device_config.device_secret, MAX_DEVICE_KEY_LEN - 1);
    } else {
        ESP_LOGW(TAG, "设备配置加载失败，使用默认值");
        // 设置默认设备配置
        strcpy(g_smartjet.cloud.product_id, "SmartJet");
        strcpy(g_smartjet.cloud.device_name, "smartjet_default");
        strcpy(g_smartjet.cloud.device_key, "default_key");
    }
    
    // 设置默认MQTT配置
    g_smartjet.cloud.state = MQTT_STATE_DISCONNECTED;
    g_smartjet.cloud.publish_interval = g_smartjet.config.mqtt_report_interval_active;
    g_smartjet.cloud.message_id = 1;
    g_smartjet.cloud.auto_report = true;
    g_smartjet.cloud.rssi_threshold = g_smartjet.config.ota_rssi_min;
    
    // 加载统计数据
    ret = config_manager_load_stats(&g_smartjet.stats);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "统计数据加载失败，初始化为默认值");
        memset(&g_smartjet.stats, 0, sizeof(system_stats_t));
    }
    
    ESP_LOGI(TAG, "默认配置加载完成");
    return ESP_OK;
}

esp_err_t config_manager_save_all(void)
{
    ESP_LOGI(TAG, "保存所有配置...");
    
    esp_err_t ret = ESP_OK;
    
    // 保存系统配置
    ret = config_manager_save_system_config(&g_smartjet.config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "系统配置保存失败: %s", esp_err_to_name(ret));
    }
    
    // 保存统计数据
    ret = config_manager_save_stats(&g_smartjet.stats);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "统计数据保存失败: %s", esp_err_to_name(ret));
    }
    
    // 备份统计数据
    config_manager_backup_stats();
    
    ESP_LOGI(TAG, "配置保存完成");
    return ESP_OK;
}

esp_err_t config_manager_factory_reset(void)
{
    ESP_LOGW(TAG, "执行恢复出厂设置...");
    
    // 擦除所有NVS命名空间
    esp_err_t ret;
    
    ret = nvs_erase_all(config_mgr.device_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "擦除设备配置失败: %s", esp_err_to_name(ret));
    }
    
    ret = nvs_erase_all(config_mgr.wifi_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "擦除WiFi配置失败: %s", esp_err_to_name(ret));
    }
    
    ret = nvs_erase_all(config_mgr.system_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "擦除系统配置失败: %s", esp_err_to_name(ret));
    }
    
    ret = nvs_erase_all(config_mgr.stats_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "擦除统计数据失败: %s", esp_err_to_name(ret));
    }
    
    ret = nvs_erase_all(config_mgr.backup_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "擦除备份数据失败: %s", esp_err_to_name(ret));
    }
    
    // 提交更改
    nvs_commit(config_mgr.device_handle);
    nvs_commit(config_mgr.wifi_handle);
    nvs_commit(config_mgr.system_handle);
    nvs_commit(config_mgr.stats_handle);
    nvs_commit(config_mgr.backup_handle);
    
    // 重新加载默认配置
    config_manager_load_defaults();
    
    ESP_LOGW(TAG, "恢复出厂设置完成");
    return ESP_OK;
}

// ================================================================================
// 设备配置相关函数
// ================================================================================

esp_err_t config_manager_load_device_config(device_config_t* config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!config_mgr.handles_opened) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    size_t required_size;
    
    // 加载产品ID
    required_size = MAX_PRODUCT_ID_LEN;
    ret = nvs_get_str(config_mgr.device_handle, "product_id", config->product_id, &required_size);
    if (ret != ESP_OK) {
        load_default_device_config(config);
        return ret;
    }
    
    // 加载设备名称
    required_size = MAX_DEVICE_NAME_LEN;
    ret = nvs_get_str(config_mgr.device_handle, "device_name", config->device_name, &required_size);
    if (ret != ESP_OK) {
        load_default_device_config(config);
        return ret;
    }
    
    // 加载设备密钥
    required_size = MAX_DEVICE_KEY_LEN;
    ret = nvs_get_str(config_mgr.device_handle, "device_key", config->device_secret, &required_size);
    if (ret != ESP_OK) {
        load_default_device_config(config);
        return ret;
    }
    
    // 加载配置版本
    ret = nvs_get_u32(config_mgr.device_handle, "config_version", &config->config_version);
    if (ret != ESP_OK) {
        config->config_version = CONFIG_VERSION;
    }
    
    // 加载首次启动标志
    uint8_t first_boot_u8;
    ret = nvs_get_u8(config_mgr.device_handle, "first_boot", &first_boot_u8);
    if (ret != ESP_OK) {
        config->first_boot = true;
    } else {
        config->first_boot = (first_boot_u8 != 0);
    }
    
    return ESP_OK;
}

esp_err_t config_manager_save_device_config(const device_config_t* config)
{
    if (!config || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret;
    
    // 保存产品ID
    ret = nvs_set_str(config_mgr.device_handle, "product_id", config->product_id);
    if (ret != ESP_OK) return ret;
    
    // 保存设备名称
    ret = nvs_set_str(config_mgr.device_handle, "device_name", config->device_name);
    if (ret != ESP_OK) return ret;
    
    // 保存设备密钥
    ret = nvs_set_str(config_mgr.device_handle, "device_key", config->device_secret);
    if (ret != ESP_OK) return ret;
    
    // 保存配置版本
    ret = nvs_set_u32(config_mgr.device_handle, "config_version", config->config_version);
    if (ret != ESP_OK) return ret;
    
    // 保存首次启动标志
    ret = nvs_set_u8(config_mgr.device_handle, "first_boot", config->first_boot ? 1 : 0);
    if (ret != ESP_OK) return ret;
    
    // 提交更改
    return nvs_commit(config_mgr.device_handle);
}

bool config_manager_validate_device_config(const device_config_t* config)
{
    if (!config) {
        return false;
    }
    
    // 检查产品ID
    if (strlen(config->product_id) < 4 || strlen(config->product_id) >= MAX_PRODUCT_ID_LEN) {
        return false;
    }
    
    // 检查设备名称
    if (strlen(config->device_name) < 2 || strlen(config->device_name) >= MAX_DEVICE_NAME_LEN) {
        return false;
    }
    
    // 检查设备密钥
    if (strlen(config->device_secret) < 16 || strlen(config->device_secret) >= MAX_DEVICE_KEY_LEN) {
        return false;
    }
    
    return true;
}

// ================================================================================
// 系统配置相关函数
// ================================================================================

esp_err_t config_manager_load_system_config(sys_config_t* config)
{
    if (!config || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 先设置默认值
    memcpy(config, &default_system_config, sizeof(sys_config_t));
    
    esp_err_t ret;
    size_t required_size = sizeof(sys_config_t);
    
    // 尝试从NVS加载
    ret = nvs_get_blob(config_mgr.system_handle, "sys_config", config, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "系统配置加载失败，使用默认值: %s", esp_err_to_name(ret));
        // 保存默认配置到NVS
        config_manager_save_system_config(config);
    }
    
    return ESP_OK;
}

esp_err_t config_manager_save_system_config(const sys_config_t* config)
{
    if (!config || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = nvs_set_blob(config_mgr.system_handle, "sys_config", config, sizeof(sys_config_t));
    if (ret != ESP_OK) {
        return ret;
    }
    
    return nvs_commit(config_mgr.system_handle);
}

esp_err_t config_manager_update_system_param(const char* key, const void* value, size_t size)
{
    if (!key || !value || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = nvs_set_blob(config_mgr.system_handle, key, value, size);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return nvs_commit(config_mgr.system_handle);
}

// ================================================================================
// 统计数据相关函数
// ================================================================================

esp_err_t config_manager_load_stats(system_stats_t* stats)
{
    if (!stats || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 初始化为默认值
    memset(stats, 0, sizeof(system_stats_t));
    
    esp_err_t ret;
    size_t required_size = sizeof(system_stats_t);
    
    // 尝试从NVS加载
    ret = nvs_get_blob(config_mgr.stats_handle, "system_stats", stats, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "统计数据加载失败: %s", esp_err_to_name(ret));
        
        // 尝试从备份恢复
        ret = config_manager_restore_stats_from_backup();
        if (ret == ESP_OK) {
            required_size = sizeof(system_stats_t);
            ret = nvs_get_blob(config_mgr.stats_handle, "system_stats", stats, &required_size);
        }
    }
    
    return ret;
}

esp_err_t config_manager_save_stats(const system_stats_t* stats)
{
    if (!stats || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = nvs_set_blob(config_mgr.stats_handle, "system_stats", stats, sizeof(system_stats_t));
    if (ret != ESP_OK) {
        return ret;
    }
    
    return nvs_commit(config_mgr.stats_handle);
}

esp_err_t config_manager_reset_stats(void)
{
    ESP_LOGW(TAG, "重置统计数据");
    
    system_stats_t empty_stats;
    memset(&empty_stats, 0, sizeof(system_stats_t));
    
    return config_manager_save_stats(&empty_stats);
}

esp_err_t config_manager_backup_stats(void)
{
    if (!config_mgr.handles_opened) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = nvs_set_blob(config_mgr.backup_handle, "stats_backup", 
                                &g_smartjet.stats, sizeof(system_stats_t));
    if (ret != ESP_OK) {
        return ret;
    }
    
    return nvs_commit(config_mgr.backup_handle);
}

esp_err_t config_manager_restore_stats_from_backup(void)
{
    if (!config_mgr.handles_opened) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "从备份恢复统计数据");
    
    system_stats_t backup_stats;
    size_t required_size = sizeof(system_stats_t);
    
    esp_err_t ret = nvs_get_blob(config_mgr.backup_handle, "stats_backup", 
                                &backup_stats, &required_size);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 恢复到主存储
    return config_manager_save_stats(&backup_stats);
}

// ================================================================================
// WiFi配置相关函数
// ================================================================================

esp_err_t config_manager_load_wifi_config(smartjet_wifi_config_t* config)
{
    if (!config || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret;
    size_t required_size;
    
    // 加载SSID
    required_size = MAX_WIFI_SSID_LEN;
    ret = nvs_get_str(config_mgr.wifi_handle, "ssid", config->ssid, &required_size);
    if (ret != ESP_OK) {
        load_default_wifi_config(config);
        return ret;
    }
    
    // 加载密码
    required_size = MAX_WIFI_PASS_LEN;
    ret = nvs_get_str(config_mgr.wifi_handle, "password", config->password, &required_size);
    if (ret != ESP_OK) {
        load_default_wifi_config(config);
        return ret;
    }
    
    // 加载其他参数
    uint8_t auto_connect_u8;
    ret = nvs_get_u8(config_mgr.wifi_handle, "auto_connect", &auto_connect_u8);
    config->auto_connect = (ret == ESP_OK) ? (auto_connect_u8 != 0) : true;
    
    ret = nvs_get_u8(config_mgr.wifi_handle, "max_retry", &config->max_retry);
    if (ret != ESP_OK) {
        config->max_retry = 5;
    }
    
    ret = nvs_get_u32(config_mgr.wifi_handle, "connect_timeout", &config->connect_timeout_ms);
    if (ret != ESP_OK) {
        config->connect_timeout_ms = 10000;
    }
    
    return ESP_OK;
}

esp_err_t config_manager_save_wifi_config(const smartjet_wifi_config_t* config)
{
    if (!config || !config_mgr.handles_opened) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret;
    
    // 保存SSID
    ret = nvs_set_str(config_mgr.wifi_handle, "ssid", config->ssid);
    if (ret != ESP_OK) return ret;
    
    // 保存密码
    ret = nvs_set_str(config_mgr.wifi_handle, "password", config->password);
    if (ret != ESP_OK) return ret;
    
    // 保存其他参数
    ret = nvs_set_u8(config_mgr.wifi_handle, "auto_connect", config->auto_connect ? 1 : 0);
    if (ret != ESP_OK) return ret;
    
    ret = nvs_set_u8(config_mgr.wifi_handle, "max_retry", config->max_retry);
    if (ret != ESP_OK) return ret;
    
    ret = nvs_set_u32(config_mgr.wifi_handle, "connect_timeout", config->connect_timeout_ms);
    if (ret != ESP_OK) return ret;
    
    return nvs_commit(config_mgr.wifi_handle);
}

esp_err_t config_manager_clear_wifi_config(void)
{
    if (!config_mgr.handles_opened) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = nvs_erase_all(config_mgr.wifi_handle);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }
    
    return nvs_commit(config_mgr.wifi_handle);
}

// ================================================================================
// 内部函数实现
// ================================================================================

/**
 * @brief 打开NVS句柄
 */
static esp_err_t open_nvs_handles(void)
{
    esp_err_t ret;
    
    // 打开设备配置句柄
    ret = nvs_open(CONFIG_NAMESPACE_DEVICE, NVS_READWRITE, &config_mgr.device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开设备配置NVS失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 打开WiFi配置句柄
    ret = nvs_open(CONFIG_NAMESPACE_WIFI, NVS_READWRITE, &config_mgr.wifi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开WiFi配置NVS失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 打开系统配置句柄
    ret = nvs_open(CONFIG_NAMESPACE_SYSTEM, NVS_READWRITE, &config_mgr.system_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开系统配置NVS失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 打开统计数据句柄
    ret = nvs_open(CONFIG_NAMESPACE_STATS, NVS_READWRITE, &config_mgr.stats_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开统计数据NVS失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 打开备份数据句柄
    ret = nvs_open(CONFIG_NAMESPACE_BACKUP, NVS_READWRITE, &config_mgr.backup_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开备份数据NVS失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    config_mgr.handles_opened = true;
    return ESP_OK;
    
cleanup:
    close_nvs_handles();
    return ret;
}

/**
 * @brief 关闭NVS句柄
 */
static void close_nvs_handles(void)
{
    if (config_mgr.handles_opened) {
        nvs_close(config_mgr.device_handle);
        nvs_close(config_mgr.wifi_handle);
        nvs_close(config_mgr.system_handle);
        nvs_close(config_mgr.stats_handle);
        nvs_close(config_mgr.backup_handle);
        config_mgr.handles_opened = false;
    }
}

/**
 * @brief 加载默认设备配置
 */
static esp_err_t load_default_device_config(device_config_t* config)
{
    strcpy(config->product_id, "SmartJet");
    strcpy(config->device_name, "smartjet_default");
    strcpy(config->device_secret, "default_device_key_123456");
    config->config_version = CONFIG_VERSION;
    config->first_boot = true;
    
    return ESP_OK;
}

/**
 * @brief 加载默认WiFi配置
 */
static esp_err_t load_default_wifi_config(smartjet_wifi_config_t* config)
{
    memset(config, 0, sizeof(smartjet_wifi_config_t));
    config->auto_connect = true;
    config->max_retry = 5;
    config->connect_timeout_ms = 10000;
    
    return ESP_OK;
} 