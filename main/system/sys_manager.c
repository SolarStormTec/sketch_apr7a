/**
 * @file sys_manager.c
 * @brief 系统管理模块实现
 * @author SmartJet Team
 * @date 2024
 * 
 * 系统管理模块负责系统状态管理、看门狗控制、异常处理等核心功能
 */

#include "sys_manager.h"
#include "include/smartjet_common.h"
#include "utils/event_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

// 日志标签
static const char* TAG = "SYS_MANAGER";

// ================================================================================
// 内部数据结构
// ================================================================================

// 任务监控数组
#define MAX_MONITORED_TASKS     16
static task_monitor_t monitored_tasks[MAX_MONITORED_TASKS];
static uint8_t task_count = 0;
static SemaphoreHandle_t task_monitor_mutex = NULL;

// 系统管理器状态
static struct {
    bool initialized;
    esp_timer_handle_t memory_check_timer;
    esp_timer_handle_t safety_check_timer;
    system_health_t health_status;
    uint32_t memory_warning_count;
    uint32_t last_cleanup_time;
    bool low_power_mode;
} sys_mgr = {0};

// ================================================================================
// 内部函数声明
// ================================================================================
static void memory_check_callback(void* arg);
static void safety_check_callback(void* arg);
static esp_err_t init_watchdog(void);
static esp_err_t init_timers(void);
static void cleanup_timers(void);
static system_health_t evaluate_system_health(void);

// ================================================================================
// 重启原因字符串映射
// ================================================================================
static const char* reset_reason_strings[] = {
    [ESP_RST_UNKNOWN]    = "未知原因",
    [ESP_RST_POWERON]    = "上电重启",
    [ESP_RST_EXT]        = "外部重启",
    [ESP_RST_SW]         = "软件重启",
    [ESP_RST_PANIC]      = "系统崩溃",
    [ESP_RST_INT_WDT]    = "中断看门狗超时",
    [ESP_RST_TASK_WDT]   = "任务看门狗超时",
    [ESP_RST_WDT]        = "其他看门狗超时",
    [ESP_RST_DEEPSLEEP]  = "深度睡眠唤醒",
    [ESP_RST_BROWNOUT]   = "欠压重启",
    [ESP_RST_SDIO]       = "SDIO重启"
};

// ================================================================================
// 定时器回调函数
// ================================================================================

/**
 * @brief 内存检查定时器回调
 */
static void memory_check_callback(void* arg)
{
    memory_status_t mem_status;
    
    // 获取内存状态
    mem_status.free_heap = esp_get_free_heap_size();
    mem_status.min_free_heap = esp_get_minimum_free_heap_size();
    mem_status.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    
    // 评估内存健康状态
    if (mem_status.free_heap < MEMORY_EMERGENCY_THRESHOLD) {
        mem_status.health = SYSTEM_HEALTH_EMERGENCY;
        ESP_LOGE(TAG, "内存紧急状态: %d bytes", mem_status.free_heap);
        
        // 立即执行内存清理
        sys_manager_cleanup_memory();
        
        // 如果清理后仍不足，重启系统
        if (esp_get_free_heap_size() < MEMORY_EMERGENCY_THRESHOLD) {
            ESP_LOGE(TAG, "内存严重不足，系统重启");
            sys_manager_restart("内存不足");
        }
    } else if (mem_status.free_heap < MEMORY_CRITICAL_THRESHOLD) {
        mem_status.health = SYSTEM_HEALTH_CRITICAL;
        ESP_LOGW(TAG, "内存危险状态: %d bytes", mem_status.free_heap);
        sys_manager_cleanup_memory();
    } else if (mem_status.free_heap < MEMORY_WARNING_THRESHOLD) {
        mem_status.health = SYSTEM_HEALTH_WARNING;
        sys_mgr.memory_warning_count++;
        ESP_LOGW(TAG, "内存警告状态: %d bytes (警告次数: %d)", 
                 mem_status.free_heap, sys_mgr.memory_warning_count);
        
        // 发送内存警告事件
        if (sys_mgr.memory_warning_count % 10 == 1) {  // 每10次警告发送一次事件
            event_message_t event = {
                .type = EVENT_MEMORY_WARNING,
                .data = &mem_status,
                .data_len = sizeof(mem_status),
                .timestamp = sys_manager_get_timestamp_ms()
            };
            event_system_post(&event);
        }
    } else {
        mem_status.health = SYSTEM_HEALTH_GOOD;
    }
    
    // 更新全局状态
    if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_smartjet.system.free_heap = mem_status.free_heap;
        g_smartjet.system.min_free_heap = mem_status.min_free_heap;
        xSemaphoreGive(g_smartjet.system_mutex);
    }
}

/**
 * @brief 安全检查定时器回调
 */
static void safety_check_callback(void* arg)
{
    // 检查任务健康状态
    sys_manager_check_tasks_health();
    
    // 评估系统整体健康状态
    sys_mgr.health_status = evaluate_system_health();
    
    // 检查系统状态是否异常
    if (sys_mgr.health_status == SYSTEM_HEALTH_EMERGENCY) {
        ESP_LOGE(TAG, "系统健康状态紧急，可能需要重启");
        
        // 发送系统错误事件
        event_message_t event = {
            .type = EVENT_SYSTEM_ERROR,
            .data = &sys_mgr.health_status,
            .data_len = sizeof(sys_mgr.health_status),
            .timestamp = sys_manager_get_timestamp_ms()
        };
        event_system_post(&event);
    }
}

// ================================================================================
// 公共函数实现
// ================================================================================

esp_err_t sys_manager_init(void)
{
    esp_err_t ret = ESP_OK;
    
    if (sys_mgr.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化系统管理器...");
    
    // 创建任务监控互斥锁
    task_monitor_mutex = xSemaphoreCreateMutex();
    if (!task_monitor_mutex) {
        ESP_LOGE(TAG, "创建任务监控互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化看门狗
    ret = init_watchdog();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "看门狗初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化定时器
    ret = init_timers();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "定时器初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化监控数组
    memset(monitored_tasks, 0, sizeof(monitored_tasks));
    task_count = 0;
    
    // 初始化系统状态
    sys_mgr.health_status = SYSTEM_HEALTH_GOOD;
    sys_mgr.memory_warning_count = 0;
    sys_mgr.last_cleanup_time = 0;
    sys_mgr.low_power_mode = false;
    sys_mgr.initialized = true;
    
    ESP_LOGI(TAG, "系统管理器初始化完成");
    return ESP_OK;
    
cleanup:
    if (task_monitor_mutex) {
        vSemaphoreDelete(task_monitor_mutex);
        task_monitor_mutex = NULL;
    }
    cleanup_timers();
    return ret;
}

void sys_manager_cleanup(void)
{
    if (!sys_mgr.initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "清理系统管理器...");
    
    // 停止定时器
    cleanup_timers();
    
    // 删除任务监控互斥锁
    if (task_monitor_mutex) {
        vSemaphoreDelete(task_monitor_mutex);
        task_monitor_mutex = NULL;
    }
    
    // 重置状态
    memset(&sys_mgr, 0, sizeof(sys_mgr));
    
    ESP_LOGI(TAG, "系统管理器清理完成");
}

esp_err_t sys_manager_set_state(sys_state_t new_state)
{
    if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    sys_state_t old_state = g_smartjet.system.current_state;
    
    if (old_state != new_state) {
        g_smartjet.system.previous_state = old_state;
        g_smartjet.system.current_state = new_state;
        g_smartjet.system.state_enter_time = sys_manager_get_timestamp_ms();
        
        ESP_LOGI(TAG, "系统状态变更: %d -> %d", old_state, new_state);
    }
    
    xSemaphoreGive(g_smartjet.system_mutex);
    return ESP_OK;
}

sys_state_t sys_manager_get_state(void)
{
    sys_state_t state = SYS_STATE_BOOT;
    
    if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = g_smartjet.system.current_state;
        xSemaphoreGive(g_smartjet.system_mutex);
    }
    
    return state;
}

system_health_t sys_manager_get_health(void)
{
    return sys_mgr.health_status;
}

esp_err_t sys_manager_get_memory_status(memory_status_t* status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->free_heap = esp_get_free_heap_size();
    status->min_free_heap = esp_get_minimum_free_heap_size();
    status->largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    
    // 评估内存健康状态
    if (status->free_heap < MEMORY_EMERGENCY_THRESHOLD) {
        status->health = SYSTEM_HEALTH_EMERGENCY;
    } else if (status->free_heap < MEMORY_CRITICAL_THRESHOLD) {
        status->health = SYSTEM_HEALTH_CRITICAL;
    } else if (status->free_heap < MEMORY_WARNING_THRESHOLD) {
        status->health = SYSTEM_HEALTH_WARNING;
    } else {
        status->health = SYSTEM_HEALTH_GOOD;
    }
    
    return ESP_OK;
}

void sys_manager_feed_watchdog(const char* task_name)
{
    // 喂任务看门狗
    esp_task_wdt_reset();
    
    // 记录心跳时间
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    sys_manager_task_heartbeat(current_task);
}

esp_err_t sys_manager_register_task(TaskHandle_t task_handle, const char* task_name)
{
    if (!task_handle || !task_name) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(task_monitor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NO_MEM;
    
    if (task_count < MAX_MONITORED_TASKS) {
        monitored_tasks[task_count].handle = task_handle;
        strncpy(monitored_tasks[task_count].name, task_name, sizeof(monitored_tasks[task_count].name) - 1);
        monitored_tasks[task_count].name[sizeof(monitored_tasks[task_count].name) - 1] = '\0';
        monitored_tasks[task_count].is_alive = true;
        monitored_tasks[task_count].last_heartbeat = xTaskGetTickCount();
        task_count++;
        
        // 注册任务到看门狗
        esp_task_wdt_add(task_handle);
        
        ESP_LOGI(TAG, "注册任务监控: %s (总数: %d)", task_name, task_count);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "任务监控数组已满，无法注册任务: %s", task_name);
    }
    
    xSemaphoreGive(task_monitor_mutex);
    return ret;
}

esp_err_t sys_manager_unregister_task(TaskHandle_t task_handle)
{
    if (!task_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(task_monitor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < task_count; i++) {
        if (monitored_tasks[i].handle == task_handle) {
            ESP_LOGI(TAG, "注销任务监控: %s", monitored_tasks[i].name);
            
            // 从看门狗中移除任务
            esp_task_wdt_delete(task_handle);
            
            // 移动数组元素
            for (int j = i; j < task_count - 1; j++) {
                monitored_tasks[j] = monitored_tasks[j + 1];
            }
            task_count--;
            
            ret = ESP_OK;
            break;
        }
    }
    
    xSemaphoreGive(task_monitor_mutex);
    return ret;
}

void sys_manager_task_heartbeat(TaskHandle_t task_handle)
{
    if (!task_handle) {
        return;
    }
    
    if (xSemaphoreTake(task_monitor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        for (int i = 0; i < task_count; i++) {
            if (monitored_tasks[i].handle == task_handle) {
                monitored_tasks[i].last_heartbeat = xTaskGetTickCount();
                monitored_tasks[i].is_alive = true;
                break;
            }
        }
        xSemaphoreGive(task_monitor_mutex);
    }
}

esp_err_t sys_manager_check_tasks_health(void)
{
    if (xSemaphoreTake(task_monitor_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    TickType_t current_tick = xTaskGetTickCount();
    bool has_dead_task = false;
    
    for (int i = 0; i < task_count; i++) {
        task_monitor_t* task = &monitored_tasks[i];
        
        // 检查任务是否仍然存在
        if (eTaskGetState(task->handle) == eDeleted) {
            ESP_LOGW(TAG, "任务已删除: %s", task->name);
            task->is_alive = false;
            has_dead_task = true;
            continue;
        }
        
        // 检查心跳超时（30秒无心跳认为任务死锁）
        if (current_tick - task->last_heartbeat > pdMS_TO_TICKS(30000)) {
            ESP_LOGW(TAG, "任务心跳超时: %s", task->name);
            task->is_alive = false;
            has_dead_task = true;
        }
        
        // 检查栈使用情况
        task->stack_remaining = uxTaskGetStackHighWaterMark(task->handle);
        if (task->stack_remaining < STACK_CRITICAL_THRESHOLD) {
            ESP_LOGW(TAG, "任务栈空间不足: %s (%d bytes)", task->name, task->stack_remaining);
        }
    }
    
    xSemaphoreGive(task_monitor_mutex);
    
    // 如果有死亡任务，更新系统健康状态
    if (has_dead_task) {
        ESP_LOGW(TAG, "检测到异常任务，系统健康状态降级");
    }
    
    return ESP_OK;
}

void sys_manager_restart(const char* reason)
{
    ESP_LOGW(TAG, "系统重启: %s", reason ? reason : "未知原因");
    
    // 发送重启事件
    event_message_t event = {
        .type = EVENT_SYSTEM_ERROR,
        .data = (void*)reason,
        .data_len = reason ? strlen(reason) : 0,
        .timestamp = sys_manager_get_timestamp_ms()
    };
    event_system_post(&event);
    
    // 等待事件处理
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 执行重启
    esp_restart();
}

const char* sys_manager_get_reset_reason_string(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    
    if (reason < sizeof(reset_reason_strings) / sizeof(reset_reason_strings[0])) {
        return reset_reason_strings[reason];
    }
    
    return "未知原因";
}

uint32_t sys_manager_get_uptime(void)
{
    return esp_timer_get_time() / 1000000;
}

uint64_t sys_manager_get_timestamp_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void sys_manager_safe_delay(uint32_t ms)
{
    const uint32_t chunk_size = 100;  // 每100ms喂一次看门狗
    uint32_t remaining = ms;
    
    while (remaining > 0) {
        uint32_t delay_time = (remaining > chunk_size) ? chunk_size : remaining;
        vTaskDelay(pdMS_TO_TICKS(delay_time));
        
        // 喂看门狗
        sys_manager_feed_watchdog("safe_delay");
        
        remaining -= delay_time;
    }
}

void sys_manager_cleanup_memory(void)
{
    uint32_t current_time = sys_manager_get_uptime();
    
    // 避免频繁清理（最少间隔10秒）
    if (current_time - sys_mgr.last_cleanup_time < 10) {
        return;
    }
    
    ESP_LOGI(TAG, "执行内存清理...");
    
    // 清理堆碎片
    heap_caps_malloc_extmem_enable(1024);
    
    // 记录清理时间
    sys_mgr.last_cleanup_time = current_time;
    
    ESP_LOGI(TAG, "内存清理完成，当前可用内存: %d bytes", esp_get_free_heap_size());
}

bool sys_manager_can_perform_ota(void)
{
    // 检查系统状态
    sys_state_t state = sys_manager_get_state();
    if (state != SYS_STATE_NORMAL) {
        return false;
    }
    
    // 检查内存状态
    memory_status_t mem_status;
    if (sys_manager_get_memory_status(&mem_status) == ESP_OK) {
        if (mem_status.health != SYSTEM_HEALTH_GOOD) {
            return false;
        }
    }
    
    // 检查系统健康状态
    if (sys_mgr.health_status != SYSTEM_HEALTH_GOOD) {
        return false;
    }
    
    return true;
}

esp_err_t sys_manager_enter_ota_mode(void)
{
    ESP_LOGI(TAG, "进入OTA升级模式");
    return sys_manager_set_state(SYS_STATE_OTA_UPDATE);
}

esp_err_t sys_manager_exit_ota_mode(void)
{
    ESP_LOGI(TAG, "退出OTA升级模式");
    return sys_manager_set_state(SYS_STATE_NORMAL);
}

esp_err_t sys_manager_self_check(void)
{
    ESP_LOGI(TAG, "执行系统自检...");
    
    // 检查内存状态
    memory_status_t mem_status;
    esp_err_t ret = sys_manager_get_memory_status(&mem_status);
    if (ret != ESP_OK || mem_status.health == SYSTEM_HEALTH_EMERGENCY) {
        ESP_LOGE(TAG, "内存自检失败");
        return ESP_FAIL;
    }
    
    // 检查任务状态
    ret = sys_manager_check_tasks_health();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "任务自检失败");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "系统自检完成");
    return ESP_OK;
}

// ================================================================================
// 内部函数实现
// ================================================================================

/**
 * @brief 初始化看门狗
 */
static esp_err_t init_watchdog(void)
{
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    
    esp_err_t ret = esp_task_wdt_init(&wdt_config);
    if (ret == ESP_ERR_INVALID_STATE) {
        // 看门狗已经初始化，尝试重新配置
        ESP_LOGW(TAG, "看门狗已初始化，尝试重新配置...");
        ret = esp_task_wdt_reconfigure(&wdt_config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "看门狗重新配置完成 (超时: %d秒)", WDT_TIMEOUT_SECONDS);
        } else {
            // 重新配置失败，但这不是致命错误，使用默认配置继续
            ESP_LOGW(TAG, "看门狗重新配置失败，使用默认配置: %s", esp_err_to_name(ret));
            ret = ESP_OK;  // 不阻止系统启动
        }
    } else if (ret == ESP_OK) {
        ESP_LOGI(TAG, "看门狗初始化完成 (超时: %d秒)", WDT_TIMEOUT_SECONDS);
    } else {
        ESP_LOGE(TAG, "看门狗初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/**
 * @brief 初始化定时器
 */
static esp_err_t init_timers(void)
{
    esp_err_t ret;
    
    // 创建内存检查定时器
    esp_timer_create_args_t memory_timer_args = {
        .callback = memory_check_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "memory_check"
    };
    
    ret = esp_timer_create(&memory_timer_args, &sys_mgr.memory_check_timer);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 创建安全检查定时器
    esp_timer_create_args_t safety_timer_args = {
        .callback = safety_check_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "safety_check"
    };
    
    ret = esp_timer_create(&safety_timer_args, &sys_mgr.safety_check_timer);
    if (ret != ESP_OK) {
        esp_timer_delete(sys_mgr.memory_check_timer);
        return ret;
    }
    
    // 启动定时器
    ret = esp_timer_start_periodic(sys_mgr.memory_check_timer, MEMORY_CHECK_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        cleanup_timers();
        return ret;
    }
    
    ret = esp_timer_start_periodic(sys_mgr.safety_check_timer, SAFETY_CHECK_INTERVAL_MS * 1000);
    if (ret != ESP_OK) {
        cleanup_timers();
        return ret;
    }
    
    ESP_LOGI(TAG, "系统监控定时器启动完成");
    return ESP_OK;
}

/**
 * @brief 清理定时器
 */
static void cleanup_timers(void)
{
    if (sys_mgr.memory_check_timer) {
        esp_timer_stop(sys_mgr.memory_check_timer);
        esp_timer_delete(sys_mgr.memory_check_timer);
        sys_mgr.memory_check_timer = NULL;
    }
    
    if (sys_mgr.safety_check_timer) {
        esp_timer_stop(sys_mgr.safety_check_timer);
        esp_timer_delete(sys_mgr.safety_check_timer);
        sys_mgr.safety_check_timer = NULL;
    }
}

/**
 * @brief 评估系统整体健康状态
 */
static system_health_t evaluate_system_health(void)
{
    system_health_t health = SYSTEM_HEALTH_GOOD;
    
    // 检查内存状态
    memory_status_t mem_status;
    if (sys_manager_get_memory_status(&mem_status) == ESP_OK) {
        if (mem_status.health == SYSTEM_HEALTH_EMERGENCY) {
            health = SYSTEM_HEALTH_EMERGENCY;
        } else if (mem_status.health == SYSTEM_HEALTH_CRITICAL && health < SYSTEM_HEALTH_CRITICAL) {
            health = SYSTEM_HEALTH_CRITICAL;
        } else if (mem_status.health == SYSTEM_HEALTH_WARNING && health < SYSTEM_HEALTH_WARNING) {
            health = SYSTEM_HEALTH_WARNING;
        }
    }
    
    // 检查任务状态
    if (xSemaphoreTake(task_monitor_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        int dead_tasks = 0;
        int stack_warning_tasks = 0;
        
        for (int i = 0; i < task_count; i++) {
            if (!monitored_tasks[i].is_alive) {
                dead_tasks++;
            }
            if (monitored_tasks[i].stack_remaining < STACK_WARNING_THRESHOLD) {
                stack_warning_tasks++;
            }
        }
        
        if (dead_tasks > 2) {
            health = SYSTEM_HEALTH_EMERGENCY;
        } else if (dead_tasks > 0 || stack_warning_tasks > 3) {
            if (health < SYSTEM_HEALTH_CRITICAL) {
                health = SYSTEM_HEALTH_CRITICAL;
            }
        } else if (stack_warning_tasks > 0) {
            if (health < SYSTEM_HEALTH_WARNING) {
                health = SYSTEM_HEALTH_WARNING;
            }
        }
        
        xSemaphoreGive(task_monitor_mutex);
    }
    
    return health;
} 