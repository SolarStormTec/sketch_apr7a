/**
 * @file main.c
 * @brief SmartJet v2.0 主程序文件
 * @author SmartJet Team
 * @date 2024
 * 
 * 系统主程序，负责初始化和启动各个功能模块
 */

#include "include/smartjet_common.h"
#include "system/sys_manager.h"
#include "system/task_manager.h"
#include "config/config_mgr.h"
#include "utils/event_system.h"
#include "cloud/wifi_manager.h"
#include "cloud/onenet_mqtt.h"
#include "battery/bq40z50.h"
#include "ota/ota_manager.h"
#include "display/st7789_driver.h"
#include "sensors/sensor_mgr.h"
#include "engine/engine_ctrl.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

// 日志标签
static const char* TAG = "MAIN";

// 全局系统状态变量
smartjet_global_t g_smartjet;

/**
 * @brief 系统早期初始化
 * @return 初始化结果
 */
static esp_err_t early_init(void)
{
    esp_err_t ret = ESP_OK;

    // 初始化NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS分区需要擦除，正在执行...");
        CHECK_ERROR_RETURN(nvs_flash_erase(), TAG, "NVS擦除失败");
        ret = nvs_flash_init();
    }
    CHECK_ERROR_RETURN(ret, TAG, "NVS初始化失败");

    // 初始化网络接口
    CHECK_ERROR_RETURN(esp_netif_init(), TAG, "网络接口初始化失败");
    
    // 创建默认事件循环
    CHECK_ERROR_RETURN(esp_event_loop_create_default(), TAG, "事件循环创建失败");

    ESP_LOGI(TAG, "早期初始化完成");
    return ESP_OK;
}

/**
 * @brief 初始化全局状态结构
 */
static void init_global_state(void)
{
    // 清零全局状态
    memset(&g_smartjet, 0, sizeof(smartjet_global_t));
    
    // 创建同步原语
    g_smartjet.system_mutex = xSemaphoreCreateMutex();
    g_smartjet.event_queue = xQueueCreate(32, sizeof(event_message_t));
    g_smartjet.system_events = xEventGroupCreate();
    
    // 检查创建结果
    if (!g_smartjet.system_mutex || !g_smartjet.event_queue || !g_smartjet.system_events) {
        ESP_LOGE(TAG, "同步原语创建失败");
        esp_restart();
    }
    
    // 设置系统初始状态
    g_smartjet.system.current_state = SYS_STATE_BOOT;
    g_smartjet.system.state_enter_time = esp_timer_get_time() / 1000;
    g_smartjet.system.last_reset_reason = esp_reset_reason();
    
    // 初始化发动机状态
    g_smartjet.engine.state = ENGINE_STATE_STOPPED;
    g_smartjet.engine.max_start_attempts = 3;
    
    // 初始化显示状态
    g_smartjet.display.current_page = UI_PAGE_MAIN;
    g_smartjet.display.brightness_level = 80;
    g_smartjet.display.auto_sleep_enabled = true;
    
    // 初始化OTA状态
    g_smartjet.ota.current_version = SMARTJET_VERSION_MAJOR * 10000 + 
                                     SMARTJET_VERSION_MINOR * 100 + 
                                     SMARTJET_VERSION_PATCH;
    // OTA状态初始化
    g_smartjet.ota.state = OTA_STATE_IDLE;
    g_smartjet.ota.current_version = 1;
    
    ESP_LOGI(TAG, "全局状态初始化完成");
}

/**
 * @brief 打印系统信息
 */
static void print_system_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "SmartJet v%s Generator Controller", SMARTJET_VERSION_STRING);
    ESP_LOGI(TAG, "Build: %s", SMARTJET_BUILD_DATE);
    ESP_LOGI(TAG, "Hardware: %s", HARDWARE_VERSION);
    ESP_LOGI(TAG, "==========================================");
    ESP_LOGI(TAG, "芯片信息:");
    ESP_LOGI(TAG, "  型号: %s", MCU_TYPE);
    ESP_LOGI(TAG, "  内核数: %d", chip_info.cores);
    ESP_LOGI(TAG, "  WiFi: %s", (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "支持" : "不支持");
    ESP_LOGI(TAG, "  蓝牙: %s", (chip_info.features & CHIP_FEATURE_BT) ? "支持" : "不支持");
    uint32_t flash_size = 0;
    esp_flash_get_size(esp_flash_default_chip, &flash_size);
    ESP_LOGI(TAG, "  Flash: %dMB %s", flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "内置" : "外置");
    ESP_LOGI(TAG, "内存信息:");
    ESP_LOGI(TAG, "  可用堆内存: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  最小可用堆内存: %d bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "重启原因: %s", sys_manager_get_reset_reason_string());
    ESP_LOGI(TAG, "==========================================");
}

/**
 * @brief 应用程序主函数
 */
void app_main(void)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "SmartJet v2.0 启动中...");
    
    // 早期初始化
    ret = early_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "早期初始化失败: %s", esp_err_to_name(ret));
        esp_restart();
    }
    
    // 初始化全局状态
    init_global_state();
    
        // 打印系统信息
    print_system_info();

    // 初始化系统管理器 (必须在事件系统之前)
    ret = sys_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "系统管理器初始化失败: %s", esp_err_to_name(ret));
        esp_restart();
    }

    // 初始化事件系统
    ret = event_system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "事件系统初始化失败: %s", esp_err_to_name(ret));
        esp_restart();
    }

    // 加载系统配置
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置管理器初始化失败: %s", esp_err_to_name(ret));
        esp_restart();
    }
    
    // 启动任务管理器
    ret = task_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "任务管理器启动失败: %s", esp_err_to_name(ret));
        esp_restart();
    }
    
    // 初始化硬件驱动模块
    ESP_LOGI(TAG, "初始化硬件驱动模块...");
    
    // 初始化传感器管理器
    ret = sensor_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "传感器管理器初始化失败: %s", esp_err_to_name(ret));
    }
    
    // 初始化发动机控制器
    ret = engine_controller_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "发动机控制器初始化失败: %s", esp_err_to_name(ret));
    }
    
    // 初始化电池管理模块
    ret = bq40z50_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "电池管理模块初始化失败: %s", esp_err_to_name(ret));
        // 电池管理失败不影响系统启动，继续运行
    }
    
    // 初始化显示驱动
    ret = st7789_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "显示驱动初始化失败: %s", esp_err_to_name(ret));
        // 显示失败不影响系统启动，继续运行
    }
    
    // 初始化网络和云服务模块
    ESP_LOGI(TAG, "初始化网络和云服务模块...");
    
    // 初始化WiFi管理器
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi管理器初始化失败: %s", esp_err_to_name(ret));
        esp_restart();
    }
    
    // 初始化OneNet MQTT模块
    onenet_config_t mqtt_config = {
        .product_id = "ZjX327b28N",  // 从model-ZjX327b28N-7.json文件名推测的产品ID
        .device_name = "smartjet_device_001",  // 默认设备名，需从配置中加载
        .device_key = "YOUR_DEVICE_KEY_HERE",  // 需从配置中加载
        .port = ONENET_MQTT_PORT,
        .keepalive = ONENET_MQTT_KEEPALIVE,
        .timeout_ms = ONENET_MQTT_TIMEOUT,
        .auto_reconnect = true,
        .reconnect_interval_ms = 5000
    };
    
    ret = onenet_mqtt_init(&mqtt_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OneNet MQTT初始化失败: %s", esp_err_to_name(ret));
        // MQTT初始化失败不影响基本功能，继续运行
    }
    
    // 初始化OTA管理器
    ota_config_t ota_config = {
        .auto_update_enabled = true,
        .check_interval_minutes = 60,
        .min_rssi_threshold = -75,
        .download_timeout_ms = 300000,
        .max_retry_count = 3
    };
    
    ret = ota_manager_init(&ota_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA管理器初始化失败: %s", esp_err_to_name(ret));
        // OTA初始化失败不影响基本功能，继续运行
    }
    
    // 启动网络连接
    ESP_LOGI(TAG, "启动网络连接...");
    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi启动失败: %s", esp_err_to_name(ret));
    }
    
    // 设置系统为正常运行状态
    g_smartjet.system.current_state = SYS_STATE_NORMAL;
    
    // 发送系统启动事件
    event_message_t boot_event = {
        .type = EVENT_SYSTEM_BOOT,
        .data = NULL,
        .data_len = 0,
        .timestamp = esp_timer_get_time() / 1000
    };
    event_system_post(&boot_event);
    
    ESP_LOGI(TAG, "SmartJet v2.0 系统启动完成");
    
    // 主线程进入监控循环
    while (1) {
        // 更新系统运行时间
        g_smartjet.system.uptime_seconds = esp_timer_get_time() / 1000000;
        
        // 更新内存信息
        g_smartjet.system.free_heap = esp_get_free_heap_size();
        g_smartjet.system.min_free_heap = esp_get_minimum_free_heap_size();
        
        // 检查系统状态
        if (g_smartjet.shutdown_requested) {
            ESP_LOGW(TAG, "收到系统关闭请求，正在关闭...");
            break;
        }
        
        // 主线程心跳（每秒一次）
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    ESP_LOGI(TAG, "SmartJet v2.0 系统正在关闭...");
    
    // 清理网络和云服务模块
    ESP_LOGI(TAG, "清理网络和云服务模块...");
    ota_manager_deinit();
    onenet_mqtt_deinit();
    wifi_manager_deinit();
    
    // 清理硬件驱动模块
    ESP_LOGI(TAG, "清理硬件驱动模块...");
    st7789_deinit();
    bq40z50_deinit();
    engine_controller_deinit();
    sensor_manager_deinit();
    
    // 清理系统核心模块
    task_manager_stop();
    sys_manager_cleanup();
    config_manager_cleanup();
    event_system_cleanup();
    
    // 清理同步原语
    if (g_smartjet.system_mutex) {
        vSemaphoreDelete(g_smartjet.system_mutex);
    }
    if (g_smartjet.event_queue) {
        vQueueDelete(g_smartjet.event_queue);
    }
    if (g_smartjet.system_events) {
        vEventGroupDelete(g_smartjet.system_events);
    }
    
    ESP_LOGI(TAG, "SmartJet v2.0 系统关闭完成");
} 