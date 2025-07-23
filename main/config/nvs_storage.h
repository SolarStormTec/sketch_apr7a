/**
 * @file nvs_storage.h
 * @brief NVS存储管理模块
 * @author SmartJet Team
 * @date 2024
 */

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "smartjet_common.h"
#include "nvs_flash.h"
#include "nvs.h"

// NVS命名空间定义
#define NVS_NAMESPACE_DEVICE        "device"        // 设备配置
#define NVS_NAMESPACE_WIFI          "wifi"          // WiFi配置
#define NVS_NAMESPACE_SYSTEM        "system"        // 系统配置
#define NVS_NAMESPACE_STATS         "stats"         // 统计数据
#define NVS_NAMESPACE_BACKUP        "backup"        // 备份数据
#define NVS_NAMESPACE_OTA           "ota"           // OTA配置
#define NVS_NAMESPACE_CALIBRATION   "calibration"   // 校准数据

// 最大键名长度
#define NVS_MAX_KEY_LENGTH          15
#define NVS_MAX_STRING_LENGTH       512
#define NVS_MAX_BLOB_SIZE          2048

// NVS操作结果
typedef enum {
    NVS_RESULT_OK = 0,              // 成功
    NVS_RESULT_ERROR,               // 一般错误
    NVS_RESULT_NOT_FOUND,           // 键未找到
    NVS_RESULT_NO_MEMORY,           // 内存不足
    NVS_RESULT_INVALID_ARG,         // 无效参数
    NVS_RESULT_INVALID_SIZE,        // 大小无效
    NVS_RESULT_READ_ONLY,           // 只读
    NVS_RESULT_NOT_ENOUGH_SPACE     // 存储空间不足
} nvs_result_t;

// NVS存储类型
typedef enum {
    STORAGE_TYPE_U8 = 0,           // 8位无符号整数
    STORAGE_TYPE_I8,               // 8位有符号整数
    STORAGE_TYPE_U16,              // 16位无符号整数
    STORAGE_TYPE_I16,              // 16位有符号整数
    STORAGE_TYPE_U32,              // 32位无符号整数
    STORAGE_TYPE_I32,              // 32位有符号整数
    STORAGE_TYPE_U64,              // 64位无符号整数
    STORAGE_TYPE_I64,              // 64位有符号整数
    STORAGE_TYPE_STR,              // 字符串
    STORAGE_TYPE_BLOB              // 二进制数据
} nvs_storage_type_t;

// NVS句柄管理结构
typedef struct {
    nvs_handle_t handle;           // NVS句柄
    const char* namespace_name;    // 命名空间名称
    bool is_open;                 // 是否已打开
    uint32_t open_count;          // 打开计数
} nvs_handle_info_t;

// NVS管理器结构
typedef struct {
    nvs_handle_info_t handles[8];  // 句柄数组
    uint8_t handle_count;         // 句柄数量
    SemaphoreHandle_t nvs_mutex;  // NVS操作互斥锁
    bool initialized;             // 初始化标志
} nvs_manager_t;

// 函数声明
esp_err_t nvs_storage_init(void);
void nvs_storage_deinit(void);

// 句柄管理
nvs_result_t nvs_storage_open(const char* namespace_name, nvs_handle_t* handle);
nvs_result_t nvs_storage_close(nvs_handle_t handle);
nvs_result_t nvs_storage_commit(nvs_handle_t handle);

// 基本读写操作
nvs_result_t nvs_storage_set_u8(nvs_handle_t handle, const char* key, uint8_t value);
nvs_result_t nvs_storage_get_u8(nvs_handle_t handle, const char* key, uint8_t* value);

nvs_result_t nvs_storage_set_u16(nvs_handle_t handle, const char* key, uint16_t value);
nvs_result_t nvs_storage_get_u16(nvs_handle_t handle, const char* key, uint16_t* value);

nvs_result_t nvs_storage_set_u32(nvs_handle_t handle, const char* key, uint32_t value);
nvs_result_t nvs_storage_get_u32(nvs_handle_t handle, const char* key, uint32_t* value);

nvs_result_t nvs_storage_set_u64(nvs_handle_t handle, const char* key, uint64_t value);
nvs_result_t nvs_storage_get_u64(nvs_handle_t handle, const char* key, uint64_t* value);

nvs_result_t nvs_storage_set_string(nvs_handle_t handle, const char* key, const char* value);
nvs_result_t nvs_storage_get_string(nvs_handle_t handle, const char* key, char* value, size_t* length);

nvs_result_t nvs_storage_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length);
nvs_result_t nvs_storage_get_blob(nvs_handle_t handle, const char* key, void* value, size_t* length);

// 高级操作
nvs_result_t nvs_storage_erase_key(nvs_handle_t handle, const char* key);
nvs_result_t nvs_storage_erase_namespace(const char* namespace_name);
nvs_result_t nvs_storage_get_used_entry_count(nvs_handle_t handle, size_t* used_entries);
nvs_result_t nvs_storage_get_free_entry_count(nvs_handle_t handle, size_t* free_entries);

// 便利函数 - 设备配置
nvs_result_t nvs_storage_save_device_config(const device_config_t* config);
nvs_result_t nvs_storage_load_device_config(device_config_t* config);

// 便利函数 - WiFi配置
nvs_result_t nvs_storage_save_wifi_config(const smartjet_wifi_config_t* config);
nvs_result_t nvs_storage_load_wifi_config(smartjet_wifi_config_t* config);

// 便利函数 - 系统配置
nvs_result_t nvs_storage_save_system_config(const sys_config_t* config);
nvs_result_t nvs_storage_load_system_config(sys_config_t* config);

// 便利函数 - 统计数据
nvs_result_t nvs_storage_save_system_stats(const system_stats_t* stats);
nvs_result_t nvs_storage_load_system_stats(system_stats_t* stats);

// 便利函数 - OTA配置
nvs_result_t nvs_storage_save_ota_config(const ota_config_t* config);
nvs_result_t nvs_storage_load_ota_config(ota_config_t* config);

// 数据校验和备份
nvs_result_t nvs_storage_create_backup(const char* namespace_name);
nvs_result_t nvs_storage_restore_backup(const char* namespace_name);
nvs_result_t nvs_storage_verify_data_integrity(const char* namespace_name);

// 工厂重置
nvs_result_t nvs_storage_factory_reset(void);
nvs_result_t nvs_storage_reset_namespace(const char* namespace_name);

// 诊断和维护
void nvs_storage_dump_info(void);
nvs_result_t nvs_storage_get_stats(nvs_stats_t* stats);
size_t nvs_storage_get_total_size(void);
size_t nvs_storage_get_used_size(void);
size_t nvs_storage_get_free_size(void);

// 内部函数声明
// 以下函数暂时注释，未实现
// static nvs_result_t nvs_error_to_result(esp_err_t err);
// static nvs_handle_info_t* nvs_find_handle_info(nvs_handle_t handle);
// static nvs_handle_info_t* nvs_find_namespace_info(const char* namespace_name);
// static nvs_result_t nvs_storage_validate_key(const char* key);

#endif // NVS_STORAGE_H 