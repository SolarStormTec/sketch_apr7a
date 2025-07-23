/**
 * @file event_system.c
 * @brief 事件系统实现
 * @author SmartJet Team
 * @date 2024
 * 
 * 事件系统负责系统内部事件的发布、订阅和处理
 */

#include "event_system.h"
#include "system/sys_manager.h"
#include "include/smartjet_common.h"

// 日志标签
static const char* TAG = "EVENT_SYSTEM";

// ================================================================================
// 内部数据结构
// ================================================================================

// 事件处理器注册表
static event_handler_info_t event_handlers[MAX_EVENT_HANDLERS];
static uint8_t handler_count = 0;
static SemaphoreHandle_t handler_mutex = NULL;

// 事件处理任务
static TaskHandle_t event_handler_task = NULL;
static bool event_system_running = false;

// 事件名称映射表
static const char* event_names[] = {
    [EVENT_SYSTEM_BOOT]            = "系统启动",
    [EVENT_WIFI_CONNECTED]         = "WiFi连接",
    [EVENT_WIFI_DISCONNECTED]      = "WiFi断开",
    [EVENT_MQTT_CONNECTED]         = "MQTT连接",
    [EVENT_MQTT_DISCONNECTED]      = "MQTT断开",
    [EVENT_ENGINE_START_REQUEST]   = "发动机启动请求",
    [EVENT_ENGINE_STOP_REQUEST]    = "发动机停止请求",
    [EVENT_ENGINE_STARTED]         = "发动机已启动",
    [EVENT_ENGINE_STOPPED]         = "发动机已停止",
    [EVENT_OIL_ALARM]              = "机油报警",
    [EVENT_OVERSPEED_ALARM]        = "超速报警",
    [EVENT_BATTERY_LOW]            = "电池电量低",
    [EVENT_BATTERY_HIGH]           = "电池电压高",
    [EVENT_OTA_START]              = "OTA开始",
    [EVENT_OTA_SUCCESS]            = "OTA成功",
    [EVENT_OTA_FAILED]             = "OTA失败",
    [EVENT_BUTTON_PRESSED]         = "按键按下",
    [EVENT_RF_REMOTE_RECEIVED]     = "遥控信号",
    [EVENT_CLOUD_COMMAND_RECEIVED] = "云端命令",
    [EVENT_MEMORY_WARNING]         = "内存警告",
    [EVENT_SYSTEM_ERROR]           = "系统错误"
};

// ================================================================================
// 内部函数声明
// ================================================================================
static void event_handler_task_func(void* pvParameters);
static void handle_event(const event_message_t* event);
static int find_handler_index(system_event_t event_type, event_handler_t handler);

// ================================================================================
// 默认事件处理器
// ================================================================================

/**
 * @brief 系统启动事件处理器
 */
static void handle_system_boot(const event_message_t* event)
{
    ESP_LOGI(TAG, "系统启动事件处理：版本 %s", SMARTJET_VERSION_STRING);
    
    // 更新系统状态
    sys_manager_set_state(SYS_STATE_INITIALIZING);
    
    // 执行启动后的初始化工作
    // TODO: 可以在这里添加启动后的特定初始化逻辑
}

/**
 * @brief WiFi连接事件处理器
 */
static void handle_wifi_connected(const event_message_t* event)
{
    ESP_LOGI(TAG, "WiFi连接成功");
    
    // 可以在这里添加WiFi连接后的处理逻辑
    // 例如：启动NTP同步、开始MQTT连接等
}

/**
 * @brief WiFi断开事件处理器
 */
static void handle_wifi_disconnected(const event_message_t* event)
{
    ESP_LOGW(TAG, "WiFi连接断开");
    
    // 更新统计数据
    if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_smartjet.stats.wifi_disconnect_count++;
        xSemaphoreGive(g_smartjet.system_mutex);
    }
}

/**
 * @brief 内存警告事件处理器
 */
static void handle_memory_warning(const event_message_t* event)
{
    if (event->data) {
        memory_status_t* mem_status = (memory_status_t*)event->data;
        ESP_LOGW(TAG, "内存警告：可用内存 %d bytes", mem_status->free_heap);
        
        // 更新统计数据
        if (xSemaphoreTake(g_smartjet.system_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_smartjet.stats.memory_warning_count++;
            xSemaphoreGive(g_smartjet.system_mutex);
        }
    }
}

/**
 * @brief 系统错误事件处理器
 */
static void handle_system_error(const event_message_t* event)
{
    ESP_LOGE(TAG, "系统错误事件");
    
    if (event->data && event->data_len > 0) {
        ESP_LOGE(TAG, "错误信息: %.*s", (int)event->data_len, (char*)event->data);
    }
    
    // 可以在这里添加错误处理逻辑
    // 例如：保存错误日志、触发安全模式等
}

// ================================================================================
// 公共函数实现
// ================================================================================

esp_err_t event_system_init(void)
{
    if (event_system_running) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化事件系统...");
    
    // 创建处理器互斥锁
    handler_mutex = xSemaphoreCreateMutex();
    if (!handler_mutex) {
        ESP_LOGE(TAG, "创建处理器互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化处理器数组
    memset(event_handlers, 0, sizeof(event_handlers));
    handler_count = 0;
    
    // 创建事件处理任务
    BaseType_t result = xTaskCreatePinnedToCore(
        event_handler_task_func,
        "event_handler",
        EVENT_HANDLER_TASK_STACK,
        NULL,
        EVENT_HANDLER_TASK_PRIORITY,
        &event_handler_task,
        1  // 绑定到Core 1
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建事件处理任务失败");
        vSemaphoreDelete(handler_mutex);
        return ESP_FAIL;
    }
    
    // 注册系统管理器的任务监控
    sys_manager_register_task(event_handler_task, "event_handler");
    
    // 注册默认事件处理器
    event_system_register_handler(EVENT_SYSTEM_BOOT, handle_system_boot, "系统启动处理器");
    event_system_register_handler(EVENT_WIFI_CONNECTED, handle_wifi_connected, "WiFi连接处理器");
    event_system_register_handler(EVENT_WIFI_DISCONNECTED, handle_wifi_disconnected, "WiFi断开处理器");
    event_system_register_handler(EVENT_MEMORY_WARNING, handle_memory_warning, "内存警告处理器");
    event_system_register_handler(EVENT_SYSTEM_ERROR, handle_system_error, "系统错误处理器");
    
    event_system_running = true;
    
    ESP_LOGI(TAG, "事件系统初始化完成");
    return ESP_OK;
}

void event_system_cleanup(void)
{
    if (!event_system_running) {
        return;
    }
    
    ESP_LOGI(TAG, "清理事件系统...");
    
    event_system_running = false;
    
    // 等待事件处理任务退出
    if (event_handler_task) {
        sys_manager_unregister_task(event_handler_task);
        vTaskDelete(event_handler_task);
        event_handler_task = NULL;
    }
    
    // 删除互斥锁
    if (handler_mutex) {
        vSemaphoreDelete(handler_mutex);
        handler_mutex = NULL;
    }
    
    // 清理处理器数组
    handler_count = 0;
    
    ESP_LOGI(TAG, "事件系统清理完成");
}

esp_err_t event_system_post(const event_message_t* event)
{
    if (!event || !event_system_running) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_smartjet.event_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 发送事件到队列
    BaseType_t result = xQueueSend(g_smartjet.event_queue, event, pdMS_TO_TICKS(100));
    
    if (result != pdTRUE) {
        ESP_LOGW(TAG, "事件队列已满，丢弃事件: %s", event_system_get_event_name(event->type));
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

esp_err_t event_system_post_from_isr(const event_message_t* event)
{
    if (!event || !event_system_running) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_smartjet.event_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t higher_priority_task_woken = pdFALSE;
    BaseType_t result = xQueueSendFromISR(g_smartjet.event_queue, event, &higher_priority_task_woken);
    
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    
    return (result == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t event_system_register_handler(system_event_t event_type, 
                                       event_handler_t handler, 
                                       const char* name)
{
    if (!handler || !name) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NO_MEM;
    
    // 检查是否已存在
    if (find_handler_index(event_type, handler) >= 0) {
        ESP_LOGW(TAG, "事件处理器已存在: %s", name);
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    
    // 查找空位
    if (handler_count < MAX_EVENT_HANDLERS) {
        event_handler_info_t* info = &event_handlers[handler_count];
        info->event_type = event_type;
        info->handler = handler;
        info->name = name;
        info->enabled = true;
        info->call_count = 0;
        info->last_call_time = 0;
        
        handler_count++;
        
        ESP_LOGI(TAG, "注册事件处理器: %s -> %s", 
                 event_system_get_event_name(event_type), name);
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "事件处理器数组已满，无法注册: %s", name);
    }
    
cleanup:
    xSemaphoreGive(handler_mutex);
    return ret;
}

esp_err_t event_system_unregister_handler(system_event_t event_type, 
                                         event_handler_t handler)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    int index = find_handler_index(event_type, handler);
    if (index >= 0) {
        ESP_LOGI(TAG, "注销事件处理器: %s", event_handlers[index].name);
        
        // 移动数组元素
        for (int i = index; i < handler_count - 1; i++) {
            event_handlers[i] = event_handlers[i + 1];
        }
        handler_count--;
        
        ret = ESP_OK;
    }
    
    xSemaphoreGive(handler_mutex);
    return ret;
}

esp_err_t event_system_enable_handler(system_event_t event_type, 
                                     event_handler_t handler, 
                                     bool enabled)
{
    if (!handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    int index = find_handler_index(event_type, handler);
    if (index >= 0) {
        event_handlers[index].enabled = enabled;
        ESP_LOGI(TAG, "%s事件处理器: %s", 
                 enabled ? "启用" : "禁用", event_handlers[index].name);
        ret = ESP_OK;
    }
    
    xSemaphoreGive(handler_mutex);
    return ret;
}

esp_err_t event_system_get_handler_stats(uint8_t* count)
{
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *count = handler_count;
        xSemaphoreGive(handler_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

void event_system_print_status(void)
{
    ESP_LOGI(TAG, "========== 事件系统状态 ==========");
    ESP_LOGI(TAG, "系统运行状态: %s", event_system_running ? "运行中" : "已停止");
    ESP_LOGI(TAG, "处理器数量: %d/%d", handler_count, MAX_EVENT_HANDLERS);
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < handler_count; i++) {
            const event_handler_info_t* info = &event_handlers[i];
            ESP_LOGI(TAG, "处理器 %d: %s | 事件: %s | 状态: %s | 调用次数: %d",
                     i + 1, info->name, 
                     event_system_get_event_name(info->event_type),
                     info->enabled ? "启用" : "禁用", 
                     info->call_count);
        }
        xSemaphoreGive(handler_mutex);
    }
    
    ESP_LOGI(TAG, "===============================");
}

event_message_t event_system_create_event(system_event_t type, 
                                         const void* data, 
                                         size_t data_len)
{
    event_message_t event = {
        .type = type,
        .data = (void*)data,
        .data_len = data_len,
        .timestamp = sys_manager_get_timestamp_ms()
    };
    
    return event;
}

const char* event_system_get_event_name(system_event_t event_type)
{
    if (event_type < sizeof(event_names) / sizeof(event_names[0]) && 
        event_names[event_type]) {
        return event_names[event_type];
    }
    
    return "未知事件";
}

// ================================================================================
// 内部函数实现
// ================================================================================

/**
 * @brief 事件处理任务
 */
static void event_handler_task_func(void* pvParameters)
{
    ESP_LOGI(TAG, "事件处理任务启动");
    
    event_message_t event;
    
    while (event_system_running) {
        // 任务心跳
        sys_manager_task_heartbeat(xTaskGetCurrentTaskHandle());
        
        // 等待事件
        if (xQueueReceive(g_smartjet.event_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle_event(&event);
        }
        
        // 检查系统关闭请求
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "事件处理任务收到关闭请求");
            break;
        }
    }
    
    ESP_LOGI(TAG, "事件处理任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 处理单个事件
 */
static void handle_event(const event_message_t* event)
{
    if (!event) {
        return;
    }
    
    ESP_LOGD(TAG, "处理事件: %s", event_system_get_event_name(event->type));
    
    if (xSemaphoreTake(handler_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "无法获取处理器互斥锁，跳过事件处理");
        return;
    }
    
    // 遍历所有处理器
    for (int i = 0; i < handler_count; i++) {
        event_handler_info_t* info = &event_handlers[i];
        
        if (info->event_type == event->type && info->enabled && info->handler) {
            // 调用处理器
            info->handler(event);
            info->call_count++;
            info->last_call_time = sys_manager_get_uptime();
        }
    }
    
    xSemaphoreGive(handler_mutex);
}

/**
 * @brief 查找处理器索引
 */
static int find_handler_index(system_event_t event_type, event_handler_t handler)
{
    for (int i = 0; i < handler_count; i++) {
        if (event_handlers[i].event_type == event_type && 
            event_handlers[i].handler == handler) {
            return i;
        }
    }
    
    return -1;
} 