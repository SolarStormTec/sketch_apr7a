/**
 * @file config_mgr.h
 * @brief 配置管理器头文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 配置管理器负责系统配置的加载、保存和管理
 */

#ifndef CONFIG_MGR_H
#define CONFIG_MGR_H

#include "include/smartjet_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================================
// 配置相关常量
// ================================================================================
#define CONFIG_NAMESPACE_DEVICE     "device"        // 设备配置命名空间
#define CONFIG_NAMESPACE_WIFI       "wifi"          // WiFi配置命名空间
#define CONFIG_NAMESPACE_SYSTEM     "system"        // 系统配置命名空间
#define CONFIG_NAMESPACE_ENGINE     "engine"        // 发动机配置命名空间
#define CONFIG_NAMESPACE_STATS      "stats"         // 统计数据命名空间
#define CONFIG_NAMESPACE_BACKUP     "backup"        // 备份数据命名空间

// 配置版本
#define CONFIG_VERSION              1

// 配置类型定义已移至smartjet_common.h中以避免重复定义

// ================================================================================
// 函数声明
// ================================================================================

/**
 * @brief 初始化配置管理器
 * @return 初始化结果
 */
esp_err_t config_manager_init(void);

/**
 * @brief 清理配置管理器
 */
void config_manager_cleanup(void);

/**
 * @brief 加载默认配置
 * @return 加载结果
 */
esp_err_t config_manager_load_defaults(void);

/**
 * @brief 保存当前配置
 * @return 保存结果
 */
esp_err_t config_manager_save_all(void);

/**
 * @brief 重置所有配置为默认值
 * @return 重置结果
 */
esp_err_t config_manager_factory_reset(void);

// ================================================================================
// 设备配置相关函数
// ================================================================================

/**
 * @brief 加载设备配置
 * @param[out] config 设备配置
 * @return 加载结果
 */
esp_err_t config_manager_load_device_config(device_config_t* config);

/**
 * @brief 保存设备配置
 * @param config 设备配置
 * @return 保存结果
 */
esp_err_t config_manager_save_device_config(const device_config_t* config);

/**
 * @brief 检查设备配置是否有效
 * @param config 设备配置
 * @return true: 有效, false: 无效
 */
bool config_manager_validate_device_config(const device_config_t* config);

// ================================================================================
// WiFi配置相关函数
// ================================================================================

/**
 * @brief 加载WiFi配置
 * @param[out] config WiFi配置
 * @return 加载结果
 */
esp_err_t config_manager_load_wifi_config(smartjet_wifi_config_t* config);

/**
 * @brief 保存WiFi配置
 * @param config WiFi配置
 * @return 保存结果
 */
esp_err_t config_manager_save_wifi_config(const smartjet_wifi_config_t* config);

/**
 * @brief 清除WiFi配置
 * @return 清除结果
 */
esp_err_t config_manager_clear_wifi_config(void);

// ================================================================================
// 系统配置相关函数
// ================================================================================

/**
 * @brief 加载系统配置
 * @param[out] config 系统配置
 * @return 加载结果
 */
esp_err_t config_manager_load_system_config(sys_config_t* config);

/**
 * @brief 保存系统配置
 * @param config 系统配置
 * @return 保存结果
 */
esp_err_t config_manager_save_system_config(const sys_config_t* config);

/**
 * @brief 更新单个系统配置参数
 * @param key 配置键名
 * @param value 配置值
 * @return 更新结果
 */
esp_err_t config_manager_update_system_param(const char* key, const void* value, size_t size);

// ================================================================================
// 统计数据相关函数
// ================================================================================

/**
 * @brief 加载统计数据
 * @param[out] stats 统计数据
 * @return 加载结果
 */
esp_err_t config_manager_load_stats(system_stats_t* stats);

/**
 * @brief 保存统计数据
 * @param stats 统计数据
 * @return 保存结果
 */
esp_err_t config_manager_save_stats(const system_stats_t* stats);

/**
 * @brief 重置统计数据
 * @return 重置结果
 */
esp_err_t config_manager_reset_stats(void);

/**
 * @brief 备份统计数据
 * @return 备份结果
 */
esp_err_t config_manager_backup_stats(void);

/**
 * @brief 从备份恢复统计数据
 * @return 恢复结果
 */
esp_err_t config_manager_restore_stats_from_backup(void);

// ================================================================================
// 配置热更新相关函数
// ================================================================================

/**
 * @brief 注册配置变更回调
 * @param key 配置键名
 * @param callback 回调函数
 * @return 注册结果
 */
esp_err_t config_manager_register_callback(const char* key, void (*callback)(const char* key, const void* value));

/**
 * @brief 注销配置变更回调
 * @param key 配置键名
 * @return 注销结果
 */
esp_err_t config_manager_unregister_callback(const char* key);

/**
 * @brief 触发配置热更新
 * @param key 配置键名
 * @param value 新的配置值
 * @param size 值的大小
 * @return 更新结果
 */
esp_err_t config_manager_hot_update(const char* key, const void* value, size_t size);

// ================================================================================
// 配置导入导出函数
// ================================================================================

/**
 * @brief 导出配置到JSON格式
 * @param[out] json_str JSON字符串（需要调用者释放）
 * @return 导出结果
 */
esp_err_t config_manager_export_to_json(char** json_str);

/**
 * @brief 从JSON格式导入配置
 * @param json_str JSON字符串
 * @return 导入结果
 */
esp_err_t config_manager_import_from_json(const char* json_str);

/**
 * @brief 获取配置信息摘要
 * @param[out] info_str 信息字符串（需要调用者释放）
 * @return 获取结果
 */
esp_err_t config_manager_get_info_summary(char** info_str);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_MGR_H 