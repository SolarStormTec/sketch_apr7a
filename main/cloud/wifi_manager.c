/**
 * @file wifi_manager.c
 * @brief WiFi管理模块实现
 * @author SmartJet Team
 * @date 2024
 */

#include "wifi_manager.h"
#include "nvs_storage.h"
#include "sys_manager.h"
#include "event_system.h"
#include "esp_smartconfig.h"
// #include "esp_wps.h" // 暂时不使用WPS功能
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char* TAG = "WIFI_MANAGER";

// 前向声明
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_reconnect_timer_callback(void* arg);
static void wifi_config_timeout_callback(void* arg);
static esp_err_t wifi_manager_apply_sta_config(void);
static void wifi_manager_update_signal_level(void);
static wifi_signal_level_t wifi_rssi_to_level(int8_t rssi);

// 全局WiFi管理器实例
static wifi_manager_t g_wifi_mgr = {0};
static bool g_wifi_initialized = false;
static wifi_event_callback_t g_wifi_event_callback = NULL;

/**
 * @brief WiFi管理器初始化
 */
esp_err_t wifi_manager_init(void)
{
    if (g_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi管理器已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化WiFi管理器...");
    
    // 创建互斥锁和事件组
    g_wifi_mgr.wifi_mutex = xSemaphoreCreateMutex();
    if (!g_wifi_mgr.wifi_mutex) {
        ESP_LOGE(TAG, "创建WiFi互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    g_wifi_mgr.wifi_event_group = xEventGroupCreate();
    if (!g_wifi_mgr.wifi_event_group) {
        ESP_LOGE(TAG, "创建WiFi事件组失败");
        goto cleanup;
    }
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环（如果尚未创建）
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "创建默认事件循环失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 创建STA和AP网络接口
    g_wifi_mgr.sta_netif = esp_netif_create_default_wifi_sta();
    g_wifi_mgr.ap_netif = esp_netif_create_default_wifi_ap();
    
    if (!g_wifi_mgr.sta_netif || !g_wifi_mgr.ap_netif) {
        ESP_LOGE(TAG, "创建网络接口失败");
        goto cleanup;
    }
    
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 注册事件处理程序
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             wifi_event_handler, NULL,
                                             &g_wifi_mgr.sta_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册WiFi事件处理程序失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             ip_event_handler, NULL,
                                             &g_wifi_mgr.ip_event_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册IP事件处理程序失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 创建定时器
    esp_timer_create_args_t timer_args = {
        .callback = wifi_reconnect_timer_callback,
        .arg = NULL,
        .name = "wifi_reconnect_timer"
    };
    ret = esp_timer_create(&timer_args, &g_wifi_mgr.reconnect_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建重连定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    timer_args.callback = wifi_config_timeout_callback;
    timer_args.name = "wifi_config_timer";
    ret = esp_timer_create(&timer_args, &g_wifi_mgr.config_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建配网定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化状态
    memset(&g_wifi_mgr.status, 0, sizeof(wifi_status_t));
    g_wifi_mgr.status.state = WIFI_STATE_DISCONNECTED;
    g_wifi_mgr.status.config_mode = WIFI_CONFIG_MODE_NONE;
    g_wifi_mgr.status.auto_reconnect = true;
    g_wifi_mgr.status.initialized = true;
    
    // 设置WiFi模式为STA
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // 加载保存的WiFi凭据
    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char password[WIFI_PASSWORD_MAX_LEN] = {0};
    if (wifi_manager_load_credentials(ssid, password) == ESP_OK) {
        if (strlen(ssid) > 0) {
            ESP_LOGI(TAG, "加载已保存的WiFi凭据: %s", ssid);
            memcpy(g_wifi_mgr.sta_config.sta.ssid, ssid, strlen(ssid));
            memcpy(g_wifi_mgr.sta_config.sta.password, password, strlen(password));
            g_wifi_mgr.status.credentials_saved = true;
        }
    }
    
    g_wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi管理器初始化完成");
    
    return ESP_OK;
    
cleanup:
    wifi_manager_deinit();
    return ESP_ERR_NO_MEM;
}

/**
 * @brief WiFi管理器反初始化
 */
void wifi_manager_deinit(void)
{
    if (!g_wifi_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化WiFi管理器...");
    
    // 停止WiFi
    wifi_manager_stop();
    
    // 删除定时器
    if (g_wifi_mgr.reconnect_timer) {
        esp_timer_stop(g_wifi_mgr.reconnect_timer);
        esp_timer_delete(g_wifi_mgr.reconnect_timer);
        g_wifi_mgr.reconnect_timer = NULL;
    }
    
    if (g_wifi_mgr.config_timer) {
        esp_timer_stop(g_wifi_mgr.config_timer);
        esp_timer_delete(g_wifi_mgr.config_timer);
        g_wifi_mgr.config_timer = NULL;
    }
    
    // 反注册事件处理程序
    if (g_wifi_mgr.sta_event_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              g_wifi_mgr.sta_event_instance);
        g_wifi_mgr.sta_event_instance = NULL;
    }
    
    if (g_wifi_mgr.ip_event_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              g_wifi_mgr.ip_event_instance);
        g_wifi_mgr.ip_event_instance = NULL;
    }
    
    // 反初始化WiFi
    esp_wifi_deinit();
    
    // 删除网络接口
    if (g_wifi_mgr.sta_netif) {
        esp_netif_destroy(g_wifi_mgr.sta_netif);
        g_wifi_mgr.sta_netif = NULL;
    }
    
    if (g_wifi_mgr.ap_netif) {
        esp_netif_destroy(g_wifi_mgr.ap_netif);
        g_wifi_mgr.ap_netif = NULL;
    }
    
    // 删除同步原语
    if (g_wifi_mgr.wifi_event_group) {
        vEventGroupDelete(g_wifi_mgr.wifi_event_group);
        g_wifi_mgr.wifi_event_group = NULL;
    }
    
    if (g_wifi_mgr.wifi_mutex) {
        vSemaphoreDelete(g_wifi_mgr.wifi_mutex);
        g_wifi_mgr.wifi_mutex = NULL;
    }
    
    g_wifi_initialized = false;
    ESP_LOGI(TAG, "WiFi管理器反初始化完成");
}

/**
 * @brief 启动WiFi
 */
esp_err_t wifi_manager_start(void)
{
    if (!g_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "启动WiFi...");
    
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 如果有保存的凭据，尝试连接
    if (g_wifi_mgr.status.credentials_saved) {
        ret = wifi_manager_apply_sta_config();
        if (ret == ESP_OK) {
            esp_wifi_connect();
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 停止WiFi
 */
esp_err_t wifi_manager_stop(void)
{
    if (!g_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "停止WiFi...");
    
    // 停止定时器
    esp_timer_stop(g_wifi_mgr.reconnect_timer);
    esp_timer_stop(g_wifi_mgr.config_timer);
    
    // 断开连接
    esp_wifi_disconnect();
    
    // 停止WiFi
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi停止失败: %s", esp_err_to_name(ret));
    }
    
    g_wifi_mgr.status.state = WIFI_STATE_DISCONNECTED;
    
    return ret;
}

/**
 * @brief 连接WiFi
 */
esp_err_t wifi_manager_connect(const char* ssid, const char* password)
{
    if (!g_wifi_initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "连接WiFi: %s", ssid);
    
    if (xSemaphoreTake(g_wifi_mgr.wifi_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取WiFi锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    // 更新STA配置
    memset(&g_wifi_mgr.sta_config, 0, sizeof(wifi_config_t));
    strcpy((char*)g_wifi_mgr.sta_config.sta.ssid, ssid);
    if (password) {
        strcpy((char*)g_wifi_mgr.sta_config.sta.password, password);
    }
    g_wifi_mgr.sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    // 应用配置
    esp_err_t ret = wifi_manager_apply_sta_config();
    if (ret == ESP_OK) {
        g_wifi_mgr.status.state = WIFI_STATE_CONNECTING;
        g_wifi_mgr.status.retry_count = 0;
        
        // 启动连接
        ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi连接启动失败: %s", esp_err_to_name(ret));
            g_wifi_mgr.status.state = WIFI_STATE_ERROR;
        }
    }
    
    xSemaphoreGive(g_wifi_mgr.wifi_mutex);
    return ret;
}

/**
 * @brief 断开WiFi连接
 */
esp_err_t wifi_manager_disconnect(void)
{
    if (!g_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "断开WiFi连接");
    
    g_wifi_mgr.status.auto_reconnect = false;
    return esp_wifi_disconnect();
}

/**
 * @brief 开始SmartConfig配网
 */
esp_err_t wifi_manager_start_smartconfig(void)
{
    if (!g_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "启动SmartConfig配网...");
    
    // 设置WiFi为STA模式
    esp_wifi_set_mode(WIFI_MODE_STA);
    
    // 启动SmartConfig
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    esp_err_t ret = esp_smartconfig_start(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SmartConfig启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    g_wifi_mgr.status.state = WIFI_STATE_SMARTCONFIG;
    g_wifi_mgr.status.config_mode = WIFI_CONFIG_MODE_SMARTCONFIG;
    
    // 启动超时定时器
    esp_timer_start_once(g_wifi_mgr.config_timer, SMARTCONFIG_TIMEOUT_MS * 1000);
    
    xEventGroupSetBits(g_wifi_mgr.wifi_event_group, WIFI_SMARTCONFIG_START_BIT);
    
    return ESP_OK;
}

/**
 * @brief 停止SmartConfig配网
 */
esp_err_t wifi_manager_stop_smartconfig(void)
{
    ESP_LOGI(TAG, "停止SmartConfig配网");
    
    esp_timer_stop(g_wifi_mgr.config_timer);
    esp_smartconfig_stop();
    
    g_wifi_mgr.status.config_mode = WIFI_CONFIG_MODE_NONE;
    
    return ESP_OK;
}

/**
 * @brief WiFi事件处理程序
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA启动");
            break;
            
        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
            ESP_LOGI(TAG, "WiFi连接成功: %s (频道 %d)", event->ssid, event->channel);
            
            g_wifi_mgr.status.state = WIFI_STATE_CONNECTED;
            g_wifi_mgr.status.channel = event->channel;
            memcpy(g_wifi_mgr.status.ssid, event->ssid, sizeof(event->ssid));
            memcpy(g_wifi_mgr.status.bssid, event->bssid, 6);
            g_wifi_mgr.status.connect_count++;
            g_wifi_mgr.status.retry_count = 0;
            g_wifi_mgr.status.last_connect_time = esp_timer_get_time() / 1000000;
            
            // 停止重连定时器
            esp_timer_stop(g_wifi_mgr.reconnect_timer);
            
            xEventGroupSetBits(g_wifi_mgr.wifi_event_group, WIFI_CONNECTED_BIT);
            
            // 发送事件
            if (g_wifi_event_callback) {
                g_wifi_event_callback(WIFI_STATE_CONNECTED, ESP_OK);
            }
            
            // 发送系统事件
            event_message_t sys_event = {
                .type = EVENT_WIFI_CONNECTED,
                .data = NULL,  // 简化事件数据处理
                .data_len = sizeof(wifi_signal_level_t),
                .timestamp = esp_timer_get_time() / 1000
            };
            event_system_post(&sys_event);
            break;
        }
        
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "WiFi断开连接，原因: %d", event->reason);
            
            g_wifi_mgr.status.state = WIFI_STATE_DISCONNECTED;
            g_wifi_mgr.status.disconnect_count++;
            g_wifi_mgr.status.last_error = event->reason;
            
            xEventGroupClearBits(g_wifi_mgr.wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(g_wifi_mgr.wifi_event_group, WIFI_FAIL_BIT);
            
            // 自动重连
            if (g_wifi_mgr.status.auto_reconnect && 
                g_wifi_mgr.status.retry_count < WIFI_RETRY_MAX_COUNT) {
                g_wifi_mgr.status.state = WIFI_STATE_RECONNECTING;
                g_wifi_mgr.status.retry_count++;
                
                ESP_LOGI(TAG, "启动重连定时器，重试次数: %d", g_wifi_mgr.status.retry_count);
                esp_timer_start_once(g_wifi_mgr.reconnect_timer, WIFI_RETRY_DELAY_MS * 1000);
            }
            
            // 发送事件
            if (g_wifi_event_callback) {
                g_wifi_event_callback(WIFI_STATE_DISCONNECTED, event->reason);
            }
            
            // 发送系统事件 - 不传递临时数据地址，避免悬空指针问题
            event_message_t sys_event = {
                .type = EVENT_WIFI_DISCONNECTED,
                .data = NULL,  // 不传递临时数据地址
                .data_len = 0,
                .timestamp = esp_timer_get_time() / 1000
            };
            event_system_post(&sys_event);
            break;
        }
        
        case WIFI_EVENT_SCAN_DONE:
        {
            wifi_event_sta_scan_done_t* event = (wifi_event_sta_scan_done_t*)event_data;
            ESP_LOGI(TAG, "WiFi扫描完成，找到 %d 个AP", event->number);
            
            // 获取扫描结果
            uint16_t ap_count = WIFI_SCAN_MAX_AP;
            wifi_ap_record_t ap_records[WIFI_SCAN_MAX_AP];
            
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);
            
            // 转换为内部格式
            g_wifi_mgr.scan_count = ap_count;
            for (int i = 0; i < ap_count && i < WIFI_SCAN_MAX_AP; i++) {
                strcpy(g_wifi_mgr.scan_results[i].ssid, (char*)ap_records[i].ssid);
                memcpy(g_wifi_mgr.scan_results[i].bssid, ap_records[i].bssid, 6);
                g_wifi_mgr.scan_results[i].primary = ap_records[i].primary;
                g_wifi_mgr.scan_results[i].second = ap_records[i].second;
                g_wifi_mgr.scan_results[i].rssi = ap_records[i].rssi;
                g_wifi_mgr.scan_results[i].authmode = ap_records[i].authmode;
            }
            break;
        }
        
        default:
            ESP_LOGW(TAG, "未处理的WiFi事件: %d", (int)event_id);
            break;
    }
}

/**
 * @brief SmartConfig事件处理程序 - 预留功能
 */
/*
static void smartconfig_event_handler(void* arg, esp_event_base_t event_base,
                                     int32_t event_id, void* event_data)
{
    switch (event_id) {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "SmartConfig扫描完成");
            break;
            
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "SmartConfig找到频道");
            break;
            
        case SC_EVENT_GOT_SSID_PSWD:
        {
            smartconfig_event_got_ssid_pswd_t* event = (smartconfig_event_got_ssid_pswd_t*)event_data;
            ESP_LOGI(TAG, "SmartConfig获取到SSID和密码");
            
            // 保存凭据
            wifi_manager_save_credentials((char*)event->ssid, (char*)event->password);
            
            // 连接WiFi
            wifi_manager_connect((char*)event->ssid, (char*)event->password);
            
            // 停止SmartConfig
            wifi_manager_stop_smartconfig();
            
            xEventGroupSetBits(g_wifi_mgr.wifi_event_group, WIFI_SMARTCONFIG_DONE_BIT);
            break;
        }
        
        default:
            ESP_LOGW(TAG, "未处理的SmartConfig事件: %d", (int)event_id);
            break;
    }
}
*/

/**
 * @brief IP事件处理程序
 */
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "获取到IP地址: " IPSTR, IP2STR(&event->ip_info.ip));
        
        // 更新IP信息
        memcpy(&g_wifi_mgr.status.ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
        
        // 更新信号强度
        wifi_manager_update_signal_level();
        
        // 发送系统事件 - 不传递数据指针，避免悬空指针问题
        event_message_t sys_event = {
            .type = EVENT_WIFI_CONNECTED,
            .data = NULL,  // 不传递局部变量地址
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&sys_event);
    }
}

/**
 * @brief 重连定时器回调
 */
static void wifi_reconnect_timer_callback(void* arg)
{
    ESP_LOGI(TAG, "重连定时器触发，尝试重连");
    
    if (g_wifi_mgr.status.state == WIFI_STATE_RECONNECTING) {
        esp_wifi_connect();
    }
}

/**
 * @brief 配网超时定时器回调
 */
static void wifi_config_timeout_callback(void* arg)
{
    ESP_LOGW(TAG, "配网超时");
    
    if (g_wifi_mgr.status.config_mode == WIFI_CONFIG_MODE_SMARTCONFIG) {
        wifi_manager_stop_smartconfig();
    }
    
    g_wifi_mgr.status.state = WIFI_STATE_ERROR;
    g_wifi_mgr.status.last_error = ESP_ERR_TIMEOUT;
}

/**
 * @brief 应用STA配置
 */
static esp_err_t wifi_manager_apply_sta_config(void)
{
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &g_wifi_mgr.sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置STA配置失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief 更新信号强度等级
 */
static void wifi_manager_update_signal_level(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        g_wifi_mgr.status.rssi = ap_info.rssi;
        g_wifi_mgr.status.signal_level = wifi_rssi_to_level(ap_info.rssi);
    }
}

/**
 * @brief RSSI转信号等级
 */
static wifi_signal_level_t wifi_rssi_to_level(int8_t rssi)
{
    if (rssi >= -50) return WIFI_SIGNAL_LEVEL_4;
    if (rssi >= -60) return WIFI_SIGNAL_LEVEL_3;
    if (rssi >= -70) return WIFI_SIGNAL_LEVEL_2;
    if (rssi >= -80) return WIFI_SIGNAL_LEVEL_1;
    return WIFI_SIGNAL_LEVEL_0;
}

/**
 * @brief 保存WiFi凭据
 */
esp_err_t wifi_manager_save_credentials(const char* ssid, const char* password)
{
    if (!ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "保存WiFi凭据: %s", ssid);
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_WIFI, &handle);
    if (result != NVS_RESULT_OK) {
        return ESP_FAIL;
    }
    
    nvs_storage_set_string(handle, "ssid", ssid);
    if (password) {
        nvs_storage_set_string(handle, "password", password);
    }
    nvs_storage_commit(handle);
    nvs_storage_close(handle);
    
    g_wifi_mgr.status.credentials_saved = true;
    
    return ESP_OK;
}

/**
 * @brief 加载WiFi凭据
 */
esp_err_t wifi_manager_load_credentials(char* ssid, char* password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_WIFI, &handle);
    if (result != NVS_RESULT_OK) {
        return ESP_FAIL;
    }
    
    size_t ssid_len = WIFI_SSID_MAX_LEN;
    size_t password_len = WIFI_PASSWORD_MAX_LEN;
    
    result = nvs_storage_get_string(handle, "ssid", ssid, &ssid_len);
    if (result == NVS_RESULT_OK) {
        nvs_storage_get_string(handle, "password", password, &password_len);
    }
    
    nvs_storage_close(handle);
    
    return (result == NVS_RESULT_OK) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 清除WiFi凭据
 */
esp_err_t wifi_manager_clear_credentials(void)
{
    ESP_LOGI(TAG, "清除WiFi凭据");
    
    nvs_handle_t handle;
    nvs_result_t result = nvs_storage_open(NVS_NAMESPACE_WIFI, &handle);
    if (result != NVS_RESULT_OK) {
        return ESP_FAIL;
    }
    
    nvs_storage_erase_key(handle, "ssid");
    nvs_storage_erase_key(handle, "password");
    nvs_storage_commit(handle);
    nvs_storage_close(handle);
    
    g_wifi_mgr.status.credentials_saved = false;
    
    return ESP_OK;
}

// 状态查询接口实现
wifi_status_t wifi_manager_get_status(void) { return g_wifi_mgr.status; }
wifi_state_t wifi_manager_get_state(void) { return g_wifi_mgr.status.state; }
bool wifi_manager_is_connected(void) { return g_wifi_mgr.status.state == WIFI_STATE_CONNECTED; }
bool wifi_manager_is_configuring(void) { 
    return (g_wifi_mgr.status.state == WIFI_STATE_SMARTCONFIG || 
            g_wifi_mgr.status.state == WIFI_STATE_AP_MODE); 
}
int8_t wifi_manager_get_rssi(void) { return g_wifi_mgr.status.rssi; }
wifi_signal_level_t wifi_manager_get_signal_level(void) { return g_wifi_mgr.status.signal_level; }

/**
 * @brief 打印WiFi状态
 */
void wifi_manager_dump_status(void)
{
    wifi_status_t status = wifi_manager_get_status();
    
    ESP_LOGI(TAG, "========== WiFi状态 ==========");
    ESP_LOGI(TAG, "状态: %d", status.state);
    ESP_LOGI(TAG, "SSID: %s", status.ssid);
    ESP_LOGI(TAG, "频道: %d", status.channel);
    ESP_LOGI(TAG, "信号强度: %d dBm (等级 %d)", status.rssi, status.signal_level);
    ESP_LOGI(TAG, "IP地址: " IPSTR, IP2STR(&status.ip_info.ip));
    ESP_LOGI(TAG, "连接次数: %lu", status.connect_count);
    ESP_LOGI(TAG, "断开次数: %lu", status.disconnect_count);
    ESP_LOGI(TAG, "自动重连: %s", status.auto_reconnect ? "启用" : "禁用");
    ESP_LOGI(TAG, "==============================");
}

// 删除重复的函数定义，使用前面已有的实现 