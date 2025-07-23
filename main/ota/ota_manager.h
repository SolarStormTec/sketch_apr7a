/**
 * @file ota_manager.h
 * @brief OTA升级管理器 - 基于成熟ino代码重构
 * @author SmartJet Team
 * @date 2024
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdbool.h>
#include "smartjet_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// OTA类型定义已移至 smartjet_common.h 避免重复定义

// OTA进度回调函数类型
typedef void (*ota_progress_callback_t)(uint8_t progress, uint32_t bytes_downloaded, uint32_t total_size);

// OTA状态变化回调函数类型
typedef void (*ota_status_callback_t)(ota_state_t old_state, ota_state_t new_state, esp_err_t error);

// OTA管理器结构（内部使用）
typedef struct {
    ota_config_t config;           // 配置信息
    ota_status_t status;           // 状态信息
    
    // 回调函数
    ota_progress_callback_t progress_callback;
    ota_status_callback_t status_callback;
    
    // 内部状态
    bool initialized;              // 初始化标志
    esp_timer_handle_t check_timer; // 检查定时器
    
    // 同步原语
    void *mutex;                   // 互斥锁
    
    // 缓冲区
    char firmware_url[256];        // 固件URL
    char md5_hash[64];             // MD5哈希
} ota_manager_t;

/**
 * @brief 初始化OTA管理器
 * 
 * @param config OTA配置
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 初始化失败
 */
esp_err_t ota_manager_init(const ota_config_t *config);

/**
 * @brief 反初始化OTA管理器
 */
void ota_manager_deinit(void);

/**
 * @brief 检查更新
 * 
 * @return esp_err_t 
 *         - ESP_OK: 检查成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 检查失败
 */
esp_err_t ota_manager_check_update(void);

/**
 * @brief 强制启动OTA更新（忽略自动更新设置）
 * 
 * @return esp_err_t 
 *         - ESP_OK: 启动成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_FAIL: 启动失败
 */
esp_err_t ota_manager_force_update(void);

/**
 * @brief 取消当前OTA操作
 * 
 * @return esp_err_t 
 *         - ESP_OK: 取消成功
 *         - ESP_ERR_INVALID_STATE: 无操作可取消
 */
esp_err_t ota_manager_cancel_update(void);

/**
 * @brief 获取OTA状态
 * 
 * @return ota_status_t 当前状态信息
 */
ota_status_t ota_manager_get_status(void);

/**
 * @brief 设置自动更新使能
 * 
 * @param enabled 是否启用自动更新
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_set_auto_update(bool enabled);

/**
 * @brief 获取当前固件版本
 * 
 * @return int 当前版本号
 */
int ota_manager_get_current_version(void);

/**
 * @brief 设置版本号（用于手动同步）
 * 
 * @param version 版本号
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_ARG: 版本号无效
 *         - ESP_ERR_INVALID_STATE: 状态无效
 */
esp_err_t ota_manager_set_version(int version);

/**
 * @brief 设置进度回调函数
 * 
 * @param callback 回调函数指针
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_set_progress_callback(ota_progress_callback_t callback);

/**
 * @brief 设置状态变化回调函数
 * 
 * @param callback 回调函数指针
 * @return esp_err_t 
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_set_status_callback(ota_status_callback_t callback);

/**
 * @brief 获取OTA配置
 * 
 * @param config 配置信息输出缓冲区
 * @return esp_err_t 
 *         - ESP_OK: 获取成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_get_config(ota_config_t *config);

/**
 * @brief 更新OTA配置
 * 
 * @param config 新的配置信息
 * @return esp_err_t 
 *         - ESP_OK: 更新成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_update_config(const ota_config_t *config);

/**
 * @brief 检查是否正在进行OTA
 * 
 * @return true 正在进行OTA
 * @return false 未进行OTA
 */
bool ota_manager_is_updating(void);

/**
 * @brief 获取上次检查时间
 * 
 * @return uint64_t 时间戳（微秒）
 */
uint64_t ota_manager_get_last_check_time(void);

/**
 * @brief 重置OTA统计信息
 * 
 * @return esp_err_t 
 *         - ESP_OK: 重置成功
 *         - ESP_ERR_INVALID_STATE: 管理器未初始化
 */
esp_err_t ota_manager_reset_statistics(void);

/**
 * @brief 获取默认OTA配置
 * 
 * @param config 配置输出缓冲区
 */
void ota_manager_get_default_config(ota_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // OTA_MANAGER_H 