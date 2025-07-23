/**
 * @file nvs_storage.c
 * @brief NVS存储管理模块实现
 * @author SmartJet Team
 * @date 2024
 */

#include "nvs_storage.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "NVS_STORAGE";

// 全局NVS管理器实例
static nvs_manager_t g_nvs_manager = {0};
static bool g_nvs_initialized = false;

// 静态函数声明
static nvs_result_t nvs_error_to_result(esp_err_t err);
static nvs_handle_info_t* nvs_find_handle_info(nvs_handle_t handle);
static nvs_handle_info_t* nvs_find_namespace_info(const char* namespace_name);
static nvs_result_t nvs_storage_validate_key(const char* key);

/**
 * @brief NVS存储管理器初始化
 */
esp_err_t nvs_storage_init(void)
{
    if (g_nvs_initialized) {
        ESP_LOGW(TAG, "NVS存储管理器已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化NVS存储管理器...");
    
    // 初始化NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS分区需要擦除，正在擦除...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化NVS Flash失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 创建互斥锁
    g_nvs_manager.nvs_mutex = xSemaphoreCreateMutex();
    if (!g_nvs_manager.nvs_mutex) {
        ESP_LOGE(TAG, "创建NVS互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化句柄数组
    memset(g_nvs_manager.handles, 0, sizeof(g_nvs_manager.handles));
    g_nvs_manager.handle_count = 0;
    g_nvs_manager.initialized = true;
    
    g_nvs_initialized = true;
    ESP_LOGI(TAG, "NVS存储管理器初始化完成");
    
    return ESP_OK;
}

/**
 * @brief NVS存储管理器反初始化
 */
void nvs_storage_deinit(void)
{
    if (!g_nvs_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化NVS存储管理器...");
    
    // 关闭所有打开的句柄
    for (int i = 0; i < g_nvs_manager.handle_count; i++) {
        if (g_nvs_manager.handles[i].is_open) {
            nvs_close(g_nvs_manager.handles[i].handle);
        }
    }
    
    // 删除互斥锁
    if (g_nvs_manager.nvs_mutex) {
        vSemaphoreDelete(g_nvs_manager.nvs_mutex);
        g_nvs_manager.nvs_mutex = NULL;
    }
    
    g_nvs_initialized = false;
    ESP_LOGI(TAG, "NVS存储管理器反初始化完成");
}

/**
 * @brief 打开NVS命名空间
 */
nvs_result_t nvs_storage_open(const char* namespace_name, nvs_handle_t* handle)
{
    if (!g_nvs_initialized || !namespace_name || !handle) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_nvs_manager.nvs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取NVS锁超时");
        return NVS_RESULT_ERROR;
    }
    
    // 检查是否已经打开
    nvs_handle_info_t* info = nvs_find_namespace_info(namespace_name);
    if (info && info->is_open) {
        *handle = info->handle;
        info->open_count++;
        xSemaphoreGive(g_nvs_manager.nvs_mutex);
        return NVS_RESULT_OK;
    }
    
    // 查找空闲槽位
    nvs_handle_info_t* free_slot = NULL;
    for (int i = 0; i < 8; i++) {
        if (!g_nvs_manager.handles[i].is_open) {
            free_slot = &g_nvs_manager.handles[i];
            break;
        }
    }
    
    if (!free_slot) {
        ESP_LOGE(TAG, "没有可用的NVS句柄槽位");
        xSemaphoreGive(g_nvs_manager.nvs_mutex);
        return NVS_RESULT_NO_MEMORY;
    }
    
    // 打开NVS命名空间
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &free_slot->handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开NVS命名空间 '%s' 失败: %s", namespace_name, esp_err_to_name(err));
        xSemaphoreGive(g_nvs_manager.nvs_mutex);
        return nvs_error_to_result(err);
    }
    
    // 更新句柄信息
    free_slot->namespace_name = namespace_name;
    free_slot->is_open = true;
    free_slot->open_count = 1;
    *handle = free_slot->handle;
    
    if (g_nvs_manager.handle_count < 8) {
        g_nvs_manager.handle_count++;
    }
    
    xSemaphoreGive(g_nvs_manager.nvs_mutex);
    ESP_LOGI(TAG, "成功打开NVS命名空间: %s", namespace_name);
    
    return NVS_RESULT_OK;
}

/**
 * @brief 关闭NVS句柄
 */
nvs_result_t nvs_storage_close(nvs_handle_t handle)
{
    if (!g_nvs_initialized) {
        return NVS_RESULT_ERROR;
    }
    
    if (xSemaphoreTake(g_nvs_manager.nvs_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return NVS_RESULT_ERROR;
    }
    
    nvs_handle_info_t* info = nvs_find_handle_info(handle);
    if (!info || !info->is_open) {
        xSemaphoreGive(g_nvs_manager.nvs_mutex);
        return NVS_RESULT_NOT_FOUND;
    }
    
    info->open_count--;
    if (info->open_count == 0) {
        nvs_close(handle);
        info->is_open = false;
        info->namespace_name = NULL;
        g_nvs_manager.handle_count--;
    }
    
    xSemaphoreGive(g_nvs_manager.nvs_mutex);
    return NVS_RESULT_OK;
}

/**
 * @brief 提交NVS更改
 */
nvs_result_t nvs_storage_commit(nvs_handle_t handle)
{
    if (!g_nvs_initialized) {
        return NVS_RESULT_ERROR;
    }
    
    esp_err_t err = nvs_commit(handle);
    return nvs_error_to_result(err);
}

/**
 * @brief 设置8位无符号整数
 */
nvs_result_t nvs_storage_set_u8(nvs_handle_t handle, const char* key, uint8_t value)
{
    if (!g_nvs_initialized || !key) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_result_t validate_result = nvs_storage_validate_key(key);
    if (validate_result != NVS_RESULT_OK) {
        return validate_result;
    }
    
    esp_err_t err = nvs_set_u8(handle, key, value);
    return nvs_error_to_result(err);
}

/**
 * @brief 获取8位无符号整数
 */
nvs_result_t nvs_storage_get_u8(nvs_handle_t handle, const char* key, uint8_t* value)
{
    if (!g_nvs_initialized || !key || !value) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    esp_err_t err = nvs_get_u8(handle, key, value);
    return nvs_error_to_result(err);
}

/**
 * @brief 设置32位无符号整数
 */
nvs_result_t nvs_storage_set_u32(nvs_handle_t handle, const char* key, uint32_t value)
{
    if (!g_nvs_initialized || !key) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_result_t validate_result = nvs_storage_validate_key(key);
    if (validate_result != NVS_RESULT_OK) {
        return validate_result;
    }
    
    esp_err_t err = nvs_set_u32(handle, key, value);
    return nvs_error_to_result(err);
}

/**
 * @brief 获取32位无符号整数
 */
nvs_result_t nvs_storage_get_u32(nvs_handle_t handle, const char* key, uint32_t* value)
{
    if (!g_nvs_initialized || !key || !value) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    esp_err_t err = nvs_get_u32(handle, key, value);
    return nvs_error_to_result(err);
}

/**
 * @brief 设置字符串
 */
nvs_result_t nvs_storage_set_string(nvs_handle_t handle, const char* key, const char* value)
{
    if (!g_nvs_initialized || !key || !value) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_result_t validate_result = nvs_storage_validate_key(key);
    if (validate_result != NVS_RESULT_OK) {
        return validate_result;
    }
    
    if (strlen(value) >= NVS_MAX_STRING_LENGTH) {
        return NVS_RESULT_INVALID_SIZE;
    }
    
    esp_err_t err = nvs_set_str(handle, key, value);
    return nvs_error_to_result(err);
}

/**
 * @brief 获取字符串
 */
nvs_result_t nvs_storage_get_string(nvs_handle_t handle, const char* key, char* value, size_t* length)
{
    if (!g_nvs_initialized || !key || !value || !length) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    esp_err_t err = nvs_get_str(handle, key, value, length);
    return nvs_error_to_result(err);
}

/**
 * @brief 设置二进制数据
 */
nvs_result_t nvs_storage_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length)
{
    if (!g_nvs_initialized || !key || !value || length == 0) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_result_t validate_result = nvs_storage_validate_key(key);
    if (validate_result != NVS_RESULT_OK) {
        return validate_result;
    }
    
    if (length > NVS_MAX_BLOB_SIZE) {
        return NVS_RESULT_INVALID_SIZE;
    }
    
    esp_err_t err = nvs_set_blob(handle, key, value, length);
    return nvs_error_to_result(err);
}

/**
 * @brief 获取二进制数据
 */
nvs_result_t nvs_storage_get_blob(nvs_handle_t handle, const char* key, void* value, size_t* length)
{
    if (!g_nvs_initialized || !key || !value || !length) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    esp_err_t err = nvs_get_blob(handle, key, value, length);
    return nvs_error_to_result(err);
}

/**
 * @brief 删除键
 */
nvs_result_t nvs_storage_erase_key(nvs_handle_t handle, const char* key)
{
    if (!g_nvs_initialized || !key) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    esp_err_t err = nvs_erase_key(handle, key);
    return nvs_error_to_result(err);
}

/**
 * @brief 保存设备配置
 */
nvs_result_t nvs_storage_save_device_config(const device_config_t* config)
{
    if (!config) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_DEVICE, &handle);
    if (result != NVS_RESULT_OK) {
        return result;
    }
    
    do {
        result = nvs_storage_set_string(handle, "product_id", config->product_id);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_string(handle, "device_name", config->device_name);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_string(handle, "device_secret", config->device_secret);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_u32(handle, "config_version", config->config_version);
        if (result != NVS_RESULT_OK) break;
        
        // 提交更改
        nvs_storage_commit(handle);
        
    } while (0);
    
    nvs_storage_close(handle);
    
    if (result == NVS_RESULT_OK) {
        ESP_LOGI(TAG, "设备配置保存成功");
    } else {
        ESP_LOGE(TAG, "设备配置保存失败: %d", result);
    }
    
    return result;
}

/**
 * @brief 加载设备配置
 */
nvs_result_t nvs_storage_load_device_config(device_config_t* config)
{
    if (!config) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_DEVICE, &handle);
    if (result != NVS_RESULT_OK) {
        return result;
    }
    
    do {
        size_t length = sizeof(config->product_id);
        result = nvs_storage_get_string(handle, "product_id", config->product_id, &length);
        if (result != NVS_RESULT_OK) break;
        
        length = sizeof(config->device_name);
        result = nvs_storage_get_string(handle, "device_name", config->device_name, &length);
        if (result != NVS_RESULT_OK) break;
        
        length = sizeof(config->device_secret);
        result = nvs_storage_get_string(handle, "device_secret", config->device_secret, &length);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_get_u32(handle, "config_version", &config->config_version);
        
    } while (0);
    
    nvs_storage_close(handle);
    
    if (result == NVS_RESULT_OK) {
        ESP_LOGI(TAG, "设备配置加载成功");
    } else {
        ESP_LOGW(TAG, "设备配置加载失败，使用默认配置: %d", result);
    }
    
    return result;
}

/**
 * @brief 保存系统统计数据
 */
nvs_result_t nvs_storage_save_system_stats(const system_stats_t* stats)
{
    if (!stats) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_STATS, &handle);
    if (result != NVS_RESULT_OK) {
        return result;
    }
    
    do {
        result = nvs_storage_set_u32(handle, "boot_count", stats->system_restart_count);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_u32(handle, "engine_start_count", stats->engine_start_count);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_u32(handle, "engine_run_hours", stats->total_runtime_minutes / 60);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_u32(handle, "failed_start_count", stats->failed_starts);
        if (result != NVS_RESULT_OK) break;
        
        result = nvs_storage_set_u32(handle, "alarm_count", stats->oil_alarm_count);
        if (result != NVS_RESULT_OK) break;
        
        // ota_update_count 和 last_error_code 字段在当前结构体中不存在，已移除
        
        // 提交更改
        nvs_storage_commit(handle);
        
    } while (0);
    
    nvs_storage_close(handle);
    return result;
}

/**
 * @brief 加载系统统计数据
 */
nvs_result_t nvs_storage_load_system_stats(system_stats_t* stats)
{
    if (!stats) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_STATS, &handle);
    if (result != NVS_RESULT_OK) {
        return result;
    }
    
    // 加载统计数据，如果不存在则使用默认值0
    uint32_t temp_value = 0;
    nvs_storage_get_u32(handle, "boot_count", &temp_value);
    stats->system_restart_count = (uint16_t)temp_value;
    nvs_storage_get_u32(handle, "engine_start_count", &stats->engine_start_count);
    uint32_t temp_hours = 0;
    nvs_storage_get_u32(handle, "engine_run_hours", &temp_hours);
    stats->total_runtime_minutes = temp_hours * 60;  // 转换小时为分钟
    nvs_storage_get_u32(handle, "failed_start_count", &stats->failed_starts);
    temp_value = 0;
    nvs_storage_get_u32(handle, "alarm_count", &temp_value);
    stats->oil_alarm_count = (uint16_t)temp_value;
    // ota_update_count 和 last_error_code 字段在当前结构体中不存在，已移除
    
    nvs_storage_close(handle);
    return NVS_RESULT_OK;
}

/**
 * @brief 工厂重置
 */
nvs_result_t nvs_storage_factory_reset(void)
{
    ESP_LOGW(TAG, "执行工厂重置...");
    
    // 擦除NVS分区
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "擦除NVS分区失败: %s", esp_err_to_name(err));
        return nvs_error_to_result(err);
    }
    
    // 重新初始化NVS
    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "重新初始化NVS失败: %s", esp_err_to_name(err));
        return nvs_error_to_result(err);
    }
    
    ESP_LOGI(TAG, "工厂重置完成");
    return NVS_RESULT_OK;
}

/**
 * @brief ESP错误码转换为NVS结果码
 */
static nvs_result_t nvs_error_to_result(esp_err_t err)
{
    switch (err) {
        case ESP_OK:
            return NVS_RESULT_OK;
        case ESP_ERR_NVS_NOT_FOUND:
            return NVS_RESULT_NOT_FOUND;
        case ESP_ERR_NVS_NO_FREE_PAGES:
        case ESP_ERR_NO_MEM:
            return NVS_RESULT_NO_MEMORY;
        case ESP_ERR_INVALID_ARG:
            return NVS_RESULT_INVALID_ARG;
        case ESP_ERR_NVS_INVALID_LENGTH:
        case ESP_ERR_NVS_VALUE_TOO_LONG:
            return NVS_RESULT_INVALID_SIZE;
        case ESP_ERR_NVS_READ_ONLY:
            return NVS_RESULT_READ_ONLY;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE:
            return NVS_RESULT_NOT_ENOUGH_SPACE;
        default:
            return NVS_RESULT_ERROR;
    }
}

/**
 * @brief 根据句柄查找句柄信息
 */
static nvs_handle_info_t* nvs_find_handle_info(nvs_handle_t handle)
{
    for (int i = 0; i < 8; i++) {
        if (g_nvs_manager.handles[i].is_open && 
            g_nvs_manager.handles[i].handle == handle) {
            return &g_nvs_manager.handles[i];
        }
    }
    return NULL;
}

/**
 * @brief 根据命名空间名称查找句柄信息
 */
static nvs_handle_info_t* nvs_find_namespace_info(const char* namespace_name)
{
    for (int i = 0; i < 8; i++) {
        if (g_nvs_manager.handles[i].is_open && 
            g_nvs_manager.handles[i].namespace_name &&
            strcmp(g_nvs_manager.handles[i].namespace_name, namespace_name) == 0) {
            return &g_nvs_manager.handles[i];
        }
    }
    return NULL;
}

/**
 * @brief 验证键名有效性
 */
static nvs_result_t nvs_storage_validate_key(const char* key)
{
    if (!key) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len >= NVS_MAX_KEY_LENGTH) {
        return NVS_RESULT_INVALID_ARG;
    }
    
    return NVS_RESULT_OK;
}

/**
 * @brief 打印NVS存储信息
 */
void nvs_storage_dump_info(void)
{
    if (!g_nvs_initialized) {
        ESP_LOGI(TAG, "NVS存储管理器未初始化");
        return;
    }
    
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS存储信息:");
        ESP_LOGI(TAG, "  已使用条目: %d", nvs_stats.used_entries);
        ESP_LOGI(TAG, "  可用条目: %d", nvs_stats.free_entries);
        ESP_LOGI(TAG, "  总条目数: %d", nvs_stats.total_entries);
        ESP_LOGI(TAG, "  命名空间数: %d", nvs_stats.namespace_count);
    }
    
    ESP_LOGI(TAG, "打开的句柄数: %d", g_nvs_manager.handle_count);
    for (int i = 0; i < 8; i++) {
        if (g_nvs_manager.handles[i].is_open) {
            ESP_LOGI(TAG, "  句柄 %d: %s (引用计数: %d)", 
                     i, g_nvs_manager.handles[i].namespace_name, 
                     g_nvs_manager.handles[i].open_count);
        }
    }
} 