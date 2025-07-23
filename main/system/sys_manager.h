/**
 * @file sys_manager.h
 * @brief 系统管理模块头文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 系统管理模块负责系统状态管理、看门狗控制、异常处理等核心功能
 */

#ifndef SYS_MANAGER_H
#define SYS_MANAGER_H

#include "include/smartjet_common.h"
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================================
// 宏定义
// ================================================================================
#define WDT_TIMEOUT_SECONDS         8       // 看门狗超时时间(秒)
#define MEMORY_CHECK_INTERVAL_MS    5000    // 内存检查间隔(毫秒)
#define SAFETY_CHECK_INTERVAL_MS    1000    // 安全检查间隔(毫秒)

// 内存阈值定义
#define MEMORY_WARNING_THRESHOLD    25600   // 内存警告阈值(25KB)
#define MEMORY_CRITICAL_THRESHOLD   15360   // 内存危险阈值(15KB)
#define MEMORY_EMERGENCY_THRESHOLD  8192    // 内存紧急阈值(8KB)

// 栈阈值定义
#define STACK_WARNING_THRESHOLD     2048    // 栈警告阈值
#define STACK_CRITICAL_THRESHOLD    1024    // 栈危险阈值

// ================================================================================
// 类型定义
// ================================================================================

/**
 * @brief 系统健康状态
 */
typedef enum {
    SYSTEM_HEALTH_GOOD,         // 系统健康
    SYSTEM_HEALTH_WARNING,      // 系统警告
    SYSTEM_HEALTH_CRITICAL,     // 系统危险
    SYSTEM_HEALTH_EMERGENCY     // 系统紧急
} system_health_t;

/**
 * @brief 内存状态
 */
typedef struct {
    uint32_t free_heap;         // 可用堆内存
    uint32_t min_free_heap;     // 最小可用堆内存
    uint32_t largest_block;     // 最大连续内存块
    system_health_t health;     // 内存健康状态
} memory_status_t;

/**
 * @brief 任务监控信息
 */
typedef struct {
    TaskHandle_t handle;        // 任务句柄
    char name[16];              // 任务名称
    uint32_t stack_remaining;   // 剩余栈空间
    uint32_t runtime;           // 运行时间
    bool is_alive;              // 任务是否存活
    uint32_t last_heartbeat;    // 最后心跳时间
} task_monitor_t;

// ================================================================================
// 函数声明
// ================================================================================

/**
 * @brief 初始化系统管理器
 * @return 初始化结果
 */
esp_err_t sys_manager_init(void);

/**
 * @brief 清理系统管理器
 */
void sys_manager_cleanup(void);

/**
 * @brief 更新系统状态
 * @param new_state 新的系统状态
 * @return 更新结果
 */
esp_err_t sys_manager_set_state(sys_state_t new_state);

/**
 * @brief 获取当前系统状态
 * @return 当前系统状态
 */
sys_state_t sys_manager_get_state(void);

/**
 * @brief 获取系统健康状态
 * @return 系统健康状态
 */
system_health_t sys_manager_get_health(void);

/**
 * @brief 获取内存状态
 * @param[out] status 内存状态信息
 * @return 操作结果
 */
esp_err_t sys_manager_get_memory_status(memory_status_t* status);

/**
 * @brief 喂看门狗
 * @param task_name 任务名称
 */
void sys_manager_feed_watchdog(const char* task_name);

/**
 * @brief 注册任务监控
 * @param task_handle 任务句柄
 * @param task_name 任务名称
 * @return 注册结果
 */
esp_err_t sys_manager_register_task(TaskHandle_t task_handle, const char* task_name);

/**
 * @brief 注销任务监控
 * @param task_handle 任务句柄
 * @return 注销结果
 */
esp_err_t sys_manager_unregister_task(TaskHandle_t task_handle);

/**
 * @brief 任务心跳
 * @param task_handle 任务句柄
 */
void sys_manager_task_heartbeat(TaskHandle_t task_handle);

/**
 * @brief 检查任务健康状态
 * @return 检查结果
 */
esp_err_t sys_manager_check_tasks_health(void);

/**
 * @brief 系统软重启
 * @param reason 重启原因
 */
void sys_manager_restart(const char* reason);

/**
 * @brief 进入低功耗模式
 * @return 操作结果
 */
esp_err_t sys_manager_enter_low_power(void);

/**
 * @brief 退出低功耗模式
 * @return 操作结果
 */
esp_err_t sys_manager_exit_low_power(void);

/**
 * @brief 获取重启原因字符串
 * @return 重启原因字符串
 */
const char* sys_manager_get_reset_reason_string(void);

/**
 * @brief 获取系统运行时间(秒)
 * @return 系统运行时间
 */
uint32_t sys_manager_get_uptime(void);

/**
 * @brief 获取系统时间戳(毫秒)
 * @return 当前时间戳
 */
uint64_t sys_manager_get_timestamp_ms(void);

/**
 * @brief 安全延时函数（带看门狗喂养）
 * @param ms 延时时间(毫秒)
 */
void sys_manager_safe_delay(uint32_t ms);

/**
 * @brief 执行内存清理
 */
void sys_manager_cleanup_memory(void);

/**
 * @brief 检查系统是否可以进行OTA升级
 * @return true: 可以升级, false: 不可以升级
 */
bool sys_manager_can_perform_ota(void);

/**
 * @brief 设置系统为OTA升级模式
 * @return 操作结果
 */
esp_err_t sys_manager_enter_ota_mode(void);

/**
 * @brief 退出OTA升级模式
 * @return 操作结果
 */
esp_err_t sys_manager_exit_ota_mode(void);

/**
 * @brief 系统自检
 * @return 自检结果
 */
esp_err_t sys_manager_self_check(void);

#ifdef __cplusplus
}
#endif

#endif // SYS_MANAGER_H 