/**
 * @file onenet_mqtt.h
 * @brief OneNet MQTT云通信模块 - 基于成熟ino代码重构
 * @author SmartJet Team
 * @date 2024
 */

#ifndef ONENET_MQTT_H
#define ONENET_MQTT_H

#include "smartjet_common.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// OneNet MQTT服务器配置（基于原始ino代码的准确配置）
#define ONENET_MQTT_SERVER          "mqttstls.heclouds.com"
#define ONENET_MQTT_PORT            8883                 // TLS端口
#define ONENET_MQTT_KEEPALIVE       600                  // 心跳间隔(秒)
#define ONENET_MQTT_TIMEOUT         10000                // 连接超时(毫秒)
#define ONENET_MQTT_BUFFER_SIZE     2048                 // 消息缓冲区大小

// 认证配置
#define ONENET_TOKEN_VERSION        "2018-10-31"         // Token版本
#define ONENET_REMOTE_REBOOT_CODE   "tenkon123"          // 远程重启密码

// 连接状态枚举
typedef enum {
    ONENET_STATE_DISCONNECTED = 0,     // 未连接
    ONENET_STATE_CONNECTING,           // 连接中
    ONENET_STATE_CONNECTED,            // 已连接
    ONENET_STATE_RECONNECTING,         // 重连中
    ONENET_STATE_ERROR                 // 错误状态
} onenet_state_t;

// OneNet设备配置结构
typedef struct {
    char product_id[32];               // 产品ID
    char device_name[64];              // 设备名称
    char device_key[256];              // 设备密钥（Base64编码）
    
    uint16_t port;                     // MQTT端口
    uint16_t keepalive;                // 心跳间隔（秒）
    uint32_t timeout_ms;               // 连接超时（毫秒）
    
    bool auto_reconnect;               // 自动重连
    uint32_t reconnect_interval_ms;    // 重连间隔
} onenet_config_t;

// OneNet连接状态
typedef struct {
    onenet_state_t state;              // 连接状态
    bool authenticated;                // 是否已认证
    
    // 连接统计
    uint32_t connect_count;            // 连接次数
    uint32_t disconnect_count;         // 断开次数
    uint32_t last_connect_time;        // 最后连接时间
    
    // 消息统计
    uint32_t msg_sent_count;           // 发送消息数
    uint32_t msg_received_count;       // 接收消息数
    
    // 错误信息
    esp_err_t last_error;              // 最后错误码
    uint64_t last_update_time;         // 最后更新时间
} onenet_status_t;

// 属性数据结构（用于上报）
typedef struct {
    // 基本状态
    bool online;                       // 在线状态
    int version;                       // 固件版本
    
    // 发动机数据
    bool engine_running;               // 发动机运行状态
    uint32_t rpm;                      // 发动机转速
    float temperature;                 // 温度
    bool oil_alarm;                    // 机油报警
    
    // 网络状态
    int wifi_rssi;                     // WiFi信号强度
    
    // 时间戳
    uint64_t timestamp;                // 时间戳（毫秒）
} onenet_property_data_t;

// 事件数据结构
typedef struct {
    char event_id[64];                 // 事件标识符
    char *event_data;                  // 事件数据（JSON字符串）
    uint64_t timestamp;                // 时间戳
} onenet_event_data_t;

// 服务调用数据结构
typedef struct {
    char service_id[64];               // 服务标识符
    char *params;                      // 参数（JSON字符串）
    char *response;                    // 响应（JSON字符串）
    size_t response_size;              // 响应缓冲区大小
} onenet_service_data_t;

// 回调函数类型定义
typedef void (*onenet_property_set_cb_t)(const char* property, const char* value);
typedef void (*onenet_service_invoke_cb_t)(const onenet_service_data_t* service);
typedef void (*onenet_connection_cb_t)(onenet_state_t state, esp_err_t error);

/**
 * @brief 初始化OneNet MQTT客户端
 * 
 * @param config 配置参数
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t onenet_mqtt_init(const onenet_config_t* config);

/**
 * @brief 反初始化OneNet MQTT客户端
 */
void onenet_mqtt_deinit(void);

/**
 * @brief 启动MQTT连接
 * 
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 状态无效
 *         - ESP_FAIL: 启动失败
 */
esp_err_t onenet_mqtt_start(void);

/**
 * @brief 停止MQTT连接
 * 
 * @return esp_err_t 
 *         - ESP_OK: 成功
 */
esp_err_t onenet_mqtt_stop(void);

/**
 * @brief 上报属性数据
 * 
 * @param data 属性数据
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 未连接
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_FAIL: 发送失败
 */
esp_err_t onenet_mqtt_report_properties(const onenet_property_data_t* data);

/**
 * @brief 上报事件
 * 
 * @param event 事件数据
 * @return esp_err_t 
 *         - ESP_OK: 成功
 *         - ESP_ERR_INVALID_STATE: 未连接
 *         - ESP_ERR_INVALID_ARG: 参数无效
 *         - ESP_FAIL: 发送失败
 */
esp_err_t onenet_mqtt_report_event(const onenet_event_data_t* event);

/**
 * @brief 获取连接状态
 * 
 * @return onenet_status_t 当前状态
 */
onenet_status_t onenet_mqtt_get_status(void);

/**
 * @brief 设置属性设置回调函数
 * 
 * @param callback 回调函数
 */
void onenet_mqtt_set_property_callback(onenet_property_set_cb_t callback);

/**
 * @brief 设置服务调用回调函数
 * 
 * @param callback 回调函数
 */
void onenet_mqtt_set_service_callback(onenet_service_invoke_cb_t callback);

/**
 * @brief 设置连接状态回调函数
 * 
 * @param callback 回调函数
 */
void onenet_mqtt_set_connection_callback(onenet_connection_cb_t callback);

/**
 * @brief 检查是否已连接
 * 
 * @return true 已连接
 * @return false 未连接
 */
bool onenet_mqtt_is_connected(void);

/**
 * @brief 强制刷新Token（用于认证错误恢复）
 */
void onenet_mqtt_refresh_token(void);

#ifdef __cplusplus
}
#endif

#endif // ONENET_MQTT_H 