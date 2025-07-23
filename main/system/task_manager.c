/**
 * @file task_manager.c
 * @brief 任务管理器实现
 * @author SmartJet Team
 * @date 2024
 * 
 * 任务管理器负责创建和管理所有系统任务
 */

#include "task_manager.h"
#include "sys_manager.h"
#include "include/smartjet_common.h"

// 日志标签
static const char* TAG = "TASK_MANAGER";

// ================================================================================
// 任务函数声明
// ================================================================================
static void task_system_manager(void* pvParameters);
static void task_engine_control(void* pvParameters);
static void task_sensor_monitor(void* pvParameters);
static void task_cloud_comm(void* pvParameters);
static void task_display_ui(void* pvParameters);
static void task_battery_monitor(void* pvParameters);
static void task_ota_manager(void* pvParameters);

// ================================================================================
// 任务定义表
// ================================================================================
typedef struct {
    const char* name;
    TaskFunction_t function;
    uint32_t stack_size;
    UBaseType_t priority;
    BaseType_t core_id;
    TaskHandle_t* handle_ptr;
    bool enabled;
} task_def_t;

// 任务句柄
static TaskHandle_t h_task_system = NULL;
static TaskHandle_t h_task_engine = NULL;
static TaskHandle_t h_task_sensor = NULL;
static TaskHandle_t h_task_cloud = NULL;
static TaskHandle_t h_task_display = NULL;
static TaskHandle_t h_task_battery = NULL;
static TaskHandle_t h_task_ota = NULL;

// 任务定义表
static const task_def_t task_definitions[] = {
    {
        .name = "system_mgr",
        .function = task_system_manager,
        .stack_size = TASK_STACK_SIZE_SYSTEM,
        .priority = TASK_PRIORITY_SYSTEM,
        .core_id = 0,  // Core 0 (Protocol CPU)
        .handle_ptr = &h_task_system,
        .enabled = true
    },
    {
        .name = "engine_ctrl",
        .function = task_engine_control,
        .stack_size = TASK_STACK_SIZE_ENGINE,
        .priority = TASK_PRIORITY_ENGINE,
        .core_id = 0,  // Core 0 (Protocol CPU)
        .handle_ptr = &h_task_engine,
        .enabled = true
    },
    {
        .name = "sensor_mon",
        .function = task_sensor_monitor,
        .stack_size = TASK_STACK_SIZE_SENSOR,
        .priority = TASK_PRIORITY_SENSOR,
        .core_id = 0,  // Core 0 (Protocol CPU)
        .handle_ptr = &h_task_sensor,
        .enabled = true
    },
    {
        .name = "cloud_comm",
        .function = task_cloud_comm,
        .stack_size = TASK_STACK_SIZE_CLOUD,
        .priority = TASK_PRIORITY_CLOUD,
        .core_id = 0,  // Core 0 (Protocol CPU)
        .handle_ptr = &h_task_cloud,
        .enabled = true
    },
    {
        .name = "display_ui",
        .function = task_display_ui,
        .stack_size = TASK_STACK_SIZE_DISPLAY,
        .priority = TASK_PRIORITY_DISPLAY,
        .core_id = 1,  // Core 1 (Application CPU)
        .handle_ptr = &h_task_display,
        .enabled = true
    },
    {
        .name = "battery_mon",
        .function = task_battery_monitor,
        .stack_size = TASK_STACK_SIZE_BATTERY,
        .priority = TASK_PRIORITY_BATTERY,
        .core_id = 1,  // Core 1 (Application CPU)
        .handle_ptr = &h_task_battery,
        .enabled = true
    },
    {
        .name = "ota_mgr",
        .function = task_ota_manager,
        .stack_size = TASK_STACK_SIZE_OTA,
        .priority = TASK_PRIORITY_OTA,
        .core_id = 1,  // Core 1 (Application CPU)
        .handle_ptr = &h_task_ota,
        .enabled = true
    }
};

static const uint8_t TASK_COUNT = sizeof(task_definitions) / sizeof(task_definitions[0]);

// 任务管理器状态
static struct {
    bool initialized;
    bool all_started;
    uint32_t start_time;
} task_mgr = {0};

// ================================================================================
// 任务函数实现
// ================================================================================

/**
 * @brief 系统管理任务
 */
static void task_system_manager(void* pvParameters)
{
    ESP_LOGI(TAG, "系统管理任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "system_mgr");
    
    const TickType_t task_period = pdMS_TO_TICKS(1000);  // 1秒周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // 更新系统运行时间
        g_smartjet.system.uptime_seconds = sys_manager_get_uptime();
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "系统管理任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "系统管理任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 发动机控制任务
 */
static void task_engine_control(void* pvParameters)
{
    ESP_LOGI(TAG, "发动机控制任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "engine_ctrl");
    
    const TickType_t task_period = pdMS_TO_TICKS(100);  // 100ms周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现发动机控制逻辑
        // - 处理启动/停止命令
        // - 监控发动机状态
        // - 控制风门步进电机
        // - 安全保护逻辑
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "发动机控制任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "发动机控制任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 传感器监控任务
 */
static void task_sensor_monitor(void* pvParameters)
{
    ESP_LOGI(TAG, "传感器监控任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "sensor_mon");
    
    const TickType_t task_period = pdMS_TO_TICKS(500);  // 500ms周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现传感器监控逻辑
        // - 读取RPM传感器
        // - 检测机油压力
        // - 监控温度传感器
        // - 检测RF遥控信号
        // - 更新传感器数据
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "传感器监控任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "传感器监控任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 云通信任务
 */
static void task_cloud_comm(void* pvParameters)
{
    ESP_LOGI(TAG, "云通信任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "cloud_comm");
    
    const TickType_t task_period = pdMS_TO_TICKS(100);  // 100ms周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现云通信逻辑
        // - WiFi连接管理
        // - MQTT客户端维护
        // - 数据上报处理
        // - 云端命令处理
        // - 网络状态监控
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "云通信任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "云通信任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 显示UI任务
 */
static void task_display_ui(void* pvParameters)
{
    ESP_LOGI(TAG, "显示UI任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "display_ui");
    
    const TickType_t task_period = pdMS_TO_TICKS(50);  // 50ms周期 (20FPS)
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现显示UI逻辑
        // - 更新LCD显示内容
        // - 处理按键输入
        // - 管理LED状态指示
        // - 控制背光和节能
        // - 页面切换逻辑
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "显示UI任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "显示UI任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 电池监控任务
 */
static void task_battery_monitor(void* pvParameters)
{
    ESP_LOGI(TAG, "电池监控任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "battery_mon");
    
    const TickType_t task_period = pdMS_TO_TICKS(1000);  // 1秒周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现电池监控逻辑
        // - 与BQ40Z50通信
        // - 读取电池电压、电流、SOC
        // - 监控电池温度
        // - 检测充电状态
        // - 电池保护逻辑
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "电池监控任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "电池监控任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief OTA管理任务
 */
static void task_ota_manager(void* pvParameters)
{
    ESP_LOGI(TAG, "OTA管理任务启动");
    
    // 注册任务到监控
    sys_manager_register_task(xTaskGetCurrentTaskHandle(), "ota_mgr");
    
    const TickType_t task_period = pdMS_TO_TICKS(5000);  // 5秒周期
    TickType_t last_wake_time = xTaskGetTickCount();
    
    while (1) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // TODO: 实现OTA管理逻辑
        // - 定期检查固件更新
        // - 处理OTA升级流程
        // - 固件下载和验证
        // - 升级状态报告
        // - 回滚机制
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "OTA管理任务收到关闭请求");
            break;
        }
        
        // 等待下一个周期
        vTaskDelayUntil(&last_wake_time, task_period);
    }
    
    // 注销任务监控
    sys_manager_unregister_task(xTaskGetCurrentTaskHandle());
    ESP_LOGI(TAG, "OTA管理任务退出");
    vTaskDelete(NULL);
}

// ================================================================================
// 公共函数实现
// ================================================================================

esp_err_t task_manager_start(void)
{
    if (task_mgr.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "启动任务管理器...");
    
    uint32_t successful_tasks = 0;
    
    // 创建所有任务
    for (int i = 0; i < TASK_COUNT; i++) {
        const task_def_t* task_def = &task_definitions[i];
        
        if (!task_def->enabled) {
            ESP_LOGW(TAG, "跳过已禁用的任务: %s", task_def->name);
            continue;
        }
        
        BaseType_t result = xTaskCreatePinnedToCore(
            task_def->function,
            task_def->name,
            task_def->stack_size,
            NULL,
            task_def->priority,
            task_def->handle_ptr,
            task_def->core_id
        );
        
        if (result == pdPASS) {
            ESP_LOGI(TAG, "任务创建成功: %s (优先级:%d, 核心:%d, 栈:%d)", 
                     task_def->name, task_def->priority, 
                     task_def->core_id, task_def->stack_size);
            successful_tasks++;
        } else {
            ESP_LOGE(TAG, "任务创建失败: %s", task_def->name);
            
            // 清理已创建的任务
            for (int j = 0; j <= i; j++) {
                if (task_definitions[j].enabled && *(task_definitions[j].handle_ptr)) {
                    vTaskDelete(*(task_definitions[j].handle_ptr));
                    *(task_definitions[j].handle_ptr) = NULL;
                }
            }
            return ESP_FAIL;
        }
    }
    
    task_mgr.initialized = true;
    task_mgr.all_started = true;
    task_mgr.start_time = sys_manager_get_uptime();
    
    ESP_LOGI(TAG, "任务管理器启动完成，成功创建 %d 个任务", successful_tasks);
    return ESP_OK;
}

esp_err_t task_manager_stop(void)
{
    if (!task_mgr.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "停止任务管理器...");
    
    // 设置关闭标志
    g_smartjet.shutdown_requested = true;
    
    // 等待任务自然退出
    sys_manager_safe_delay(2000);
    
    // 强制删除剩余任务
    for (int i = 0; i < TASK_COUNT; i++) {
        TaskHandle_t* handle = task_definitions[i].handle_ptr;
        if (handle && *handle) {
            ESP_LOGW(TAG, "强制删除任务: %s", task_definitions[i].name);
            vTaskDelete(*handle);
            *handle = NULL;
        }
    }
    
    task_mgr.initialized = false;
    task_mgr.all_started = false;
    
    ESP_LOGI(TAG, "任务管理器停止完成");
    return ESP_OK;
}

esp_err_t task_manager_get_task_info(const char* task_name, task_info_t* info)
{
    if (!task_name || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < TASK_COUNT; i++) {
        if (strcmp(task_definitions[i].name, task_name) == 0) {
            TaskHandle_t handle = *(task_definitions[i].handle_ptr);
            
            info->handle = handle;
            info->name = task_definitions[i].name;
            info->stack_size = task_definitions[i].stack_size;
            info->priority = task_definitions[i].priority;
            info->core_id = task_definitions[i].core_id;
            info->creation_time = task_mgr.start_time;
            info->last_run_time = xTaskGetTickCount();
            
            if (handle) {
                eTaskState task_state = eTaskGetState(handle);
                switch (task_state) {
                    case eRunning:
                    case eReady:
                        info->state = TASK_STATE_RUNNING;
                        break;
                    case eBlocked:
                    case eSuspended:
                        info->state = TASK_STATE_STOPPED;
                        break;
                    case eDeleted:
                        info->state = TASK_STATE_ERROR;
                        break;
                    default:
                        info->state = TASK_STATE_ERROR;
                        break;
                }
            } else {
                info->state = TASK_STATE_STOPPED;
            }
            
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t task_manager_get_all_tasks_status(void)
{
    ESP_LOGI(TAG, "========== 任务状态报告 ==========");
    ESP_LOGI(TAG, "任务总数: %d", TASK_COUNT);
    ESP_LOGI(TAG, "运行时间: %d 秒", sys_manager_get_uptime() - task_mgr.start_time);
    ESP_LOGI(TAG, "可用堆内存: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "=====================================");
    
    for (int i = 0; i < TASK_COUNT; i++) {
        const task_def_t* task_def = &task_definitions[i];
        TaskHandle_t handle = *(task_def->handle_ptr);
        
        if (handle) {
            eTaskState state = eTaskGetState(handle);
            uint32_t stack_remaining = uxTaskGetStackHighWaterMark(handle);
            
            const char* state_str;
            switch (state) {
                case eRunning:   state_str = "运行中"; break;
                case eReady:     state_str = "就绪"; break;
                case eBlocked:   state_str = "阻塞"; break;
                case eSuspended: state_str = "挂起"; break;
                case eDeleted:   state_str = "已删除"; break;
                default:         state_str = "未知"; break;
            }
            
            ESP_LOGI(TAG, "任务: %-12s | 状态: %-6s | 栈剩余: %4d | 核心: %d", 
                     task_def->name, state_str, stack_remaining, task_def->core_id);
        } else {
            ESP_LOGI(TAG, "任务: %-12s | 状态: 未创建", task_def->name);
        }
    }
    
    ESP_LOGI(TAG, "=====================================");
    return ESP_OK;
}

esp_err_t task_manager_restart_task(const char* task_name)
{
    // TODO: 实现任务重启逻辑
    ESP_LOGW(TAG, "任务重启功能暂未实现: %s", task_name);
    return ESP_ERR_NOT_SUPPORTED;
}

bool task_manager_all_tasks_healthy(void)
{
    if (!task_mgr.initialized || !task_mgr.all_started) {
        return false;
    }
    
    for (int i = 0; i < TASK_COUNT; i++) {
        TaskHandle_t handle = *(task_definitions[i].handle_ptr);
        if (task_definitions[i].enabled && !handle) {
            return false;
        }
        
        if (handle && eTaskGetState(handle) == eDeleted) {
            return false;
        }
    }
    
    return true;
} 