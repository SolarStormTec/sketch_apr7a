/**
 * @file task_manager.h
 * @brief 任务管理器头文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 任务管理器负责创建和管理所有系统任务
 */

#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include "include/smartjet_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================================
// 任务状态定义
// ================================================================================
typedef enum {
    TASK_STATE_STOPPED,     // 任务已停止
    TASK_STATE_STARTING,    // 任务启动中
    TASK_STATE_RUNNING,     // 任务运行中
    TASK_STATE_STOPPING,    // 任务停止中
    TASK_STATE_ERROR        // 任务错误
} task_state_t;

/**
 * @brief 任务信息结构
 */
typedef struct {
    TaskHandle_t handle;        // 任务句柄
    const char* name;           // 任务名称
    task_state_t state;         // 任务状态
    uint32_t stack_size;        // 栈大小
    UBaseType_t priority;       // 优先级
    BaseType_t core_id;         // 绑定的CPU核心
    uint32_t creation_time;     // 创建时间
    uint32_t last_run_time;     // 最后运行时间
} task_info_t;

// ================================================================================
// 函数声明
// ================================================================================

/**
 * @brief 启动任务管理器
 * @return 启动结果
 */
esp_err_t task_manager_start(void);

/**
 * @brief 停止任务管理器
 * @return 停止结果
 */
esp_err_t task_manager_stop(void);

/**
 * @brief 获取任务信息
 * @param task_name 任务名称
 * @param[out] info 任务信息
 * @return 操作结果
 */
esp_err_t task_manager_get_task_info(const char* task_name, task_info_t* info);

/**
 * @brief 获取所有任务状态
 * @return 任务状态列表
 */
esp_err_t task_manager_get_all_tasks_status(void);

/**
 * @brief 重启指定任务
 * @param task_name 任务名称
 * @return 重启结果
 */
esp_err_t task_manager_restart_task(const char* task_name);

/**
 * @brief 检查所有任务是否正常运行
 * @return true: 所有任务正常, false: 有任务异常
 */
bool task_manager_all_tasks_healthy(void);

#ifdef __cplusplus
}
#endif

#endif // TASK_MANAGER_H 