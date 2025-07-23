/*
 * OneNet MQTT Client Implementation for ESP32-S3
 * Based on mature Arduino implementation with industrial-grade features
 * 
 * Features:
 * - Secure token generation with HMAC-SHA256
 * - Automatic reconnection with intelligent retry
 * - Message throttling and flow control
 * - Property synchronization and event reporting
 * - Service invocation support
 */

#include "onenet_mqtt.h"
#include "smartjet_common.h"
#include "nvs_storage.h"
#include "sys_manager.h"
#include "event_system.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "cJSON.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char* TAG = "ONENET_MQTT";

// MQTT 服务器配置
#define MQTT_SERVER_URI "mqtts://mqttstls.heclouds.com:8883"
#define MQTT_KEEP_ALIVE 600
#define MQTT_BUFFER_SIZE 2048
#define MQTT_TASK_STACK_SIZE 6144
#define MIN_VALID_TIME 1584588588  // 2020年时间戳

// 全局状态变量
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool g_onenet_initialized = false;
static bool g_mqtt_connected = false;
static bool g_force_token_refresh = false;
static time_t g_last_token_time = 0;
static uint32_t g_message_id_counter = 0;

// 设备信息
static char g_product_id[32] = {0};
static char g_device_name[64] = {0};
static char g_device_key[256] = {0};

// 消息限流控制
typedef struct {
    uint32_t last_time;
    uint16_t count_per_sec;
    uint32_t last_sec;
} throttle_controller_t;

static throttle_controller_t g_global_throttle = {0};
static throttle_controller_t g_property_throttle = {0};

// 静态JSON缓冲区
static cJSON *g_static_json = NULL;
// static char g_static_payload[2048]; // 预留缓冲区，暂未使用
static char g_static_topic[256];

// 前向声明
static bool check_and_update_throttle(throttle_controller_t *ctrl, uint32_t now, 
                                     uint32_t interval, uint16_t max_per_sec);
static esp_err_t generate_secure_token(char *token_buf, size_t buf_size);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                              int32_t event_id, void *event_data);
static void process_mqtt_message(const char *topic, const char *data, int data_len);
static time_t get_current_time(void);
static void safe_wdt_reset(void);

/**
 * @brief URL编码函数
 */
static void url_encode(const char *str, char *encoded, size_t encoded_size)
{
    const char *hex_chars = "0123456789ABCDEF";
    size_t str_len = strlen(str);
    size_t j = 0;
    
    for (size_t i = 0; i < str_len && j < encoded_size - 1; i++) {
        char c = str[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded[j++] = c;
        } else {
            if (j < encoded_size - 3) {
                encoded[j++] = '%';
                encoded[j++] = hex_chars[(unsigned char)c >> 4];
                encoded[j++] = hex_chars[(unsigned char)c & 0x0F];
            }
        }
    }
    encoded[j] = '\0';
}

/**
 * @brief 获取当前时间
 */
static time_t get_current_time(void)
{
    time_t t = time(NULL);
    if (t < MIN_VALID_TIME) {
        ESP_LOGW(TAG, "系统时间未同步");
        return MIN_VALID_TIME + esp_timer_get_time() / 1000000;
    }
    return t;
}

/**
 * @brief 安全的看门狗重置 - 暂未使用
 */
/*
static void safe_wdt_reset(void)
{
    // ESP-IDF中通常不需要手动重置看门狗，任务调度器会自动处理
    vTaskDelay(pdMS_TO_TICKS(1));
}
*/

/**
 * @brief 生成安全Token
 */
static esp_err_t generate_secure_token(char *token_buf, size_t buf_size)
{
    if (!token_buf || buf_size < 512) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 基本参数检查
    if (strlen(g_product_id) < 4 || strlen(g_device_name) < 2 || strlen(g_device_key) < 16) {
        ESP_LOGE(TAG, "设备三元组参数无效");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char* version = "2018-10-31";
    char res[128];
    snprintf(res, sizeof(res), "products/%s", g_product_id);
    
    time_t current_time = get_current_time();
    time_t et = current_time + 86400; // 24小时后过期
    
    // 构建签名字符串
    char string_for_sign[512];
    snprintf(string_for_sign, sizeof(string_for_sign), 
             "%lld\nsha256\n%s\n%s", (long long)et, res, version);
    
    // Base64解码设备密钥
    unsigned char key_bin[32];
    size_t key_len = 0;
    int ret = mbedtls_base64_decode(key_bin, sizeof(key_bin), &key_len,
                                   (const unsigned char*)g_device_key, strlen(g_device_key));
    if (ret != 0) {
        ESP_LOGE(TAG, "设备密钥Base64解码失败: %d", ret);
        return ESP_ERR_INVALID_ARG;
    }
    
    // HMAC-SHA256计算
    unsigned char hmac_result[32];
    ret = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                         key_bin, key_len,
                         (const unsigned char*)string_for_sign, strlen(string_for_sign),
                         hmac_result);
    if (ret != 0) {
        ESP_LOGE(TAG, "HMAC计算失败: %d", ret);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Base64编码签名
    char sign_base64[64];
    size_t sign_len = 0;
    ret = mbedtls_base64_encode((unsigned char*)sign_base64, sizeof(sign_base64), 
                               &sign_len, hmac_result, sizeof(hmac_result));
    if (ret != 0) {
        ESP_LOGE(TAG, "签名Base64编码失败: %d", ret);
        return ESP_ERR_INVALID_ARG;
    }
    sign_base64[sign_len] = '\0';
    
    // URL编码资源和签名
    char res_encoded[256];
    char sign_encoded[128];
    url_encode(res, res_encoded, sizeof(res_encoded));
    url_encode(sign_base64, sign_encoded, sizeof(sign_encoded));
    
    // 生成最终Token
    snprintf(token_buf, buf_size,
             "version=%s&res=%s&et=%lld&method=sha256&sign=%s",
             version, res_encoded, (long long)et, sign_encoded);
    
    ESP_LOGI(TAG, "Token生成成功，有效期至: %lld", (long long)et);
    g_last_token_time = current_time;
    
    return ESP_OK;
}

/**
 * @brief 消息限流检查
 */
static bool check_and_update_throttle(throttle_controller_t *ctrl, uint32_t now,
                                     uint32_t interval, uint16_t max_per_sec)
{
    uint32_t now_sec = now / 1000;
    
    // 间隔检查
    if (now - ctrl->last_time < interval) {
        return false;
    }
    
    // 频率检查
    if (now_sec != ctrl->last_sec) {
        ctrl->count_per_sec = 0;
        ctrl->last_sec = now_sec;
    }
    if (ctrl->count_per_sec >= max_per_sec) {
        return false;
    }
    
    ctrl->last_time = now;
    ctrl->count_per_sec++;
    return true;
}

/**
 * @brief 处理MQTT消息
 */
static void process_mqtt_message(const char *topic, const char *data, int data_len)
{
    if (!topic || !data || data_len <= 0) {
        return;
    }
    
    ESP_LOGI(TAG, "收到消息 [%s] (%d bytes)", topic, data_len);
    
    // 消息限流
    uint32_t now = esp_timer_get_time() / 1000;
    if (!check_and_update_throttle(&g_global_throttle, now, 80, 12)) {
        ESP_LOGW(TAG, "消息被限流丢弃");
        return;
    }
    
    // 检查认证错误
    if (strstr(data, "authentication failed") || strstr(data, "10403")) {
        ESP_LOGW(TAG, "检测到认证错误，将刷新Token");
        g_force_token_refresh = true;
        return;
    }
    
    // 解析JSON
    if (g_static_json) {
        cJSON_Delete(g_static_json);
    }
    g_static_json = cJSON_ParseWithLength(data, data_len);
    if (!g_static_json) {
        ESP_LOGW(TAG, "JSON解析失败");
        return;
    }
    
    // 构建完整的topic字符串
    char full_topic[256];
    snprintf(full_topic, sizeof(full_topic), "$sys/%s/%s/thing/property/set", 
             g_product_id, g_device_name);
    
    // 属性设置处理
    if (strstr(topic, "/thing/property/set") && strcmp(topic, full_topic) == 0) {
        cJSON *params = cJSON_GetObjectItem(g_static_json, "params");
        if (cJSON_IsObject(params)) {
            // 处理onff参数（开关控制）
            cJSON *onff = cJSON_GetObjectItem(params, "onff");
            if (cJSON_IsBool(onff)) {
                bool device_on = cJSON_IsTrue(onff);
                ESP_LOGI(TAG, "收到开关指令: %s", device_on ? "ON" : "OFF");
                
                // 发送事件到引擎控制模块
                event_message_t event = {
                    .type = device_on ? EVENT_ENGINE_START_REQUEST : EVENT_ENGINE_STOP_REQUEST,
                    .data = NULL,
                    .data_len = 0,
                    .timestamp = esp_timer_get_time() / 1000
                };
                event_system_post(&event);
            }
            
            // 处理其他配置参数...
            // 这里可以添加更多参数处理逻辑
        }
        
        // 发送响应
        cJSON *id = cJSON_GetObjectItem(g_static_json, "id");
        cJSON *reply = cJSON_CreateObject();
        cJSON_AddStringToObject(reply, "id", cJSON_IsString(id) ? id->valuestring : "");
        cJSON_AddNumberToObject(reply, "code", 0);
        cJSON_AddStringToObject(reply, "msg", "success");
        cJSON_AddObjectToObject(reply, "data");
        
        char *reply_str = cJSON_PrintUnformatted(reply);
        if (reply_str) {
            snprintf(g_static_topic, sizeof(g_static_topic), 
                    "$sys/%s/%s/thing/property/set_reply", g_product_id, g_device_name);
            esp_mqtt_client_publish(mqtt_client, g_static_topic, reply_str, 0, 0, 0);
            free(reply_str);
        }
        cJSON_Delete(reply);
    }
    
    // 属性查询处理
    else if (strstr(topic, "/thing/property/get")) {
        cJSON *params = cJSON_GetObjectItem(g_static_json, "params");
        if (cJSON_IsArray(params)) {
            cJSON *reply = cJSON_CreateObject();
            cJSON *id = cJSON_GetObjectItem(g_static_json, "id");
            cJSON_AddStringToObject(reply, "id", cJSON_IsString(id) ? id->valuestring : "");
            cJSON_AddNumberToObject(reply, "code", 0);
            cJSON_AddStringToObject(reply, "msg", "success");
            
            cJSON *reply_data = cJSON_CreateObject();
            
            // 模拟一些基本属性数据
            cJSON *param_item = NULL;
            cJSON_ArrayForEach(param_item, params) {
                if (cJSON_IsString(param_item)) {
                    const char *param_name = param_item->valuestring;
                    
                    if (strcmp(param_name, "online") == 0) {
                        cJSON_AddBoolToObject(reply_data, "online", true);
                    } else if (strcmp(param_name, "version") == 0) {
                        cJSON_AddNumberToObject(reply_data, "version", 1);
                    } else if (strcmp(param_name, "rpm") == 0) {
                        cJSON_AddNumberToObject(reply_data, "rpm", 1800);
                    } else if (strcmp(param_name, "temp") == 0) {
                        cJSON_AddNumberToObject(reply_data, "temp", 25.5);
                    }
                    // 可以添加更多属性...
                }
            }
            
            cJSON_AddItemToObject(reply, "data", reply_data);
            
            char *reply_str = cJSON_PrintUnformatted(reply);
            if (reply_str) {
                snprintf(g_static_topic, sizeof(g_static_topic), 
                        "$sys/%s/%s/thing/property/get_reply", g_product_id, g_device_name);
                esp_mqtt_client_publish(mqtt_client, g_static_topic, reply_str, 0, 0, 0);
                free(reply_str);
            }
            cJSON_Delete(reply);
        }
    }
    
    // 服务调用处理
    else if (strstr(topic, "/thing/service/") && strstr(topic, "/invoke")) {
        // 提取服务标识符
        char *service_start = strstr(topic, "/thing/service/") + 15;
        char *service_end = strstr(service_start, "/invoke");
        if (service_end && service_end > service_start) {
            char service_name[64];
            size_t service_len = MIN(service_end - service_start, sizeof(service_name) - 1);
            strncpy(service_name, service_start, service_len);
            service_name[service_len] = '\0';
            
            ESP_LOGI(TAG, "收到服务调用: %s", service_name);
            
            // 处理重启服务
            if (strcmp(service_name, "remote_reboot") == 0) {
                cJSON *params = cJSON_GetObjectItem(g_static_json, "params");
                cJSON *reboot = cJSON_GetObjectItem(params, "reboot");
                cJSON *reboot_code = cJSON_GetObjectItem(params, "reboot_code");
                
                if (cJSON_IsTrue(reboot) && cJSON_IsString(reboot_code) && 
                    strcmp(reboot_code->valuestring, "tenkon123") == 0) {
                    
                    ESP_LOGW(TAG, "收到合法重启指令，系统将重启");
                    
                    // 发送响应
                    cJSON *reply = cJSON_CreateObject();
                    cJSON *id = cJSON_GetObjectItem(g_static_json, "id");
                    cJSON_AddStringToObject(reply, "id", cJSON_IsString(id) ? id->valuestring : "");
                    cJSON_AddNumberToObject(reply, "code", 0);
                    cJSON_AddStringToObject(reply, "msg", "rebooting");
                    cJSON_AddObjectToObject(reply, "data");
                    
                    char *reply_str = cJSON_PrintUnformatted(reply);
                    if (reply_str) {
                        snprintf(g_static_topic, sizeof(g_static_topic), 
                                "$sys/%s/%s/thing/service/%s/invoke_reply", 
                                g_product_id, g_device_name, service_name);
                        esp_mqtt_client_publish(mqtt_client, g_static_topic, reply_str, 0, 0, 0);
                        free(reply_str);
                    }
                    cJSON_Delete(reply);
                    
                    vTaskDelay(pdMS_TO_TICKS(1000)); // 等待消息发送
                    esp_restart();
                }
            }
        }
    }
}

/**
 * @brief MQTT事件处理器
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT连接成功");
        g_mqtt_connected = true;
        
        // 订阅主题
        struct {
            const char* topic_suffix;
            const char* description;
        } subscriptions[] = {
            {"/thing/property/set", "属性设置"},
            {"/thing/property/get", "属性查询"}, 
            {"/thing/service/+/invoke", "服务调用"},
            {"/thing/event/post/reply", "事件响应"},
            {"/thing/property/desired/get/reply", "期望属性响应"},
            {"/thing/property/desired/delete/reply", "期望属性删除响应"}
        };
        
        for (size_t i = 0; i < sizeof(subscriptions)/sizeof(subscriptions[0]); i++) {
            snprintf(g_static_topic, sizeof(g_static_topic), 
                    "$sys/%s/%s%s", g_product_id, g_device_name, subscriptions[i].topic_suffix);
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, g_static_topic, 0);
            ESP_LOGI(TAG, "订阅%s: %s (msg_id=%d)", subscriptions[i].description, 
                    g_static_topic, msg_id);
        }
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT连接断开");
        g_mqtt_connected = false;
        break;
        
    case MQTT_EVENT_DATA:
        if (event->topic_len > 0 && event->data_len > 0) {
            // 确保topic以null结尾
            char topic[256];
            size_t topic_len = MIN(event->topic_len, sizeof(topic) - 1);
            strncpy(topic, event->topic, topic_len);
            topic[topic_len] = '\0';
            
            process_mqtt_message(topic, event->data, event->data_len);
        }
        break;
        
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT错误事件");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "TCP传输错误: 0x%x", event->error_handle->esp_tls_last_esp_err);
        }
        break;
        
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "消息发布成功，msg_id=%d", event->msg_id);
        break;
        
    default:
        ESP_LOGD(TAG, "其他MQTT事件: %ld", event_id);
        break;
    }
}

/**
 * @brief 初始化OneNet MQTT
 */
esp_err_t onenet_mqtt_init(const onenet_config_t* config)
{
    if (g_onenet_initialized) {
        ESP_LOGW(TAG, "OneNet MQTT已初始化");
        return ESP_OK;
    }
    
    if (!config || strlen(config->product_id) == 0 || strlen(config->device_name) == 0 || strlen(config->device_key) == 0) {
        ESP_LOGE(TAG, "配置参数无效");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 保存设备信息
    strncpy(g_product_id, config->product_id, sizeof(g_product_id) - 1);
    strncpy(g_device_name, config->device_name, sizeof(g_device_name) - 1);
    strncpy(g_device_key, config->device_key, sizeof(g_device_key) - 1);
    
    ESP_LOGI(TAG, "初始化OneNet MQTT - 产品ID: %s, 设备名: %s", 
             g_product_id, g_device_name);
    
    g_onenet_initialized = true;
    return ESP_OK;
}

/**
 * @brief 反初始化OneNet MQTT
 */
void onenet_mqtt_deinit(void)
{
    if (!g_onenet_initialized) {
        return;
    }
    
    if (mqtt_client) {
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
    
    if (g_static_json) {
        cJSON_Delete(g_static_json);
        g_static_json = NULL;
    }
    
    g_mqtt_connected = false;
    g_onenet_initialized = false;
    
    ESP_LOGI(TAG, "OneNet MQTT已反初始化");
}

/**
 * @brief 启动MQTT连接
 */
esp_err_t onenet_mqtt_start(void)
{
    if (!g_onenet_initialized) {
        ESP_LOGE(TAG, "OneNet MQTT未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mqtt_client) {
        ESP_LOGW(TAG, "MQTT客户端已存在");
        return ESP_OK;
    }
    
    // 生成Token
    char token[512];
    esp_err_t ret = generate_secure_token(token, sizeof(token));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Token生成失败");
        return ret;
    }
    
    // 配置MQTT客户端
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_SERVER_URI,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.username = g_product_id,
        .credentials.authentication.password = token,
        .credentials.client_id = g_device_name,
        .session.keepalive = MQTT_KEEP_ALIVE,
        .buffer.size = MQTT_BUFFER_SIZE,
        .task.stack_size = MQTT_TASK_STACK_SIZE,
        .network.timeout_ms = 10000,
    };
    
    // 创建MQTT客户端
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) {
        ESP_LOGE(TAG, "MQTT客户端创建失败");
        return ESP_FAIL;
    }
    
    // 注册事件处理器
    ret = esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, 
                                        mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT事件注册失败");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return ret;
    }
    
    // 启动MQTT客户端
    ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT客户端启动失败");
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return ret;
    }
    
    ESP_LOGI(TAG, "MQTT客户端启动成功");
    return ESP_OK;
}

/**
 * @brief 停止MQTT连接
 */
esp_err_t onenet_mqtt_stop(void)
{
    if (!mqtt_client) {
        return ESP_OK;
    }
    
    esp_err_t ret = esp_mqtt_client_stop(mqtt_client);
    g_mqtt_connected = false;
    
    ESP_LOGI(TAG, "MQTT客户端已停止");
    return ret;
}

/**
 * @brief 上报属性数据
 */
esp_err_t onenet_mqtt_report_properties(const onenet_property_data_t* data)
{
    if (!g_mqtt_connected || !data) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 消息限流
    uint32_t now = esp_timer_get_time() / 1000;
    if (!check_and_update_throttle(&g_property_throttle, now, 150, 5)) {
        ESP_LOGW(TAG, "属性上报被限流");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建JSON消息
    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    
    char msg_id[32];
    snprintf(msg_id, sizeof(msg_id), "%lu", ++g_message_id_counter);
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddStringToObject(root, "version", "1.0");
    
    // 添加属性数据
    uint64_t timestamp = (uint64_t)get_current_time() * 1000ULL;
    
    // 基本属性
    cJSON *online_obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(online_obj, "value", data->online);
    cJSON_AddNumberToObject(online_obj, "time", timestamp);
    cJSON_AddItemToObject(params, "online", online_obj);
    
    cJSON *version_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(version_obj, "value", data->version);
    cJSON_AddNumberToObject(version_obj, "time", timestamp);
    cJSON_AddItemToObject(params, "version", version_obj);
    
    cJSON *rpm_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(rpm_obj, "value", data->rpm);
    cJSON_AddNumberToObject(rpm_obj, "time", timestamp);
    cJSON_AddItemToObject(params, "rpm", rpm_obj);
    
    cJSON *temp_obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(temp_obj, "value", data->temperature);
    cJSON_AddNumberToObject(temp_obj, "time", timestamp);
    cJSON_AddItemToObject(params, "temp", temp_obj);
    
    cJSON_AddItemToObject(root, "params", params);
    
    // 生成JSON字符串
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "JSON生成失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发布消息
    snprintf(g_static_topic, sizeof(g_static_topic), 
            "$sys/%s/%s/thing/property/post", g_product_id, g_device_name);
    
    int msg_id_int = esp_mqtt_client_publish(mqtt_client, g_static_topic, 
                                           json_string, 0, 0, 0);
    
    if (msg_id_int < 0) {
        ESP_LOGE(TAG, "属性上报失败");
        free(json_string);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "属性上报成功: %s", json_string);
    free(json_string);
    
    return ESP_OK;
}

/**
 * @brief 上报事件
 */
esp_err_t onenet_mqtt_report_event(const onenet_event_data_t* event)
{
    if (!g_mqtt_connected || !event || strlen(event->event_id) == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建JSON消息
    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();
    cJSON *event_obj = cJSON_CreateObject();
    cJSON *value_obj = cJSON_CreateObject();
    
    char msg_id[32];
    snprintf(msg_id, sizeof(msg_id), "%lu", ++g_message_id_counter);
    cJSON_AddStringToObject(root, "id", msg_id);
    cJSON_AddStringToObject(root, "version", "1.0");
    
    // 添加事件数据
    if (event->event_data) {
        cJSON *event_data_json = cJSON_Parse(event->event_data);
        if (event_data_json) {
            cJSON_AddItemToObject(value_obj, "data", event_data_json);
        }
    }
    
    uint64_t timestamp = (uint64_t)get_current_time() * 1000ULL;
    cJSON_AddItemToObject(event_obj, "value", value_obj);
    cJSON_AddNumberToObject(event_obj, "time", timestamp);
    cJSON_AddItemToObject(params, event->event_id, event_obj);
    cJSON_AddItemToObject(root, "params", params);
    
    // 生成JSON字符串
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_string) {
        ESP_LOGE(TAG, "事件JSON生成失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 发布消息
    snprintf(g_static_topic, sizeof(g_static_topic), 
            "$sys/%s/%s/thing/event/post", g_product_id, g_device_name);
    
    int msg_id_int = esp_mqtt_client_publish(mqtt_client, g_static_topic, 
                                           json_string, 0, 0, 0);
    
    if (msg_id_int < 0) {
        ESP_LOGE(TAG, "事件上报失败");
        free(json_string);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "事件上报成功: %s", json_string);
    free(json_string);
    
    return ESP_OK;
}

/**
 * @brief 获取连接状态
 */
onenet_status_t onenet_mqtt_get_status(void)
{
    onenet_status_t status = {0};
    
    if (g_mqtt_connected) {
        status.state = ONENET_STATE_CONNECTED;
        status.authenticated = true;
    } else if (mqtt_client) {
        status.state = ONENET_STATE_CONNECTING;
        status.authenticated = false;
    } else {
        status.state = ONENET_STATE_DISCONNECTED;
        status.authenticated = false;
    }
    
    return status;
} 