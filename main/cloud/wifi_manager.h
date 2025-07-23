/**
 * @file wifi_manager.h
 * @brief WiFi管理模块
 * @author SmartJet Team
 * @date 2024
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "smartjet_common.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"

// WiFi配置参数
#define WIFI_SSID_MAX_LEN           32          // SSID最大长度
#define WIFI_PASSWORD_MAX_LEN       64          // 密码最大长度
#define WIFI_CONNECT_TIMEOUT_MS     10000       // 连接超时时间
#define WIFI_RETRY_MAX_COUNT        5           // 最大重试次数
#define WIFI_RETRY_DELAY_MS         5000        // 重试间隔
#define WIFI_SCAN_MAX_AP            20          // 最大扫描AP数量

// SmartConfig参数
#define SMARTCONFIG_TIMEOUT_MS      120000      // SmartConfig超时时间
#define SMARTCONFIG_RETRY_COUNT     3           // SmartConfig重试次数

// AP模式参数
#define WIFI_AP_SSID                "SmartJet-Config"  // 配网AP SSID
#define WIFI_AP_PASSWORD            "12345678"         // 配网AP密码
#define WIFI_AP_CHANNEL             1                  // AP频道
#define WIFI_AP_MAX_CONN            4                  // 最大连接数
#define WIFI_AP_BEACON_INTERVAL     100                // 信标间隔

// WiFi状态
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,   // 未连接
    WIFI_STATE_CONNECTING,          // 连接中
    WIFI_STATE_CONNECTED,           // 已连接
    WIFI_STATE_RECONNECTING,        // 重连中
    WIFI_STATE_SCANNING,            // 扫描中
    WIFI_STATE_SMARTCONFIG,         // SmartConfig模式
    WIFI_STATE_AP_MODE,             // AP配网模式
    WIFI_STATE_ERROR                // 错误状态
} wifi_state_t;

// WiFi配网模式
typedef enum {
    WIFI_CONFIG_MODE_NONE = 0,      // 无配网
    WIFI_CONFIG_MODE_SMARTCONFIG,   // SmartConfig配网
    WIFI_CONFIG_MODE_AP_WEB,        // AP+Web配网
    WIFI_CONFIG_MODE_WPS            // WPS配网
} wifi_config_mode_t;

// WiFi信号强度等级
typedef enum {
    WIFI_SIGNAL_LEVEL_0 = 0,        // 无信号
    WIFI_SIGNAL_LEVEL_1,            // 弱信号
    WIFI_SIGNAL_LEVEL_2,            // 中等信号
    WIFI_SIGNAL_LEVEL_3,            // 强信号
    WIFI_SIGNAL_LEVEL_4             // 很强信号
} wifi_signal_level_t;

// WiFi扫描结果
typedef struct {
    char ssid[WIFI_SSID_MAX_LEN];   // SSID
    uint8_t bssid[6];               // BSSID
    uint8_t primary;                // 主频道
    wifi_second_chan_t second;      // 副频道
    int8_t rssi;                    // 信号强度
    wifi_auth_mode_t authmode;      // 认证模式
    bool is_hidden;                 // 是否隐藏
} wifi_ap_info_t;

// WiFi状态信息
typedef struct {
    wifi_state_t state;             // 当前状态
    wifi_config_mode_t config_mode; // 配网模式
    
    // 连接信息
    char ssid[WIFI_SSID_MAX_LEN];   // 已连接SSID
    uint8_t bssid[6];               // 已连接BSSID
    uint8_t channel;                // 频道
    int8_t rssi;                    // 信号强度
    wifi_signal_level_t signal_level; // 信号等级
    
    // 网络信息
    esp_netif_ip_info_t ip_info;    // IP信息
    uint8_t mac[6];                 // MAC地址
    
    // 统计信息
    uint32_t connect_count;         // 连接次数
    uint32_t disconnect_count;      // 断开次数
    uint32_t last_connect_time;     // 最后连接时间
    uint32_t total_connect_time;    // 总连接时间
    
    // 状态标志
    bool initialized;               // 初始化标志
    bool auto_reconnect;            // 自动重连标志
    bool credentials_saved;         // 凭据已保存
    
    // 错误信息
    esp_err_t last_error;           // 最后错误码
    uint8_t retry_count;            // 当前重试次数
    
    // 时间戳
    uint64_t last_update_time;      // 最后更新时间
} wifi_status_t;

// WiFi管理器结构
typedef struct {
    wifi_status_t status;           // WiFi状态
    esp_netif_t* sta_netif;        // STA网络接口
    esp_netif_t* ap_netif;         // AP网络接口
    
    // 配置信息
    wifi_config_t sta_config;      // STA配置
    wifi_config_t ap_config;       // AP配置
    
    // 扫描结果
    wifi_ap_info_t scan_results[WIFI_SCAN_MAX_AP]; // 扫描结果
    uint16_t scan_count;           // 扫描到的AP数量
    
    // 定时器
    esp_timer_handle_t reconnect_timer;    // 重连定时器
    esp_timer_handle_t config_timer;       // 配网超时定时器
    
    // 同步原语
    SemaphoreHandle_t wifi_mutex;          // WiFi操作互斥锁
    EventGroupHandle_t wifi_event_group;   // WiFi事件组
    
    // 回调函数
    esp_event_handler_instance_t sta_event_instance;  // STA事件句柄
    esp_event_handler_instance_t ip_event_instance;   // IP事件句柄
} wifi_manager_t;

// WiFi事件位定义
#define WIFI_CONNECTED_BIT          BIT0    // WiFi已连接
#define WIFI_FAIL_BIT               BIT1    // WiFi连接失败
#define WIFI_SMARTCONFIG_START_BIT  BIT2    // SmartConfig开始
#define WIFI_SMARTCONFIG_DONE_BIT   BIT3    // SmartConfig完成

// 函数声明
esp_err_t wifi_manager_init(void);
void wifi_manager_deinit(void);

// WiFi基本控制
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
esp_err_t wifi_manager_connect(const char* ssid, const char* password);
esp_err_t wifi_manager_disconnect(void);
esp_err_t wifi_manager_reconnect(void);

// 配网功能
esp_err_t wifi_manager_start_smartconfig(void);
esp_err_t wifi_manager_stop_smartconfig(void);
esp_err_t wifi_manager_start_ap_config(void);
esp_err_t wifi_manager_stop_ap_config(void);
esp_err_t wifi_manager_start_wps_config(void);

// WiFi扫描
esp_err_t wifi_manager_scan_start(void);
esp_err_t wifi_manager_scan_stop(void);
uint16_t wifi_manager_get_scan_results(wifi_ap_info_t* results, uint16_t max_count);

// 状态查询
wifi_status_t wifi_manager_get_status(void);
wifi_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_configuring(void);
int8_t wifi_manager_get_rssi(void);
wifi_signal_level_t wifi_manager_get_signal_level(void);

// 网络信息
esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t* ip_info);
esp_err_t wifi_manager_get_mac_address(uint8_t* mac);
esp_err_t wifi_manager_get_ap_info(wifi_ap_record_t* ap_info);

// 配置管理
esp_err_t wifi_manager_save_credentials(const char* ssid, const char* password);
esp_err_t wifi_manager_load_credentials(char* ssid, char* password);
esp_err_t wifi_manager_clear_credentials(void);
esp_err_t wifi_manager_set_auto_reconnect(bool enable);

// 高级功能
esp_err_t wifi_manager_set_country_code(const char* country);
esp_err_t wifi_manager_set_power_save_mode(wifi_ps_type_t mode);
esp_err_t wifi_manager_set_bandwidth(wifi_bandwidth_t bandwidth);

// 统计和诊断
void wifi_manager_dump_status(void);
esp_err_t wifi_manager_get_statistics(uint32_t* connect_count, uint32_t* disconnect_count);
esp_err_t wifi_manager_reset_statistics(void);

// 事件回调
typedef void (*wifi_event_callback_t)(wifi_state_t state, esp_err_t error);
esp_err_t wifi_manager_register_event_callback(wifi_event_callback_t callback);
esp_err_t wifi_manager_unregister_event_callback(void);

// 内部函数声明 - 部分未实现，保留用于后续开发
/*
static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                              int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, 
                            int32_t event_id, void* event_data);
static void wifi_reconnect_timer_callback(void* arg);
static void wifi_config_timeout_callback(void* arg);
static esp_err_t wifi_manager_apply_sta_config(void);
static void wifi_manager_update_signal_level(void);
static wifi_signal_level_t wifi_rssi_to_level(int8_t rssi);
*/

#endif // WIFI_MANAGER_H 