/**
 * @file event_system.h
 * @brief 事件系统头文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 事件系统负责系统内部事件的发布、订阅和处理
 */

#ifndef EVENT_SYSTEM_H
#define EVENT_SYSTEM_H

#include "include/smartjet_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// ================================================================================
// 事件系统配置
// ================================================================================
#define EVENT_QUEUE_SIZE            32      // 事件队列大小
#define MAX_EVENT_HANDLERS          16      // 最大事件处理器数量
#define EVENT_HANDLER_TASK_STACK    2048    // 事件处理任务栈大小
#define EVENT_HANDLER_TASK_PRIORITY 15      // 事件处理任务优先级

// ================================================================================
// 事件处理器类型定义
// ================================================================================

/**
 * @brief 事件处理器函数类型
 * @param event 事件消息
 */
typedef void (*event_handler_t)(const event_message_t* event);

/**
 * @brief 事件处理器注册信息
 */
typedef struct {
    system_event_t event_type;      // 事件类型
    event_handler_t handler;        // 处理器函数
    const char* name;               // 处理器名称
    bool enabled;                   // 是否启用
    uint32_t call_count;            // 调用次数
    uint32_t last_call_time;        // 最后调用时间
} event_handler_info_t;

// ================================================================================
// 函数声明
// ================================================================================

/**
 * @brief 初始化事件系统
 * @return 初始化结果
 */
esp_err_t event_system_init(void);

/**
 * @brief 清理事件系统
 */
void event_system_cleanup(void);

/**
 * @brief 发送事件
 * @param event 事件消息
 * @return 发送结果
 */
esp_err_t event_system_post(const event_message_t* event);

/**
 * @brief 发送事件（非阻塞）
 * @param event 事件消息
 * @return 发送结果
 */
esp_err_t event_system_post_from_isr(const event_message_t* event);

/**
 * @brief 注册事件处理器
 * @param event_type 事件类型
 * @param handler 处理器函数
 * @param name 处理器名称
 * @return 注册结果
 */
esp_err_t event_system_register_handler(system_event_t event_type, 
                                       event_handler_t handler, 
                                       const char* name);

/**
 * @brief 注销事件处理器
 * @param event_type 事件类型
 * @param handler 处理器函数
 * @return 注销结果
 */
esp_err_t event_system_unregister_handler(system_event_t event_type, 
                                         event_handler_t handler);

/**
 * @brief 启用/禁用事件处理器
 * @param event_type 事件类型
 * @param handler 处理器函数
 * @param enabled 是否启用
 * @return 操作结果
 */
esp_err_t event_system_enable_handler(system_event_t event_type, 
                                     event_handler_t handler, 
                                     bool enabled);

/**
 * @brief 获取事件处理器统计信息
 * @param[out] count 处理器数量
 * @return 操作结果
 */
esp_err_t event_system_get_handler_stats(uint8_t* count);

/**
 * @brief 打印事件系统状态
 */
void event_system_print_status(void);

/**
 * @brief 创建简单事件
 * @param type 事件类型
 * @param data 事件数据（可选）
 * @param data_len 数据长度
 * @return 事件消息
 */
event_message_t event_system_create_event(system_event_t type, 
                                         const void* data, 
                                         size_t data_len);

/**
 * @brief 获取事件类型名称
 * @param event_type 事件类型
 * @return 事件名称字符串
 */
const char* event_system_get_event_name(system_event_t event_type);

#ifdef __cplusplus
}
#endif

#endif // EVENT_SYSTEM_H 