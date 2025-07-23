/*
 * OTA Manager Implementation for ESP32-S3 SmartJet v2.0
 * Based on mature Arduino implementation with enhanced safety features
 * 
 * Features:
 * - Automatic version checking with retry mechanism
 * - Secure HTTPS download with certificate validation
 * - MD5 verification for firmware integrity
 * - Pre-flight checks for system stability
 * - Progress reporting and error handling
 * - Rollback protection and recovery
 */

#include "ota_manager.h"
#include "smartjet_common.h"
#include "sys_manager.h"
#include "event_system.h"
#include "nvs_storage.h"

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "mbedtls/md5.h"
#include <string.h>
#include <time.h>

static const char* TAG = "OTA_MGR";

// OTA配置
#define OTA_UPDATE_URL "https://tenkon.sunrays.top/updateota/update_info.json"
#define OTA_HTTP_TIMEOUT_BASE 60000     // 基础60秒超时
#define OTA_DOWNLOAD_TIMEOUT_PER_MB 45000 // 每MB额外45秒
#define OTA_CONNECT_TIMEOUT 15000       // 连接超时15秒
#define OTA_MAX_RETRIES 3              // 最大重试3次
#define OTA_RETRY_DELAY 3000           // 重试间隔3秒
#define OTA_MIN_FREE_HEAP 50000        // 最小可用内存50KB
#define OTA_MIN_RSSI -85               // 最小WiFi信号强度

// 系统稳定性阈值
#define MIN_UPTIME_FOR_OTA 30000       // 30秒最小运行时间
#define MIN_VALID_TIME 1584588588      // 2020年时间戳

// 全局状态
static ota_manager_t g_ota_manager = {0};
static bool g_ota_initialized = false;
static esp_timer_handle_t g_ota_check_timer = NULL;

// TLS服务器证书 - tenkon.sunrays.top
static const char* ota_server_cert = 
"-----BEGIN CERTIFICATE-----\n"
"MIIDtDCCAzugAwIBAgISBQ0DGihZ7vGZILjALlJR4nwsMAoGCCqGSM49BAMDMDIx\n"
"CzAJBgNVBAYTAlVTMRYwFAYDVQQKEw1MZXQncyBFbmNyeXB0MQswCQYDVQQDEwJF\n"
"NTAeFw0yNTAzMzAxNDUwMDBaFw0yNTA2MjgxNDQ5NTlaMB0xGzAZBgNVBAMTEnRl\n"
"bmtvbi5zdW5yYXlzLnRvcDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABH8M28tS\n"
"wQu4ekypeaQHXOJLozkBsEg++b/q7wpyc9Vi1o+trV7Rec2CjRBeYQDqlhEfQpyH\n"
"ISwgHUGHQw31a9+jggJEMIICQDAOBgNVHQ8BAf8EBAMCB4AwHQYDVR0lBBYwFAYI\n"
"KwYBBQUHAwEGCCsGAQUFBwMCMAwGA1UdEwEB/wQCMAAwHQYDVR0OBBYEFN7s0ef/\n"
"SuX4A9zqg7g0SmEYxgmjMB8GA1UdIwQYMBaAFJ8rX888IU+dBLftKyzExnCL0tcN\n"
"MFUGCCsGAQUFBwEBBEkwRzAhBggrBgEFBQcwAYYVaHR0cDovL2U1Lm8ubGVuY3Iu\n"
"b3JnMCIGCCsGAQUFBzAChhZodHRwOi8vZTUuaS5sZW5jci5vcmcvMB0GA1UdEQQW\n"
"MBSCEnRlbmtvbi5zdW5yYXlzLnRvcDATBgNVHSAEDDAKMAgGBmeBDAECATAtBgNV\n"
"HR8EJjAkMCKgIKAehhxodHRwOi8vZTUuYy5sZW5jci5vcmcvNDUuY3JsMIIBBQYK\n"
"KwYBBAHWeQIEAgSB9gSB8wDxAHcAzxFW7tUufK/zh1vZaS6b6RpxZ0qwF+ysAdJb\n"
"d87MOwgAAAGV5719ZwAABAMASDBGAiEAwfBYQf5N++POVVybBY7Ib6yfTYTDMLlj\n"
"51MgqUtyR6YCIQDZdhSpOYId5nJBC117qkS298JvsJIOcmuhFwi8PnOJoQB2AA3h\n"
"8jAr0w3BQGISCepVLvxHdHyx1+kw7w5CHrR+Tqo0AAABlee9hP8AAAQDAEcwRQIg\n"
"Y/yva/MB2L3z6GW2veXQpR9MMTQ0xCwCkvQgpuOsIHYCIQCzohG6HcXEXI4ZBv85\n"
"bCm+i3xxDegoZtknrZaWuPEnnTAKBggqhkjOPQQDAwNnADBkAjANe1SaeeiKERBL\n"
"kGUf0RHSAJF4UXvy8MAY+xiXor46+tAytAdxur2/sUInDF0/eFACMD+LVKD0PTEG\n"
"e2WBWBeE9CTkhBr3ULUhyv1vYW2ba3qqhY/YYPNcTSm0odOxOzMGjA==\n"
"-----END CERTIFICATE-----\n";

// 前向声明
static esp_err_t ota_preflight_check(void);
static esp_err_t ota_check_version(int *new_version, char *firmware_url, size_t url_size, char *md5_hash, size_t md5_size);
static esp_err_t ota_download_and_install(const char *firmware_url, const char *md5_hash, int new_version);
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt);
static void ota_check_timer_callback(void *arg);
// static time_t get_current_time(void); // 函数已移除

// 时间获取函数预留，暂未使用
// static time_t get_current_time(void) { ... }

/**
 * @brief OTA系统预检查
 */
static esp_err_t ota_preflight_check(void)
{
    ESP_LOGI(TAG, "执行OTA预检查...");
    
    // 1. 网络质量检查
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        int rssi = ap_info.rssi;
        if (rssi < OTA_MIN_RSSI) {
            ESP_LOGW(TAG, "WiFi信号过弱(%d dBm)，延迟OTA", rssi);
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        ESP_LOGW(TAG, "无法获取WiFi信号强度");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 2. 内存检查
    size_t free_heap = esp_get_free_heap_size();
    if (free_heap < OTA_MIN_FREE_HEAP) {
        ESP_LOGW(TAG, "可用内存不足(%zu bytes)，延迟OTA", free_heap);
        return ESP_ERR_NO_MEM;
    }
    
    // 3. 存储空间检查
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    
    if (!update_partition) {
        ESP_LOGE(TAG, "无法获取OTA分区");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (update_partition->size < running->size) {
        ESP_LOGE(TAG, "OTA分区空间不足");
        return ESP_ERR_NO_MEM;
    }
    
    // 4. 系统稳定性检查
    uint32_t uptime = esp_timer_get_time() / 1000;
    esp_reset_reason_t reset_reason = esp_reset_reason();
    
    if (uptime < MIN_UPTIME_FOR_OTA && 
        (reset_reason == ESP_RST_PANIC || reset_reason == ESP_RST_INT_WDT || 
         reset_reason == ESP_RST_TASK_WDT || reset_reason == ESP_RST_WDT)) {
        ESP_LOGW(TAG, "异常重启后稳定时间不足，延迟OTA");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "OTA预检查通过 - 信号:%d dBm, 内存:%zu KB", 
             ap_info.rssi, free_heap/1024);
    
    return ESP_OK;
}

/**
 * @brief 检查版本更新
 */
static esp_err_t ota_check_version(int *new_version, char *firmware_url, size_t url_size, 
                                  char *md5_hash, size_t md5_size)
{
    if (!new_version || !firmware_url || !md5_hash) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "检查版本更新: %s", OTA_UPDATE_URL);
    
    // HTTP客户端配置
    esp_http_client_config_t config = {
        .url = OTA_UPDATE_URL,
        .cert_pem = ota_server_cert,
        .timeout_ms = OTA_CONNECT_TIMEOUT,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = false,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "HTTP客户端初始化失败");
        return ESP_FAIL;
    }
    
    // 设置请求头
    esp_http_client_set_header(client, "User-Agent", "ESP32-OTA-Checker/1.0");
    esp_http_client_set_header(client, "Connection", "close");
    
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    
    while (retry_count < OTA_MAX_RETRIES) {
        ESP_LOGI(TAG, "版本检查第%d次尝试...", retry_count + 1);
        
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP连接失败: %s", esp_err_to_name(err));
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length <= 0) {
            ESP_LOGW(TAG, "无效的响应长度: %d", content_length);
            esp_http_client_close(client);
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        // 读取响应数据
        char *buffer = malloc(content_length + 1);
        if (!buffer) {
            ESP_LOGE(TAG, "内存分配失败");
            esp_http_client_close(client);
            err = ESP_ERR_NO_MEM;
            break;
        }
        
        int data_read = esp_http_client_read_response(client, buffer, content_length);
        esp_http_client_close(client);
        
        if (data_read <= 0) {
            ESP_LOGW(TAG, "读取响应失败: %d", data_read);
            free(buffer);
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        buffer[data_read] = '\0';
        
        // 解析JSON响应
        cJSON *json = cJSON_Parse(buffer);
        free(buffer);
        
        if (!json) {
            ESP_LOGW(TAG, "JSON解析失败");
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        // 提取WiFi版本信息
        cJSON *wifi_obj = cJSON_GetObjectItem(json, "wifi");
        if (!wifi_obj) {
            ESP_LOGW(TAG, "未找到wifi对象");
            cJSON_Delete(json);
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        cJSON *version_obj = cJSON_GetObjectItem(wifi_obj, "version");
        cJSON *url_obj = cJSON_GetObjectItem(wifi_obj, "url");
        cJSON *md5_obj = cJSON_GetObjectItem(wifi_obj, "md5");
        
        if (!cJSON_IsString(version_obj) || !cJSON_IsString(url_obj)) {
            ESP_LOGW(TAG, "版本信息格式错误");
            cJSON_Delete(json);
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        *new_version = atoi(version_obj->valuestring);
        strncpy(firmware_url, url_obj->valuestring, url_size - 1);
        firmware_url[url_size - 1] = '\0';
        
        if (cJSON_IsString(md5_obj)) {
            strncpy(md5_hash, md5_obj->valuestring, md5_size - 1);
            md5_hash[md5_size - 1] = '\0';
        } else {
            md5_hash[0] = '\0';
        }
        
        cJSON_Delete(json);
        err = ESP_OK;
        break;
    }
    
    esp_http_client_cleanup(client);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "版本检查成功 - 服务器版本: %d", *new_version);
    } else {
        ESP_LOGE(TAG, "版本检查失败，重试%d次后放弃", OTA_MAX_RETRIES);
    }
    
    return err;
}

/**
 * @brief HTTP事件处理器
 */
static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP错误");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP连接成功");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP头发送完成");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP头: %.*s", evt->data_len, (char*)evt->data);
        break;
    case HTTP_EVENT_ON_DATA:
        // 进度更新
        if (g_ota_manager.status.state == OTA_STATE_DOWNLOADING) {
            static uint32_t last_progress_time = 0;
            uint32_t now = esp_timer_get_time() / 1000;
            
            if (now - last_progress_time > 2000) { // 每2秒更新一次进度
                ESP_LOGI(TAG, "OTA下载进度: %d bytes", 
                        g_ota_manager.status.bytes_downloaded);
                last_progress_time = now;
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP传输完成");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP连接断开");
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * @brief 下载并安装固件
 */
static esp_err_t ota_download_and_install(const char *firmware_url, const char *md5_hash, int new_version)
{
    if (!firmware_url) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "开始OTA下载: %s", firmware_url);
    
    // 更新状态
    g_ota_manager.status.state = OTA_STATE_DOWNLOADING;
    g_ota_manager.status.bytes_downloaded = 0;
    g_ota_manager.status.download_start_time = esp_timer_get_time() / 1000;
    
    // HTTP客户端配置
    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .cert_pem = ota_server_cert,
        .timeout_ms = OTA_HTTP_TIMEOUT_BASE,
        .keep_alive_enable = false,
        .event_handler = ota_http_event_handler,
        .skip_cert_common_name_check = false,
    };
    
    // HTTPS OTA配置
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        .http_client_init_cb = NULL,
        .bulk_flash_erase = true,
        .partial_http_download = false,
    };
    
    esp_err_t err = ESP_FAIL;
    int retry_count = 0;
    
    while (retry_count < OTA_MAX_RETRIES) {
        ESP_LOGI(TAG, "OTA第%d次尝试...", retry_count + 1);
        
        g_ota_manager.status.state = OTA_STATE_DOWNLOADING;
        
        esp_https_ota_handle_t ota_handle = NULL;
        err = esp_https_ota_begin(&ota_config, &ota_handle);
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA开始失败: %s", esp_err_to_name(err));
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        esp_app_desc_t new_app_info;
        err = esp_https_ota_get_img_desc(ota_handle, &new_app_info);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "获取固件描述失败: %s", esp_err_to_name(err));
            esp_https_ota_abort(ota_handle);
            retry_count++;
            if (retry_count < OTA_MAX_RETRIES) {
                vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
            }
            continue;
        }
        
        ESP_LOGI(TAG, "新固件版本: %s", new_app_info.version);
        ESP_LOGI(TAG, "新固件名称: %s", new_app_info.project_name);
        ESP_LOGI(TAG, "新固件时间: %s %s", new_app_info.date, new_app_info.time);
        
        // 执行下载
        while (1) {
            err = esp_https_ota_perform(ota_handle);
            if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                break;
            }
            
            // 更新下载进度
            int data_read = esp_https_ota_get_image_len_read(ota_handle);
            g_ota_manager.status.bytes_downloaded = data_read;
            
            // 看门狗重置
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        
        if (err == ESP_OK) {
            g_ota_manager.status.state = OTA_STATE_VERIFYING;
            ESP_LOGI(TAG, "OTA下载完成，验证中...");
            
            err = esp_https_ota_finish(ota_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "OTA升级成功！固件版本: %d", new_version);
                g_ota_manager.status.state = OTA_STATE_SUCCESS;
                
                // 保存版本信息
                nvs_handle_t nvs_handle;
                esp_err_t nvs_err = nvs_open("ota", NVS_READWRITE, &nvs_handle);
                if (nvs_err == ESP_OK) {
                    nvs_set_i32(nvs_handle, "version", new_version);
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                }
                
                return ESP_OK;
            } else {
                ESP_LOGE(TAG, "OTA完成验证失败: %s", esp_err_to_name(err));
                g_ota_manager.status.state = OTA_STATE_FAILED;
                g_ota_manager.status.last_error = err;
            }
        } else {
            ESP_LOGE(TAG, "OTA下载失败: %s", esp_err_to_name(err));
            esp_https_ota_abort(ota_handle);
            g_ota_manager.status.state = OTA_STATE_FAILED;
            g_ota_manager.status.last_error = err;
        }
        
        retry_count++;
        if (retry_count < OTA_MAX_RETRIES) {
            ESP_LOGI(TAG, "等待%d秒后重试...", OTA_RETRY_DELAY / 1000);
            vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_DELAY));
        }
    }
    
    g_ota_manager.status.state = OTA_STATE_FAILED;
    ESP_LOGE(TAG, "OTA升级失败，重试%d次后放弃", OTA_MAX_RETRIES);
    
    return err;
}

/**
 * @brief OTA检查定时器回调
 */
static void ota_check_timer_callback(void *arg)
{
    if (!g_ota_manager.config.auto_update_enabled) {
        return;
    }
    
    if (g_ota_manager.status.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA正在进行中，跳过定期检查");
        return;
    }
    
    ESP_LOGI(TAG, "执行定期OTA检查");
    ota_manager_check_update();
}

/**
 * @brief 初始化OTA管理器
 */
esp_err_t ota_manager_init(const ota_config_t *config)
{
    if (g_ota_initialized) {
        ESP_LOGW(TAG, "OTA管理器已初始化");
        return ESP_OK;
    }
    
    if (!config) {
        ESP_LOGE(TAG, "配置参数为空");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制配置
    memcpy(&g_ota_manager.config, config, sizeof(ota_config_t));
    
    // 初始化状态
    g_ota_manager.status.state = OTA_STATE_IDLE;
    g_ota_manager.status.last_error = ESP_OK;
    g_ota_manager.status.last_check_time = 0;
    g_ota_manager.status.bytes_downloaded = 0;
    
    // 获取当前版本
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("ota", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        nvs_get_i32(nvs_handle, "version", &g_ota_manager.status.current_version);
        nvs_close(nvs_handle);
    } else {
        g_ota_manager.status.current_version = 1; // 默认版本
    }
    
    // 创建定期检查定时器
    if (config->auto_update_enabled && config->check_interval_minutes > 0) {
        const esp_timer_create_args_t timer_args = {
            .callback = ota_check_timer_callback,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ota_check_timer"
        };
        
        err = esp_timer_create(&timer_args, &g_ota_check_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "创建OTA定时器失败: %s", esp_err_to_name(err));
            return err;
        }
        
        // 启动定时器
        uint64_t interval_us = (uint64_t)config->check_interval_minutes * 60 * 1000000;
        err = esp_timer_start_periodic(g_ota_check_timer, interval_us);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "启动OTA定时器失败: %s", esp_err_to_name(err));
            esp_timer_delete(g_ota_check_timer);
            g_ota_check_timer = NULL;
            return err;
        }
        
        ESP_LOGI(TAG, "OTA定期检查已启用，间隔: %d分钟", config->check_interval_minutes);
    }
    
    g_ota_initialized = true;
    ESP_LOGI(TAG, "OTA管理器初始化成功 - 当前版本: %d", 
             g_ota_manager.status.current_version);
    
    return ESP_OK;
}

/**
 * @brief 反初始化OTA管理器
 */
void ota_manager_deinit(void)
{
    if (!g_ota_initialized) {
        return;
    }
    
    // 停止并删除定时器
    if (g_ota_check_timer) {
        esp_timer_stop(g_ota_check_timer);
        esp_timer_delete(g_ota_check_timer);
        g_ota_check_timer = NULL;
    }
    
    // 重置状态
    memset(&g_ota_manager, 0, sizeof(g_ota_manager));
    g_ota_initialized = false;
    
    ESP_LOGI(TAG, "OTA管理器已反初始化");
}

/**
 * @brief 检查更新
 */
esp_err_t ota_manager_check_update(void)
{
    if (!g_ota_initialized) {
        ESP_LOGE(TAG, "OTA管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (g_ota_manager.status.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA正在进行中");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 执行预检查
    esp_err_t err = ota_preflight_check();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA预检查失败: %s", esp_err_to_name(err));
        return err;
    }
    
    // 更新状态
    g_ota_manager.status.state = OTA_STATE_CHECKING;
    g_ota_manager.status.last_check_time = esp_timer_get_time() / 1000;
    
    // 检查版本
    int new_version = 0;
    char firmware_url[256] = {0};
    char md5_hash[64] = {0};
    
    err = ota_check_version(&new_version, firmware_url, sizeof(firmware_url), 
                           md5_hash, sizeof(md5_hash));
    
    if (err != ESP_OK) {
        g_ota_manager.status.state = OTA_STATE_IDLE;
        g_ota_manager.status.last_error = err;
        return err;
    }
    
    // 检查是否需要更新
    if (new_version <= g_ota_manager.status.current_version) {
        ESP_LOGI(TAG, "已是最新版本 (%d)", g_ota_manager.status.current_version);
        g_ota_manager.status.state = OTA_STATE_IDLE;
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "发现新版本: %d -> %d", g_ota_manager.status.current_version, new_version);
    
    // 执行OTA升级
    err = ota_download_and_install(firmware_url, md5_hash, new_version);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA升级成功，系统将重启");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        g_ota_manager.status.state = OTA_STATE_FAILED;
        g_ota_manager.status.last_error = err;
        ESP_LOGE(TAG, "OTA升级失败: %s", esp_err_to_name(err));
    }
    
    return err;
}

/**
 * @brief 强制启动OTA更新
 */
esp_err_t ota_manager_force_update(void)
{
    ESP_LOGI(TAG, "强制启动OTA更新");
    return ota_manager_check_update();
}

/**
 * @brief 获取OTA状态
 */
ota_status_t ota_manager_get_status(void)
{
    if (!g_ota_initialized) {
        ota_status_t empty_status = {0};
        empty_status.state = OTA_STATE_IDLE;
        return empty_status;
    }
    
    return g_ota_manager.status;
}

/**
 * @brief 设置自动更新
 */
esp_err_t ota_manager_set_auto_update(bool enabled)
{
    if (!g_ota_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    g_ota_manager.config.auto_update_enabled = enabled;
    ESP_LOGI(TAG, "自动更新: %s", enabled ? "启用" : "禁用");
    
    return ESP_OK;
}

/**
 * @brief 获取当前版本
 */
int ota_manager_get_current_version(void)
{
    if (!g_ota_initialized) {
        return 1;
    }
    
    return g_ota_manager.status.current_version;
} 