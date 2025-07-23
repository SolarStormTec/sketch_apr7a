// ================================================================================
// TENKON_TEC ESP32 工业级发动机控制系统 - 重构优化版本 202506121255
// 架构：状态机驱动 + 事件处理 + 分层设计
// 优化：内存安全 + 网络容错 + 工业级稳定性
// ================================================================================

// =====================================================d:\dicom\pythonProject8\sketch_jun13a\partitions.csv===========================
// 编译时调试开关 - 生c:\Users\ASUS\Documents\Arduino\sketch_jun13a\partitions.csv产环境请将DEBUG_MODE设为false
// ================================================================================
// 使用说明：
// - 调试模式（DEBUG_MODE = true）：启用所有串口输出，便于开发调试
// - 生产模式（DEBUG_MODE = false）：完全禁用串口输出，避免缓冲区溢出和性能损耗
// - 优势：编译时决定，无运行时开销，代码更精简
// ================================================================================
#define DEBUG_MODE 1   // 🔧 生产环境必须设为0！

#if DEBUG_MODE
  #define DEBUG_INIT()        Serial.begin(115200)
  #define DEBUG_PRINT(x)      Serial.print(x)
  #define DEBUG_PRINTLN(x)    Serial.println(x)
  #define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  // 生产模式：完全禁用串口和所有系统日志，零性能开销
  #define DEBUG_INIT()        do { \
                                Serial.end(); \
                                esp_log_level_set("*", ESP_LOG_NONE); \
                              } while(0)
  #define DEBUG_PRINT(x)      ((void)0)
  #define DEBUG_PRINTLN(x)    ((void)0)
  #define DEBUG_PRINTF(...)   ((void)0)
#endif

#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include <Preferences.h>
#include <WebServer.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/semphr.h"
#include "FFat.h"
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_log.h>

// ================================================================================
// 系统常量定义 - 统一管理所有配置
// ================================================================================
namespace Config {
    // 硬件引脚
    constexpr uint8_t BOOT_BUTTON_PIN = 15;
    constexpr uint8_t ENGINE_CONTROL_PIN = 27;
    constexpr uint8_t ENGINE_OIL_STATUS_PIN = 14;
    constexpr uint8_t ENGINE_PULSE_PIN = 36;  // 注意：GPIO36是只输入引脚，无内部上拉电阻
    constexpr uint8_t LED_RED_PIN = 17;
    constexpr uint8_t LED_GREEN_PIN = 16;
    
    // 🔧 新增：电源监控引脚（可选功能，需要硬件支持）
    constexpr uint8_t POWER_VOLTAGE_PIN = 34;  // GPIO34 用于电压采样（ADC1_CH6）
    constexpr bool ENABLE_POWER_MONITORING = false;   // 启用电源监控
    
    // 网络参数
    constexpr uint16_t MQTT_PORT = 8883;
    constexpr uint16_t MQTT_KEEP_ALIVE = 600;
    constexpr uint32_t REPORT_INTERVAL = 1500;
    constexpr int16_t WIFI_RSSI_WEAK = -70;
    constexpr int16_t WIFI_RSSI_POOR = -80;
    constexpr int16_t OTA_RSSI_MIN = -75;
    
    // 时间配置（毫秒）
    constexpr uint32_t WDT_TIMEOUT = 8000;
    constexpr uint32_t LOOP_MAX_TIME = 6000;
    constexpr uint32_t WIFI_RETRY_DELAY = 10000;
    constexpr uint32_t MQTT_RETRY_DELAY = 8000;
    constexpr uint32_t ENGINE_DEBOUNCE_TIME = 3000;
    
    // OTA优化参数
    constexpr uint32_t OTA_HTTP_TIMEOUT_BASE = 60000;    // 基础60秒超时
    constexpr uint32_t OTA_DOWNLOAD_TIMEOUT_PER_MB = 45000; // 每MB额外45秒
    constexpr uint32_t OTA_CONNECT_TIMEOUT = 15000;      // 连接超时15秒
    constexpr uint8_t OTA_MAX_RETRIES = 3;               // 最大重试3次
    constexpr uint32_t OTA_RETRY_DELAY = 3000;           // 重试间隔3秒
    
    // 🔧 优化内存和栈阈值 - 提升稳定性
    constexpr size_t MEMORY_WARNING = 25600;  // 25KB (提升)
    constexpr size_t MEMORY_CRITICAL = 15360; // 15KB (提升)
    constexpr size_t MEMORY_EMERGENCY = 8192; // 8KB (提升)
    constexpr size_t STACK_WARNING = 2048;    // 2KB (翻倍提升)
    constexpr size_t STACK_CRITICAL = 1500;   // 1.5KB (几乎翻倍)
    constexpr size_t STACK_SAFE = 3072;       // 3KB (翻倍提升)
    
    // 消息限流 - 工业级优化配置
    constexpr uint16_t MQTT_GLOBAL_MAX_PER_SEC = 12;      // 提升至12条/秒，满足工业实时需求
    constexpr uint16_t MQTT_PROPERTY_MAX_PER_SEC = 5;     // 提升至5条/秒，改善响应性
    constexpr uint32_t MQTT_MIN_INTERVAL = 80;            // 降至80ms，提升数据实时性
    constexpr uint32_t MQTT_PROPERTY_INTERVAL = 150;      // 降至150ms，平衡性能和稳定性
    
    // 服务器地址
    const char* const MQTT_SERVER = "mqttstls.heclouds.com";
    const char* const NTP_SERVERS[] = {"ntp.aliyun.com", "pool.ntp.org"};
            const char* const UPDATE_INFO_URL = "https://tenkon.sunrays.top/updateota/update_info.json";  // 支持HTTPS和重定向
    
    // 安全配置
    const char* const REMOTE_REBOOT_CODE = "tenkon123";
    const char* const TRIPLET_SECRET = "Tenkon123!@#";
    const char* const REMOTE_REBOOT_FLAG_KEY = "rem_reboot";
    
    // 系统版本
    constexpr int FIRMWARE_VERSION = 1;
    
    // 时间相关
    constexpr time_t MIN_VALID_TIME = 1584588588;
    constexpr time_t NTP_SYNC_INTERVAL = 86400;  // 24小时
    
    // 脉冲计数
    constexpr uint32_t MIN_PULSE_INTERVAL_US = 800;
    constexpr uint8_t RPM_SAMPLE_COUNT = 4;
}

// ================================================================================
// 系统状态和数据结构
// ================================================================================
enum class SystemState : uint8_t {
    INITIALIZING, NORMAL_OPERATION, CONFIG_PORTAL, ERROR_RECOVERY, OTA_UPDATE
};

enum class NetworkState : uint8_t {
    DISCONNECTED, WIFI_CONNECTING, WIFI_CONNECTED, MQTT_CONNECTING, FULLY_CONNECTED
};

enum class EngineAction : uint8_t { NONE, START, STOP };

enum class LedMode : uint8_t {
    OFF, GREEN_ON, RED_ON, RED_BLINK_FAST, RED_BLINK_SLOW, YELLOW_BLINK
};

// 传感器数据结构 - 优化版
struct SensorData {
    float temperature;
    uint32_t rpm;
    uint8_t oil_level;
    bool oil_alarm;
    int16_t wifi_rssi;
    uint64_t timestamp_ms;
};

// 消息限流控制器
class ThrottleController {
public:
    bool check_and_update(uint32_t now, uint32_t interval, uint16_t max_per_sec) {
        uint32_t now_sec = now / 1000;
        
        if (now - last_time < interval) return false;
        
        if (now_sec != last_sec) {
            count_per_sec = 0;
            last_sec = now_sec;
        }
        if (count_per_sec >= max_per_sec) return false;
        
        last_time = now;
        count_per_sec++;
        return true;
    }
    
private:
    uint32_t last_time = 0;
    uint16_t count_per_sec = 0;
    uint32_t last_sec = 0;
};


// ================================================================================
// 全局变量 - 必须在类定义之前声明
// ================================================================================

// 全局时间变量
time_t gBaseTime = 0;
unsigned long gBaseMillis = 0;

// 全局栈保护标志
bool g_stackLowMode = false;
unsigned long g_lastStackModeChange = 0;

// ========== OTA 新增状态字段 ==========
bool g_auto_update = true;           // 自动更新开关（期望值）
bool g_ota_state = false;           // OTA状态：true=正在升级
unsigned long g_lastOTACheck = 0;    // 上次OTA检查时间
bool g_forceOTAStart = false;       // 强制启动OTA标志（服务调用触发）
bool g_desiredPropertySynced = false; // 期望属性是否已同步完成

// ========== OTA升级结果上报标志 ==========
bool needReportOTASuccess = false;   // 需要上报OTA成功事件
bool needReportOTAFailure = false;   // 需要上报OTA失败事件
int reportedOTAVersion = 0;          // 已升级的版本号
int otaFailureCode = 0;              // OTA失败错误码
String otaFailureMsg = "";           // OTA失败错误信息

// ========== 设备重启事件上报标志 ==========
bool needReportDeviceRestart = false;  // 需要上报设备重启事件
int lastRestartReason = 0;             // 上次重启原因
String lastRestartReasonStr = "";      // 重启原因描述

// ========== 工业级统计数据 ==========
struct IndustrialStats {
    // 发动机运行统计
    uint32_t engineTotalMinutes = 0;        // 累计运行分钟数
    uint32_t engineStartCount = 0;          // 启动次数统计
    
    // 故障统计
    uint16_t oilAlarmCount = 0;             // 油压报警次数
    uint16_t wifiDisconnectCount = 0;       // WiFi断线次数
    uint16_t mqttFailureCount = 0;          // MQTT连接失败次数
    uint16_t memoryWarningCount = 0;        // 内存警告次数
    
    // 重启统计
    uint8_t restartCountToday = 0;          // 今日重启次数
    uint32_t lastSaveDay = 0;               // 上次保存的日期
    
    // 注意：维护配置和报警阈值已移至HotConfig统一管理
    
    // 运行时统计
    unsigned long lastEngineStartTime = 0;   // 上次启动时间
    bool engineWasRunning = false;           // 上次检查时发动机状态
    unsigned long lastStatsSaveTime = 0;     // 上次保存统计数据时间
};

IndustrialStats g_stats;

// ================================================================================
// 系统管理器类 - 工具函数集合
// ================================================================================
class SystemUtils {
public:
    // 安全的看门狗重置
    static inline void safe_wdt_reset() {
        TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
        if (currentTask != NULL) {
            esp_err_t status = esp_task_wdt_status(currentTask);
            if (status == ESP_OK) {
                esp_task_wdt_reset();
            }
        }
    }
    
    // 安全延时函数
    static void safe_delay(uint32_t ms) {
        uint32_t start = millis();
        while (millis() - start < ms) {
            uint32_t remaining = ms - (millis() - start);
            delay(remaining < 20 ? remaining : 20);
            safe_wdt_reset();
        }
    }
    
    // 获取当前时间
    static time_t get_current_time() {
        time_t t = time(nullptr);
        if (t < 1584588588) {
            if (gBaseTime != 0) {
                return gBaseTime + (millis() - gBaseMillis) / 1000;
            }
        }
        return t;
    }
    
    // 获取WiFi信号强度
    static int16_t get_wifi_rssi() {
        return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -128;
    }

};

// ================================================================================
// 全局变量定义 - 按功能分组
// ================================================================================

// 设备身份信息
String g_product_id = "";
String g_device_name = "";
String g_device_key = "";

// 兼容旧变量名
String& gProductID = g_product_id;
String& gDeviceName = g_device_name;
String& gDeviceKey = g_device_key;

// 静态JSON文档 - 提前声明供类和函数使用
JsonDocument g_static_doc;
JsonDocument staticGetDoc;
JsonDocument staticReplyDoc;

// 网络配置变量 - 提前声明供类使用
String g_wifi_ssid = "";
String g_wifi_password = "";

// 硬件状态变量 - 提前声明供类使用
bool deviceOnline = false;
volatile bool otaInProgress = false;

// LED控制常量定义
#define LED_ON_LVL  HIGH
#define LED_OFF_LVL LOW

// ================================================================================
// 智能LED状态指示系统 - 通过红绿LED组合反映系统状态
// ================================================================================
#define LED_MODE_OFF                    0   // 全部关闭 - 系统未启动
#define LED_MODE_INIT_BLINK            1   // 红绿交替闪烁 - 系统初始化
#define LED_MODE_CONFIG_PORTAL         2   // 红绿同时快闪 - 配网模式
#define LED_MODE_WIFI_CONNECTING       3   // 绿灯慢闪 - WiFi连接中
#define LED_MODE_MQTT_CONNECTING       4   // 绿灯快闪 - MQTT连接中
#define LED_MODE_ENGINE_RUNNING        5   // 绿灯常亮 - 发动机运行+在线
#define LED_MODE_ENGINE_STOPPED        6   // 红灯常亮 - 发动机停止+在线
#define LED_MODE_OFFLINE               7   // 红灯慢闪 - 网络离线
#define LED_MODE_OIL_ALARM             8   // 红灯极快闪 - 机油报警
#define LED_MODE_OTA_UPDATE            9   // 红绿同时慢闪 - OTA升级
#define LED_MODE_SYSTEM_ERROR          10  // 红灯三连闪 - 系统错误
#define LED_MODE_LOW_MEMORY            11  // 红绿交替快闪 - 内存不足
#define LED_MODE_WEAK_SIGNAL           12  // 绿灯双闪 - 信号弱
#define LED_MODE_TOKEN_ERROR           13  // 红灯双闪 - 认证错误

int currentLedMode = LED_MODE_OFF;

// LED闪烁模式结构体
struct LedPattern {
    uint16_t period;        // 周期(ms)
    uint8_t red_on_time;    // 红灯亮时间比例(0-100)
    uint8_t green_on_time;  // 绿灯亮时间比例(0-100)
    uint8_t phase_shift;    // 相位差(0-100)，用于交替闪烁
};

// LED模式配置表
const LedPattern ledPatterns[] = {
    {0,     0,   0,   0},    // LED_MODE_OFF - 全关
    {600,   50,  50,  50},   // LED_MODE_INIT_BLINK - 红绿交替闪烁(慢)
    {200,   50,  50,  0},    // LED_MODE_CONFIG_PORTAL - 红绿同时快闪
    {1000,  0,   30,  0},    // LED_MODE_WIFI_CONNECTING - 绿灯慢闪
    {300,   0,   50,  0},    // LED_MODE_MQTT_CONNECTING - 绿灯快闪
    {0,     0,   100, 0},    // LED_MODE_ENGINE_RUNNING - 绿灯常亮
    {0,     100, 0,   0},    // LED_MODE_ENGINE_STOPPED - 红灯常亮
    {2000,  30,  0,   0},    // LED_MODE_OFFLINE - 红灯慢闪
    {150,   80,  0,   0},    // LED_MODE_OIL_ALARM - 红灯极快闪
    {800,   50,  50,  0},    // LED_MODE_OTA_UPDATE - 红绿同时慢闪
    {400,   33,  0,   0},    // LED_MODE_SYSTEM_ERROR - 红灯三连闪效果
    {250,   50,  50,  50},   // LED_MODE_LOW_MEMORY - 红绿交替快闪
    {600,   0,   40,  0},    // LED_MODE_WEAK_SIGNAL - 绿灯双闪效果
    {500,   40,  0,   0},    // LED_MODE_TOKEN_ERROR - 红灯双闪效果
};

// LED模式数量常量
const int LED_PATTERN_COUNT = sizeof(ledPatterns) / sizeof(ledPatterns[0]);

// 全局脉冲计数变量 - 提前声明供类使用
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t pulseCount = 0;
volatile uint32_t lastPulseMicros = 0;

// 配置结构体和实例
struct HotConfig {
    // 原有引擎控制参数
    uint32_t engineStartPullLowTime;
    uint32_t engineStartWaitTime;
    uint32_t engineStopPullLowTime;
    uint32_t engineStopWaitTime;
    float rpmCalibration;
    uint16_t engineOnRpmThreshold;
    uint32_t wakePulseMs;
    uint32_t wakeToLongMs;
    
    // 新增关键热配置参数
    bool deviceStatus;               // 设备开关状态
    uint16_t rpmAlarmThreshold;      // 转速报警阈值
    uint16_t maintenanceIntervalHours; // 维护间隔小时数
    int16_t wifiRssiWeakThreshold;   // WiFi弱信号阈值
    uint32_t memoryWarningThreshold; // 内存警告阈值(字节)
    uint16_t otaCheckIntervalMinutes; // OTA检查间隔(分钟)
} gConfig = {
    // 原有默认值
    7000, 3000, 2000, 3000, 1.0f, 1800, 100, 300,
    // 新增字段默认值
    false,    // deviceStatus - 默认关闭
    2200,     // rpmAlarmThreshold - 默认2200rpm
    200,      // maintenanceIntervalHours - 默认200小时
    -75,      // wifiRssiWeakThreshold - 默认-75dBm
    20480,    // memoryWarningThreshold - 默认20KB
    60        // otaCheckIntervalMinutes - 默认60分钟(1小时)
};

// 函数前置声明
bool needReportRemoteReboot = false;

// 按原代码风格简化wdt_safe_reset函数
void wdt_safe_reset() {
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    if (currentTask != NULL) {
        esp_err_t status = esp_task_wdt_status(currentTask);
        if (status == ESP_OK) {
            esp_task_wdt_reset();
        }
    }
}
// loadDeviceConfig函数前向声明
void loadDeviceConfig();

bool isEngineRunning();
SensorData readSensorData();


// RPM计算相关变量
uint32_t rpmSamples[4] = {0};
uint8_t rpmIdx = 0;
uint32_t filteredRPM = 0;
uint32_t filteredRPMCalib = 0;

// 时间同步变量
bool timeSynced = false;
time_t lastSyncTime = 0;





// 注意：常量定义已移至Config命名空间中，不要重复定义

// 系统监控管理器
class SystemMonitor {
public:
    static void check_memory_status() {
        static uint32_t last_check = 0;
        uint32_t now = millis();
        if (now - last_check < 5000) return;
        last_check = now;
        
        size_t free_heap = ESP.getFreeHeap();
        
        if (free_heap < 20480) { // 20KB
            DEBUG_PRINTF("[内存警告] 剩余: %u bytes\n", free_heap);
        }
        
        if (free_heap < 10240) { // 10KB
            DEBUG_PRINTF("[内存危险] 执行清理: %u bytes\n", free_heap);
            perform_cleanup();
        }
        
        if (free_heap < 5120) { // 5KB
            DEBUG_PRINTF("[内存急救] 系统重启: %u bytes\n", free_heap);
            SystemUtils::safe_delay(1000);
            ESP.restart();
        }
    }
    
    static void check_stack_status() {
        static uint32_t last_check = 0;
        uint32_t now = millis();
        if (now - last_check < 10000) return;
        last_check = now;
        
        size_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
        
        if (stack_remaining < 800 && !g_stackLowMode) { // 800B
            g_stackLowMode = true;
            DEBUG_PRINTF("[栈保护] 进入保护模式: %u bytes\n", stack_remaining);
        } else if (stack_remaining > 1500 && g_stackLowMode) { // 1.5KB
            g_stackLowMode = false;
            DEBUG_PRINTF("[栈保护] 退出保护模式: %u bytes\n", stack_remaining);
        }
        
        if (stack_remaining < 1024) { // 1KB
            DEBUG_PRINTF("[栈警告] 剩余: %u bytes\n", stack_remaining);
        }
    }

    
    static void perform_cleanup() {
        // 清理静态文档
        g_static_doc.clear();
        SystemUtils::safe_delay(100);
    }
};

// 配置管理器
class ConfigManager {
public:
    static bool load_device_config() {
        Preferences prefs;
        prefs.begin("devcfg", false);
        g_product_id = prefs.getString("product_id", "");
        g_device_name = prefs.getString("device_name", "");
        g_device_key = prefs.getString("device_key", "");
        prefs.end();

        DEBUG_PRINTF("[设备参数] 产品ID: %s\n", g_product_id.c_str());
        DEBUG_PRINTF("[设备参数] 设备名: %s\n", g_device_name.c_str());
        
        return validate_device_config();
    }
    
    static bool validate_device_config() {
        return (g_product_id.length() >= 4 && 
                g_device_name.length() >= 2 && 
                g_device_key.length() >= 16);
    }
    
    static bool load_wifi_config() {
        Preferences prefs;
        prefs.begin("wifi", false);
        g_wifi_ssid = prefs.getString("ssid", "");
        g_wifi_password = prefs.getString("pass", "");
        prefs.end();
        return !g_wifi_ssid.isEmpty();
    }
};

// loadDeviceConfig函数实现
void loadDeviceConfig() {
    // 使用ConfigManager类的实现
    ConfigManager::load_device_config();
}

// 🔧 新增：电源监控管理器
class PowerManager {
public:
    static void init() {
        // 🔧 仅在启用电源监控时初始化ADC
        if (Config::ENABLE_POWER_MONITORING) {
            // 初始化ADC用于电压监控
            analogReadResolution(12); // 12位精度
            
            // 配置电压采样引脚
            pinMode(Config::POWER_VOLTAGE_PIN, INPUT);
            
            DEBUG_PRINTF("[电源] 电源监控已启用，使用GPIO%d采样\n", Config::POWER_VOLTAGE_PIN);
            DEBUG_PRINTLN("[电源] 硬件分压电路：VIN -> 47kΩ -> GPIO34 -> 10kΩ -> GND");
        } else {
            DEBUG_PRINTLN("[电源] 电源监控功能已禁用");
        }
    }
    
    static void checkPowerStatus() {
        // 🔧 检查是否启用电源监控功能
        if (!Config::ENABLE_POWER_MONITORING) {
            return; // 功能未启用，直接返回
        }
        
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck < 30000) return; // 30秒检查一次，减少频率
        lastCheck = millis();
        
        // 读取电源电压
        float voltage = readSupplyVoltage();
        
        // 🔧 仅在获取到有效电压值时进行检查
        if (voltage <= 0.0) {
            DEBUG_PRINTLN("[电源] 警告：无法读取电压值，请检查硬件连接");
            return;
        }
        
        // 电压过低警告 (12V系统标准)
        if (voltage < 11.0) {
            DEBUG_PRINTF("[电源] 警告：电压过低 %.2fV\n", voltage);
            
            // 电压严重不足时进入保护模式
            if (voltage < 10.5) {
                DEBUG_PRINTF("[电源] 危险：电压严重不足 %.2fV，进入保护模式\n", voltage);
                enterLowPowerMode();
            }
        }
        
        // 电压过高保护
        if (voltage > 15.0) {
            DEBUG_PRINTF("[电源] 警告：电压过高 %.2fV\n", voltage);
        }
        
        // 🔧 定期记录电压状态（仅在调试时）
        static unsigned long lastVoltageLog = 0;
        if (millis() - lastVoltageLog > 300000) { // 5分钟记录一次
            DEBUG_PRINTF("[电源] 当前电压: %.2fV\n", voltage);
            lastVoltageLog = millis();
        }
    }
    
private:
    static float readSupplyVoltage() {
        // 🔧 使用配置的电压采样引脚
        int adcValue = analogRead(Config::POWER_VOLTAGE_PIN);
        if (adcValue <= 0) return 0.0; // 无效读取
        
        // 🔧 ADC转换为电压值 (ESP32 ADC参考电压约为3.3V，12位分辨率)
        float adcVoltage = (adcValue / 4095.0) * 3.3;
        
        // 🔧 分压电路计算 (47kΩ/10kΩ分压电路)
        // 分压电路：VIN -- R1(47K) -- ADC_PIN(GPIO34) -- R2(10K) -- GND
        // 分压比 = R2 / (R1 + R2) = 10 / (47 + 10) = 0.175
        // 因此：VIN = ADC_VOLTAGE / 0.175
        
        const float VOLTAGE_DIVIDER_RATIO = 0.175; // 47kΩ/10kΩ分压比
        float supplyVoltage = adcVoltage / VOLTAGE_DIVIDER_RATIO;
        
        // 🔧 电压范围合理性检查
        if (supplyVoltage < 0.0 || supplyVoltage > 30.0) {
            // 电压值不合理，可能是电路问题
            return 0.0;
        }
        
        return supplyVoltage;
    }
    
    static void enterLowPowerMode() {
        // 🔧 声明外部变量和函数
        extern PubSubClient mqttClient;
        extern String& gProductID;  // 引用类型
        extern String& gDeviceName; // 引用类型
        extern void setSpecialLedMode(int mode);
        
        // 关闭非关键功能
        setSpecialLedMode(LED_MODE_SYSTEM_ERROR);
        
        // 减少MQTT上报频率
        static bool lowPowerNotified = false;
        if (!lowPowerNotified && mqttClient.connected()) {
            char payload[256];
            float currentVoltage = readSupplyVoltage();
            snprintf(payload, sizeof(payload), 
                "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{\"power_low_event\":{\"value\":{\"voltage\":%.2f},\"time\":%llu}}}", 
                millis(), currentVoltage, (uint64_t)time(nullptr)*1000ULL);
            
            String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
            mqttClient.publish(topic.c_str(), payload);
            lowPowerNotified = true;
            DEBUG_PRINTLN("[电源] 低电压事件已上报");
        }
        
        DEBUG_PRINTLN("[电源] 已进入低功耗保护模式");
    }
};

// 🔧 新增：数据完整性管理器  
class DataIntegrityManager {
public:
    static bool verifyConfigIntegrity() {
        // 🔧 声明外部变量
        extern String& gProductID;  // 引用类型
        extern String& gDeviceName; // 引用类型
        extern String& gDeviceKey;  // 引用类型
        extern HotConfig gConfig;
        
        // 检查关键配置的完整性
        bool configValid = true;
        
        // 检查三元组完整性
        if (gProductID.length() < 4 || gDeviceName.length() < 2 || gDeviceKey.length() < 16) {
            DEBUG_PRINTLN("[数据完整性] 三元组数据不完整");
            configValid = false;
        }
        
        // 检查热配置参数范围
        if (gConfig.engineStartPullLowTime < 1000 || gConfig.engineStartPullLowTime > 10000) {
            DEBUG_PRINTF("[数据完整性] 启动时间参数异常: %lu\n", gConfig.engineStartPullLowTime);
            // 重置为默认值
            gConfig.engineStartPullLowTime = 3000;
            configValid = false;
        }
        
        if (gConfig.rpmCalibration < 0.1 || gConfig.rpmCalibration > 10.0) {
            DEBUG_PRINTF("[数据完整性] RPM校准参数异常: %.2f\n", gConfig.rpmCalibration);
            gConfig.rpmCalibration = 1.0;
            configValid = false;
        }
        
        return configValid;
    }
    
    static void performDataBackup() {
        // 🔧 声明外部变量
        extern IndustrialStats g_stats;
        extern HotConfig gConfig;
        
        // 定期备份关键数据
        static unsigned long lastBackup = 0;
        if (millis() - lastBackup < 3600000) return; // 1小时备份一次
        lastBackup = millis();
        
        Preferences backupPrefs;
        backupPrefs.begin("backup", false);
        
        // 备份统计数据
        backupPrefs.putUInt("engine_minutes", g_stats.engineTotalMinutes);
        backupPrefs.putUInt("start_count", g_stats.engineStartCount);
        backupPrefs.putUShort("restart_today", g_stats.restartCountToday);
        
        // 备份配置数据
        backupPrefs.putFloat("rpm_calib", gConfig.rpmCalibration);
        backupPrefs.putUInt("rpm_threshold", gConfig.engineOnRpmThreshold);
        
        backupPrefs.end();
        DEBUG_PRINTLN("[数据完整性] 数据备份完成");
    }
    
    static bool restoreFromBackup() {
        // 🔧 声明外部变量
        extern IndustrialStats g_stats;
        extern HotConfig gConfig;
        
        Preferences backupPrefs;
        backupPrefs.begin("backup", true); // 只读模式
        
        bool hasBackup = backupPrefs.isKey("engine_minutes");
        if (!hasBackup) {
            backupPrefs.end();
            return false;
        }
        
        DEBUG_PRINTLN("[数据完整性] 发现备份数据，开始恢复...");
        
        // 恢复统计数据
        g_stats.engineTotalMinutes = backupPrefs.getUInt("engine_minutes", 0);
        g_stats.engineStartCount = backupPrefs.getUInt("start_count", 0);
        g_stats.restartCountToday = backupPrefs.getUShort("restart_today", 0);
        
        // 恢复配置数据
        gConfig.rpmCalibration = backupPrefs.getFloat("rpm_calib", 1.0);
        gConfig.engineOnRpmThreshold = backupPrefs.getUInt("rpm_threshold", 300);
        
        backupPrefs.end();
        DEBUG_PRINTLN("[数据完整性] 数据恢复完成");
        return true;
    }
};

// 全局变量声明



//==============================================
SemaphoreHandle_t uartMutex;

// 使用全局gConfig（已在前面定义）

// ================================================================================
// 硬件抽象层 - 统一硬件接口
// ================================================================================
class HardwareManager {
public:
    static void init_gpio() {
        // LED引脚初始化
        pinMode(Config::LED_RED_PIN, OUTPUT);
        pinMode(Config::LED_GREEN_PIN, OUTPUT);
        set_led_state(false, false);
        
        // 发动机控制引脚
        pinMode(Config::ENGINE_CONTROL_PIN, OUTPUT);
        digitalWrite(Config::ENGINE_CONTROL_PIN, LOW);
        
        // 传感器输入引脚
        pinMode(Config::ENGINE_OIL_STATUS_PIN, INPUT);
        pinMode(Config::ENGINE_PULSE_PIN, INPUT);  // GPIO36 是只输入引脚，无内部上拉
        
        // 按钮引脚
        pinMode(Config::BOOT_BUTTON_PIN, INPUT_PULLUP);
        pinMode(0, INPUT_PULLUP);
    }
    
    static void init_pulse_counter() {
        attachInterrupt(digitalPinToInterrupt(Config::ENGINE_PULSE_PIN), 
                       pulse_isr, FALLING);
        pulseCount = 0;
    }
    
    static void update_led_display() {
        uint32_t now = millis();
        determine_led_mode();
        execute_led_control(now);
    }
    
    static bool get_oil_alarm_status() {
        return digitalRead(Config::ENGINE_OIL_STATUS_PIN) == LOW;
    }
    
    static bool get_button_state() {
        return digitalRead(Config::BOOT_BUTTON_PIN) == LOW;
    }
    
    static void set_engine_control(bool state) {
        digitalWrite(Config::ENGINE_CONTROL_PIN, state ? HIGH : LOW);
    }

private:
    static void IRAM_ATTR pulse_isr() {
        uint32_t now = micros();
        if (now - lastPulseMicros > 800) {
            portENTER_CRITICAL_ISR(&mux);
            pulseCount = pulseCount + 1;
            portEXIT_CRITICAL_ISR(&mux);
            lastPulseMicros = now;
        }
    }
    
    static void determine_led_mode() {
        // 声明外部变量
        extern PubSubClient mqttClient;
        extern volatile bool otaInProgress;
        extern bool deviceOnline;
        extern volatile bool g_forceTokenRefresh;
        
        // 高优先级状态检查（按优先级从高到低）
        
        // 1. 系统级紧急状态
        size_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 10240) { // 内存严重不足
            currentLedMode = LED_MODE_LOW_MEMORY;
            return;
        }
        
        // 2. 机油报警（最高设备优先级）
        if (HardwareManager::get_oil_alarm_status()) {
            currentLedMode = LED_MODE_OIL_ALARM;
            return;
        }
        
        // 3. OTA升级状态
        if (otaInProgress) {
            currentLedMode = LED_MODE_OTA_UPDATE;
            return;
        }
        
        // 4. 网络连接状态
        bool wifiConnected = (WiFi.status() == WL_CONNECTED);
        bool mqttConnected = mqttClient.connected();
        
        if (!wifiConnected) {
            currentLedMode = LED_MODE_OFFLINE;
            return;
        }
        
        if (wifiConnected && !mqttConnected) {
            // 检查是否是认证错误
            if (g_forceTokenRefresh) {
                currentLedMode = LED_MODE_TOKEN_ERROR;
                return;
            }
            currentLedMode = LED_MODE_MQTT_CONNECTING;
            return;
        }
        
        // 5. 网络质量检查
        if (wifiConnected && mqttConnected) {
            int rssi = SystemUtils::get_wifi_rssi();
            if (rssi < -80) {
                currentLedMode = LED_MODE_WEAK_SIGNAL;
                return;
            }
        }
        
        // 6. 正常运行状态 - 根据发动机状态
        if (deviceOnline) {
            // 声明外部函数
            extern bool isEngineRunning();
            if (isEngineRunning()) {
                currentLedMode = LED_MODE_ENGINE_RUNNING;
            } else {
                currentLedMode = LED_MODE_ENGINE_STOPPED;
            }
        } else {
            currentLedMode = LED_MODE_OFFLINE;
        }
    }
    
    static void execute_led_control(uint32_t now) {
        // 声明外部变量
        extern int currentLedMode;
        extern const LedPattern ledPatterns[];
        extern const int LED_PATTERN_COUNT;
        
        static uint32_t lastUpdate = 0;
        static uint8_t ledCycle = 0;
        
        // 获取当前模式的模式配置
        if (currentLedMode >= LED_PATTERN_COUNT) {
            set_led_state(false, false);
            return;
        }
        
        const LedPattern& pattern = ledPatterns[currentLedMode];
        
        // 常亮模式处理
        if (pattern.period == 0) {
            bool red = (pattern.red_on_time > 0);
            bool green = (pattern.green_on_time > 0);
            set_led_state(red, green);
            return;
        }
        
        // 闪烁模式处理
        if (now - lastUpdate >= pattern.period) {
            lastUpdate = now;
            ledCycle = (ledCycle + 1) % 100; // 0-99的循环计数
        }
        
        uint32_t elapsed = now - lastUpdate;
        uint8_t position = (elapsed * 100) / pattern.period; // 当前在周期中的位置(0-99)
        
        // 计算红灯状态
        bool redOn = false;
        if (pattern.red_on_time > 0) {
            if (pattern.phase_shift == 0) {
                // 同相闪烁
                redOn = (position < pattern.red_on_time);
            } else {
                // 交替闪烁
                uint8_t redPosition = (position + pattern.phase_shift) % 100;
                redOn = (redPosition < pattern.red_on_time);
            }
        }
        
        // 计算绿灯状态
        bool greenOn = false;
        if (pattern.green_on_time > 0) {
            greenOn = (position < pattern.green_on_time);
        }
        
        set_led_state(redOn, greenOn);
    }
    
    static void set_led_state(bool red, bool green) {
        digitalWrite(Config::LED_RED_PIN, red ? LED_ON_LVL : LED_OFF_LVL);
        digitalWrite(Config::LED_GREEN_PIN, green ? LED_ON_LVL : LED_OFF_LVL);
    }
    
    static void blink_led(uint32_t now, uint32_t interval, bool red, bool green) {
        static uint32_t last_toggle = 0;
        static bool led_state = false;
        
        if (now - last_toggle >= interval) {
            led_state = !led_state;
            if (red) digitalWrite(Config::LED_RED_PIN, led_state ? LED_ON_LVL : LED_OFF_LVL);
            if (green) digitalWrite(Config::LED_GREEN_PIN, led_state ? LED_ON_LVL : LED_OFF_LVL);
            if (!red) digitalWrite(Config::LED_RED_PIN, LED_OFF_LVL);
            if (!green) digitalWrite(Config::LED_GREEN_PIN, LED_OFF_LVL);
            last_toggle = now;
        }
    }
};

// ================================================================================
// 传感器数据管理
// ================================================================================
class SensorManager {
public:
    static void update_rpm() {
        static uint32_t last_update = 0;
        uint32_t now = millis();
        
        if (now - last_update >= 1000) {
            uint32_t count;
            portENTER_CRITICAL(&mux);
            count = pulseCount;
            pulseCount = 0;
            portEXIT_CRITICAL(&mux);
            
            uint32_t raw_rpm = count * 60;
            rpmSamples[rpmIdx] = raw_rpm;
            rpmIdx = (rpmIdx + 1) & 0x03;
            
            // 计算滤波后的RPM
            uint64_t sum = 0;
            for (uint8_t i = 0; i < 4; i++) {
                sum += rpmSamples[i];
            }
            filteredRPM = sum >> 2;
            filteredRPMCalib = (uint32_t)roundf(filteredRPM * gConfig.rpmCalibration);
            
            last_update = now;
        }
    }
    
    static SensorData read_all_sensors() {
        update_rpm();
        
        SensorData data;
        data.temperature = random(200, 351) / 10.0f;
        data.rpm = filteredRPMCalib;
        data.oil_level = random(1, 100);
        data.oil_alarm = HardwareManager::get_oil_alarm_status();
        data.wifi_rssi = SystemUtils::get_wifi_rssi();
        data.timestamp_ms = (uint64_t)SystemUtils::get_current_time() * 1000ULL;
        
        return data;
    }
    
    static bool is_engine_running() {
        update_rpm();
        return filteredRPM >= gConfig.engineOnRpmThreshold;
    }

};

// 全局变量 - 版本号管理
int setnewFirmwareVersion = 0;  // 运行时动态设置，避免编译时硬编码
bool deviceStatus         = false;

volatile bool g_needReportSync = false;
// 注释：原OTA事件上报变量已移除，改用新的实时上报机制

// OTA下载卡住检测
volatile bool g_otaStuckDetected = false;
volatile unsigned long g_otaStuckTime = 0;

// Token管理全局变量
volatile bool g_forceTokenRefresh = false;
time_t g_lastTokenTime = 0;

static unsigned long lastGetReplyTime = 0;
static int getReplyCountPerSec = 0;
static unsigned long lastGetReplySec = 0;
static char staticGetOutBuf[2048];  // 增加到2KB以支持完整工业级属性响应

// 全局MQTT消息节流变量
static unsigned long lastMqttCallbackTime = 0;
static int mqttCallbackCountPerSec = 0;
static unsigned long lastMqttCallbackSec = 0;

// 各类型消息独立节流变量
static unsigned long lastPropertySetTime = 0;
static int propertySetCountPerSec = 0;
static unsigned long lastPropertySetSec = 0;

static unsigned long lastServiceInvokeTime = 0;
static int serviceInvokeCountPerSec = 0;
static unsigned long lastServiceInvokeSec = 0;

static unsigned long lastDesiredProcessTime = 0;
static int desiredProcessCountPerSec = 0;
static unsigned long lastDesiredProcessSec = 0;



WiFiClientSecure espClient;
PubSubClient     mqttClient(espClient);
WiFiClientSecure otaClient;
WebServer        server(80);

Preferences otaPrefs;
Preferences wifiPrefs;
Preferences configPrefs;   // 热加载参数NVS

String configuredSSID = "";
String configuredPASS = "";

//按钮检测变量
// ==== 不再单独定义，使用上面全局变量 ====

// 时间同步常量
const  time_t syncInterval = Config::NTP_SYNC_INTERVAL;

// 上报
unsigned long lastReportTime = 0;
unsigned int  postMsgId      = 0;
const int     reportInterval = Config::REPORT_INTERVAL;

// 按钮防抖
bool          lastButtonState  = HIGH;
unsigned long buttonPressStart = 0;
const int     debounceTime     = 50;

const char* rootCACert = "-----BEGIN CERTIFICATE-----\n"
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

// OTA服务器证书 - tenkon.sunrays.top
const char* otaServerCert = "-----BEGIN CERTIFICATE-----\n"
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

bool lastEngineRunning = false;
unsigned long lastEngineChangeTime = 0;
const unsigned long engineStateDebounceTime = Config::ENGINE_DEBOUNCE_TIME;

// 全局报告缓冲区，避免栈分配 - 优化工业级统计功能后的大小
static char g_reportParams[2000];  // 增加缓冲区大小以容纳工业级统计字段
static char g_reportPayload[2200];  // 增加缓冲区大小以容纳完整JSON

// 声明
void reportSensorData(bool force=false);

// 转速脉冲计数与滤波变量
volatile uint32_t lastMillis   = 0;

// LED相关枚举定义（LED_MODE_已在文件开头定义）
unsigned long lastLedToggleTime = 0;
bool ledRedState = false;
bool ledGreenState = false;


enum EngAction { ENG_NONE, ENG_START, ENG_STOP };

struct EngCtrl {
  EngAction action     = ENG_NONE;  // 当前动作
  uint8_t   step       = 0;         // 当前步骤
  uint32_t  ts         = 0;         // 上一步时间戳
} engCtrl;


// ---------- WDT 辅助：判断当前任务是否已被监控 ----------
static inline bool taskWatched()
{
  // ESP_OK 表示 task 已登记到 TWDT
  return esp_task_wdt_status(xTaskGetCurrentTaskHandle()) == ESP_OK;
}

// ========== 智能LED状态控制函数 ==========
void updateLedState() {
    HardwareManager::update_led_display();
}

// 特殊状态LED控制 - 用于初始化等场景
void setSpecialLedMode(int mode) {
    currentLedMode = mode;
}

bool getOilAlarmStatus() {
  int oilPin = digitalRead(Config::ENGINE_OIL_STATUS_PIN);
  bool alarm = (oilPin == LOW);
  return alarm;
}
void IRAM_ATTR onPulse() {
  uint32_t now = micros();
  if (now - lastPulseMicros > Config::MIN_PULSE_INTERVAL_US) {
    // 使用临界区保护，且用 pulseCount = pulseCount + 1;
    portENTER_CRITICAL_ISR(&mux);
    pulseCount = pulseCount + 1;
    portEXIT_CRITICAL_ISR(&mux);
    lastPulseMicros = now;
  }
}

int getWifiRSSI() {
    if (WiFi.status() == WL_CONNECTED)
        return WiFi.RSSI();
    return -128; // 断线时用极小值
}

// 同步发送MQTT消息并等待足够多loop，确保消息送出（适合重启、OTA等场景）
bool publishSyncAndWait(const char* topic, const char* payload, uint8_t qos = 0) {
    if (!mqttClient.connected()) {
        DEBUG_PRINTLN("[MQTT] 未连接，跳过同步发布");
        return false;
    }
    
    // 检查payload长度，防止过大消息阻塞
    size_t payloadLen = strlen(payload);
    if (payloadLen > 1536) { // 1.5KB限制
        DEBUG_PRINTF("[MQTT] 消息过大(%u bytes)，跳过发布\n", payloadLen);
        return false;
    }
    
    bool ok = mqttClient.publish(topic, payload, qos);
    if (!ok) {
        DEBUG_PRINTF("[MQTT] 发布失败: %s\n", topic);
        return false;
    }
    
    unsigned long t_start = millis();
    int loopCount = 0;
    while (millis() - t_start < 1000) {   // 减少到1秒，避免长时间阻塞
        if (!mqttClient.connected()) {
            DEBUG_PRINTLN("[MQTT] 连接断开，退出同步等待");
            break;
        }
        
        mqttClient.loop();
        
        // 每10次loop喂一次狗
        if (++loopCount % 10 == 0) {
            wdt_safe_reset();
        }
        
        delay(50); // 增加延时，减少CPU占用
    }
    return ok;
}

void initPulseCounter() {
  pinMode(Config::ENGINE_PULSE_PIN, INPUT);  // GPIO36 是只输入引脚，无内部上拉
  attachInterrupt(digitalPinToInterrupt(Config::ENGINE_PULSE_PIN), onPulse, FALLING);
  pulseCount = 0;
  lastMillis = millis();
}

void updateRPM() {
  uint32_t now = millis();
  if ((uint32_t)(now - lastMillis) >= 1000) {
    uint32_t cnt;
    portENTER_CRITICAL(&mux);
    cnt = pulseCount;
    pulseCount = 0;
    portEXIT_CRITICAL(&mux);
    uint32_t rawRPM = cnt * 60;
    rpmSamples[rpmIdx] = rawRPM;
    rpmIdx = (rpmIdx + 1) & 0x03;
    uint64_t sum = 0;
    for (uint8_t i = 0; i < 4; i++) sum += rpmSamples[i];
    filteredRPM = sum >> 2;
    filteredRPMCalib = (uint32_t)roundf(filteredRPM * gConfig.rpmCalibration);
    lastMillis = now;
  }
}


SensorData readSensorData() {
  updateRPM();
  SensorData data;
  data.temperature = random(200, 351) / 10.0f;
  data.rpm         = filteredRPMCalib;
  data.oil_level   = random(1, 100);
  data.oil_alarm    = HardwareManager::get_oil_alarm_status();
  return data;
}

bool isEngineRunning() {
  updateRPM();
  return (filteredRPM >= gConfig.engineOnRpmThreshold);
}

String urlEncode(const String &str) {
  String encoded = "";
  char temp[4];
  const char *chars = str.c_str();
  for (size_t i = 0; i < str.length(); i++) {
    char c = chars[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      switch(c) {
        case '+': encoded += "%2B"; break;
        case ' ': encoded += "%20"; break;
        case '/': encoded += "%2F"; break;
        case '?': encoded += "%3F"; break;
        case '%': encoded += "%25"; break;
        case '#': encoded += "%23"; break;
        case '&': encoded += "%26"; break;
        case '=': encoded += "%3D"; break;
        default:
          sprintf(temp, "%%%02X", (unsigned char)c);
          encoded += temp;
      }
    }
  }
  return encoded;
}
time_t getCurrentTime() {
  time_t t = time(nullptr);
  // 简化时间获取逻辑，避免复杂计算
  if (t < Config::MIN_VALID_TIME && gBaseTime != 0) {
    return gBaseTime + (millis() - gBaseMillis) / 1000;
  }
  return t;
}
String generateSecureToken() {
  // 栈保护检查
  UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
  if (stackRemaining < 1024) { // 小于1KB栈空间时拒绝生成Token
    DEBUG_PRINTF("[Token] 栈空间不足，拒绝生成Token: %u bytes\n", stackRemaining);
    return "";
  }
  
  // 基本安全检查
  if (gProductID.isEmpty() || gDeviceName.isEmpty() || gDeviceKey.isEmpty()) {
    DEBUG_PRINTLN("[Token] 设备参数为空，无法生成Token");
    return "";
  }
  
  // 看门狗喂食
  wdt_safe_reset();
  
  const String version = "2018-10-31";
  // 🔧 修复认证方式：使用产品级认证（因为使用的是产品级Key）
  const String res = "products/" + gProductID;
  //const String res = "products/" + gProductID + "/devices/" + gDeviceName;
  time_t currentTime = getCurrentTime();
  
  // 简化时间处理，减少日志输出
  if (currentTime < Config::MIN_VALID_TIME) {
    DEBUG_PRINTLN("[警告] 时间尚未同步, Token 可能无效");
  }
  
  time_t et = currentTime + 86400; // 24小时后过期
  
  // 安全的字符串构建
  String stringForSign = String(et) + "\nsha256\n" + res + "\n" + version;
  
  // 基本安全检查
  if (stringForSign.length() == 0 || stringForSign.length() > 1024) {
    DEBUG_PRINTLN("[Token] stringForSign异常");
    return "";
  }
  
  // 看门狗喂食
  wdt_safe_reset();
  
  unsigned char keyBin[32]; 
  size_t keyLen = 0;
  
  // 检查设备密钥长度
  if (gDeviceKey.length() == 0 || gDeviceKey.length() > 256) {
    DEBUG_PRINTLN("[Token] 设备密钥长度异常");
    return "";
  }
  
  int ret = mbedtls_base64_decode(keyBin, sizeof(keyBin), &keyLen,
                (const unsigned char*)gDeviceKey.c_str(), gDeviceKey.length());
  if (ret != 0) { 
    DEBUG_PRINTF("[Token] base64解码失败: %d\n", ret); 
    return ""; 
  }
  
  unsigned char hmacResult[32];
  
  // 看门狗喂食
  wdt_safe_reset();
  
  ret = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
    keyBin, keyLen,
    (const unsigned char*)stringForSign.c_str(),
    stringForSign.length(),
    hmacResult
  );
  if (ret != 0) { 
    DEBUG_PRINTF("[Token] HMAC计算失败: %d\n", ret); 
    return ""; 
  }
  
  // 看门狗喂食
  wdt_safe_reset();
  
  char signBase64[64];
  memset(signBase64, 0, sizeof(signBase64)); // 清零缓冲区
  size_t signLen = 0;
  ret = mbedtls_base64_encode((unsigned char*)signBase64, sizeof(signBase64), &signLen, hmacResult, sizeof(hmacResult));
  if (ret != 0) { 
    DEBUG_PRINTF("[Token] base64编码失败: %d\n", ret); 
    return ""; 
  }
  
  // 验证编码结果
  if (signLen == 0 || signLen >= sizeof(signBase64)) {
    DEBUG_PRINTF("[Token] base64编码长度异常: %zu\n", signLen);
    return "";
  }
  
  // 确保字符串以null结尾
  signBase64[signLen] = '\0';
  
  String token = "version=" + version +
                 "&res=" + urlEncode(res) +
                 "&et=" + String(et) +
                 "&method=sha256" +
                 "&sign=" + urlEncode(String(signBase64));
  
  // 最终验证Token长度
  if (token.length() == 0 || token.length() > 2048) {
    DEBUG_PRINTF("[Token] 生成的Token长度异常: %d\n", token.length());
    return "";
  }
  
  return token;
}

// ========= 看门狗友好延时 =========
void safeDelay(uint32_t ms)
{
  uint32_t start = millis();
  uint32_t remaining = ms;
  
  while (remaining > 0) {
    uint32_t elapsed = millis() - start;
    if (elapsed >= ms) break;
    
    remaining = ms - elapsed;
    uint32_t delayTime = (remaining > 50) ? 50 : remaining;
    
    delay(delayTime);
    
    // 多重安全检查：确保看门狗操作的安全性
    TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
    if (currentTask != NULL) {
      esp_err_t status = esp_task_wdt_status(currentTask);
      if (status == ESP_OK) {
        esp_task_wdt_reset();
      }
      // 如果返回ESP_ERR_NOT_FOUND说明任务不在WDT中，这是正常的
      // 其他错误码也不需要特别处理，只是跳过喂狗
    }
  }
}

// 触发异步开机
void startEngineAsync() {
  if (engCtrl.action == ENG_NONE)
    engCtrl = { ENG_START, 0, millis() };
}

// 触发异步关机
void stopEngineAsync() {
  if (engCtrl.action == ENG_NONE)
    engCtrl = { ENG_STOP, 0, millis() };
}

void updateEngineState() {
  bool actualRunning = isEngineRunning();
  if (deviceStatus && !actualRunning) {
    startEngineAsync();
  } else if (!deviceStatus && actualRunning) {
    stopEngineAsync();
  }
}

/* ------------------------------------------------------------
 *  上报传感器数据（仅属性层含 timestamp）- 优化栈使用版
 * ------------------------------------------------------------ */
void reportSensorData(bool force)
{
    // ========== 按用户要求：OTA期间继续数据上报 ==========
    // 移除OTA期间的暂停逻辑，保持数据上报和MQTT并行运行
    
    // ========== 栈保护模式检查 ==========
    if (g_stackLowMode && !force) {
        static unsigned long lastStackLog = 0;
        if (millis() - lastStackLog > 30000) { // 30秒记录一次
            DEBUG_PRINTLN("[栈保护] 栈保护模式下跳过常规数据上报");
            lastStackLog = millis();
        }
        return;
    }
    
    static uint32_t lastSend = 0;
    if (!force && (millis() - lastSend < Config::REPORT_INTERVAL)) return;
    lastSend = millis();

    SensorData sensor   = readSensorData();
    bool       engReal  = isEngineRunning();
    bool       netOK    = (WiFi.status() == WL_CONNECTED && mqttClient.connected());

    uint64_t tsNowMs = (uint64_t)getCurrentTime() * 1000ULL;
    
    // 优化：只上报动态数据和传感器数据，配置参数只在查询时返回
    int n = snprintf(g_reportParams, sizeof(g_reportParams),
        "{"
            "\"online\":{\"value\":%s,\"time\":%llu},"
            "\"version\":{\"value\":%d,\"time\":%llu},"
            "\"oil_level\":{\"value\":%d,\"time\":%llu},"
            "\"temp\":{\"value\":%.1f,\"time\":%llu},"
            "\"rpm\":{\"value\":%lu,\"time\":%llu},"
            "\"onff\":{\"value\":%s,\"time\":%llu},"
            "\"engine_actual\":{\"value\":%s,\"time\":%llu},"
            "\"oil_alarm\":{\"value\":%s,\"time\":%llu},"
            "\"net\":{\"value\":true,\"time\":%llu},"
            "\"timestamp\":{\"value\":%llu},"
            "\"wifi_rssi\":{\"value\":%d,\"time\":%llu},"
            "\"battery_voltage\":{\"value\":%.1f,\"time\":%llu},"
            "\"ota_state\":{\"value\":%s,\"time\":%llu},"
            // ========== 工业级统计数据（定期上报）==========
            "\"engine_total_hours\":{\"value\":%d,\"time\":%llu},"
            "\"engine_total_minutes\":{\"value\":%lu,\"time\":%llu},"
            "\"engine_start_count\":{\"value\":%lu,\"time\":%llu},"
            "\"oil_alarm_count\":{\"value\":%d,\"time\":%llu},"
            "\"wifi_disconnect_count\":{\"value\":%d,\"time\":%llu},"
            "\"mqtt_failure_count\":{\"value\":%d,\"time\":%llu},"
            "\"memory_warning_count\":{\"value\":%d,\"time\":%llu},"
            "\"restart_count_today\":{\"value\":%d,\"time\":%llu},"
            "\"uptime_seconds\":{\"value\":%lu,\"time\":%llu},"
            "\"free_heap\":{\"value\":%lu,\"time\":%llu}"
        "}",
        netOK ? "true" : "false",          tsNowMs,
        setnewFirmwareVersion,             tsNowMs,
        sensor.oil_level,                  tsNowMs,
        sensor.temperature,                tsNowMs,
        sensor.rpm,                        tsNowMs,
        deviceStatus ? "true" : "false",   tsNowMs,
        engReal ? "true" : "false",        tsNowMs,
        sensor.oil_alarm ? "true" : "false",tsNowMs,
        tsNowMs,
        tsNowMs,
        getWifiRSSI(),                     tsNowMs,
        12.6,                              tsNowMs, // 电池电压（默认值，可根据实际硬件修改）
        g_ota_state ? "true" : "false", tsNowMs,
        // ========== 工业级统计数据参数（定期上报）==========
        (int)(g_stats.engineTotalMinutes / 60), tsNowMs,  // 总小时数
        g_stats.engineTotalMinutes, tsNowMs,               // 总分钟数
        g_stats.engineStartCount, tsNowMs,                 // 启动次数
        (int)g_stats.oilAlarmCount, tsNowMs,               // 油压报警次数
        (int)g_stats.wifiDisconnectCount, tsNowMs,         // WiFi断线次数
        (int)g_stats.mqttFailureCount, tsNowMs,            // MQTT失败次数
        (int)g_stats.memoryWarningCount, tsNowMs,          // 内存警告次数
        (int)g_stats.restartCountToday, tsNowMs,           // 今日重启次数
        millis() / 1000, tsNowMs,                          // 运行时间秒
        ESP.getFreeHeap(), tsNowMs                         // 可用内存
    );

    if ((size_t)n >= sizeof(g_reportParams)) {
        DEBUG_PRINTF("[ERR] params 缓冲区不足, JSON 被截断！需要:%d 可用:%d\n", n, sizeof(g_reportParams));
        return;
    }
    
    // 调试信息：显示缓冲区使用情况（OTA期间禁用）
    static unsigned long lastBufferReport = 0;
    if (!otaInProgress && millis() - lastBufferReport > 60000) { // 每分钟报告一次，OTA期间禁用
        DEBUG_PRINTF("[Buffer] params使用: %d/%d bytes (%.1f%%)\n", 
                     n, sizeof(g_reportParams), (float)n * 100.0 / sizeof(g_reportParams));
        lastBufferReport = millis();
    }

    snprintf(g_reportPayload, sizeof(g_reportPayload),
        "{"
            "\"id\":\"%u\","
            "\"version\":\"1.0\","
            "\"params\":%s"
        "}",
        postMsgId++,
        g_reportParams);

    const String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/post";
    if (!netOK) {
        //DEBUG_PRINTLN("[WiFi] 未连接, 跳过上报");
        return;
    }
    bool ok = mqttClient.publish(topic.c_str(), g_reportPayload);
    if (!otaInProgress) { // OTA期间减少日志输出
        if (ok) {
            DEBUG_PRINTF("[WiFi] 上报成功: %s\n", g_reportPayload);
        } else {
            DEBUG_PRINTF("[WiFi] 上报失败: MQTT状态=%d, 消息长度=%d字节\n", 
                         mqttClient.state(), strlen(g_reportPayload));
            DEBUG_PRINTF("[WiFi] 失败消息: %s\n", g_reportPayload);
            
            // 增加故障统计
            g_stats.mqttFailureCount++;
        }
    }
    int rssi = getWifiRSSI();
    if (rssi < gConfig.wifiRssiWeakThreshold && !otaInProgress) { // OTA期间不上报弱信号事件
        reportWeakWifiEvent(rssi);
    }
}


// ===================== 热加载参数解析接口 =====================
void updateHotConfigFromParams(JsonObject params) {
    bool updated = false;
    configPrefs.begin("hotcfg", false);
    if (params["engineStartPullLowTime"].is<unsigned long>()) {
        gConfig.engineStartPullLowTime = params["engineStartPullLowTime"];
        configPrefs.putULong("startLow", gConfig.engineStartPullLowTime);
        updated = true;
    }
    if (params["engineStartWaitTime"].is<unsigned long>()) {
        gConfig.engineStartWaitTime = params["engineStartWaitTime"];
        configPrefs.putULong("startWait", gConfig.engineStartWaitTime);
        updated = true;
    }
    if (params["engineStopPullLowTime"].is<unsigned long>()) {
        gConfig.engineStopPullLowTime = params["engineStopPullLowTime"];
        configPrefs.putULong("stopLow", gConfig.engineStopPullLowTime);
        updated = true;
    }
    if (params["engineStopWaitTime"].is<unsigned long>()) {
        gConfig.engineStopWaitTime = params["engineStopWaitTime"];
        configPrefs.putULong("stopWait", gConfig.engineStopWaitTime);
        updated = true;
    }
    if (params["rpmCalibration"].is<float>() || params["rpmCalibration"].is<double>() || params["rpmCalibration"].is<int>()) {
        gConfig.rpmCalibration = params["rpmCalibration"];
        configPrefs.putFloat("rpmCalib", gConfig.rpmCalibration);
        updated = true;
    }
    if (params["engineOnRpmThreshold"].is<int>()) {
        gConfig.engineOnRpmThreshold = params["engineOnRpmThreshold"];
        configPrefs.putInt("rpmThres", gConfig.engineOnRpmThreshold);
        updated = true;
    }
    if (params["wakePulseMs"].is<unsigned long>()) {           // 新增
        gConfig.wakePulseMs = params["wakePulseMs"];
        configPrefs.putULong("wakePulse", gConfig.wakePulseMs);
        updated = true;
    }
    if (params["wakeToLongMs"].is<unsigned long>()) {          // 新增
        gConfig.wakeToLongMs = params["wakeToLongMs"];
        configPrefs.putULong("wakeToLong", gConfig.wakeToLongMs);
        updated = true;
    }
    
    // ========== OTA自动更新开关处理 ==========
    if (params["auto_update"].is<bool>()) {
        bool newAutoUpdate = params["auto_update"];
        if (newAutoUpdate != g_auto_update) {
            g_auto_update = newAutoUpdate;
            configPrefs.putBool("autoUpdate", g_auto_update);
            updated = true;
            DEBUG_PRINTF("[OTA] auto_update 更新为: %s\n", g_auto_update ? "true" : "false");
            
            // 如果设置为true，立即触发OTA检查
            if (g_auto_update) {
                DEBUG_PRINTLN("[OTA] auto_update设为true，将在下次循环检查OTA");
                g_lastOTACheck = 0; // 重置计时器，下次立即检查
            }
        }
    }
    
    // ========== 新增热配置参数处理 ==========
            // 修正：使用平台物模型的下划线命名
        if (params["rpm_alarm_threshold"].is<int>()) {
            gConfig.rpmAlarmThreshold = params["rpm_alarm_threshold"];
            configPrefs.putUShort("rpmAlarm", gConfig.rpmAlarmThreshold);
            updated = true;
            DEBUG_PRINTF("[热配置] rpm_alarm_threshold 更新为: %d\n", gConfig.rpmAlarmThreshold);
        }
        
        if (params["maintenance_interval_hours"].is<int>()) {
            gConfig.maintenanceIntervalHours = params["maintenance_interval_hours"];
            configPrefs.putUShort("maintHours", gConfig.maintenanceIntervalHours);
            updated = true;
            DEBUG_PRINTF("[热配置] maintenance_interval_hours 更新为: %d\n", gConfig.maintenanceIntervalHours);
        }
    
    if (params["wifi_rssi_weak_threshold"].is<int>()) {
        gConfig.wifiRssiWeakThreshold = params["wifi_rssi_weak_threshold"];
        configPrefs.putShort("wifiWeak", gConfig.wifiRssiWeakThreshold);
        updated = true;
        DEBUG_PRINTF("[热配置] wifi_rssi_weak_threshold 更新为: %d\n", gConfig.wifiRssiWeakThreshold);
    }
    
    if (params["memory_warning_threshold"].is<unsigned long>()) {
        gConfig.memoryWarningThreshold = params["memory_warning_threshold"];
        configPrefs.putULong("memWarn", gConfig.memoryWarningThreshold);
        updated = true;
        DEBUG_PRINTF("[热配置] memory_warning_threshold 更新为: %lu\n", gConfig.memoryWarningThreshold);
    }
    
    if (params["ota_check_interval_minutes"].is<int>()) {
        uint16_t newInterval = params["ota_check_interval_minutes"];
        if (newInterval >= 5 && newInterval <= 1440) { // 限制5分钟到24小时
            gConfig.otaCheckIntervalMinutes = newInterval;
            configPrefs.putUShort("otaInterval", gConfig.otaCheckIntervalMinutes);
            updated = true;
            DEBUG_PRINTF("[热配置] ota_check_interval_minutes 更新为: %d分钟\n", gConfig.otaCheckIntervalMinutes);
        } else {
            DEBUG_PRINTF("[热配置] ota_check_interval_minutes 值无效(%d)，需要5-1440分钟\n", newInterval);
        }
    }
    
    configPrefs.end();
    if (updated) {
        DEBUG_PRINTLN("[热加载] 参数已热更新");
    }
}

void reportWeakWifiEvent(int rssi) {
    static unsigned long lastReportTime = 0;
    unsigned long now = millis();

    // 10分钟内只上报一次
    if (lastReportTime != 0 && now - lastReportTime < 10 * 60 * 1000) {
        // 10分钟节流触发，跳过本次上报
        return;
    }
    lastReportTime = now;

    if (!mqttClient.connected()) {
        DEBUG_PRINTLN("[WiFi] MQTT未连接，无法上报弱信号事件");
        return;
    }

    char idbuf[20];
    snprintf(idbuf, sizeof(idbuf), "%lu", now);

    char payload[512];
    int ret = snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"weak_wifi\":{"
                    "\"value\":{\"rssi\":%d},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, rssi, (uint64_t)time(nullptr)*1000ULL
    );
    if (ret < 0 || (size_t)ret >= sizeof(payload)) {
        DEBUG_PRINTLN("[ERR] weak_wifi payload溢出，终止上报");
        return;
    }

    // 注意此时不能在中断/Timer回调等地方调用
    // 主循环直接用即可
    publishSyncAndWait( // 一定要保证此时无其它 MQTT 正在 publish
        (String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post").c_str(),
        payload, 0
    );
    DEBUG_PRINTF("[WiFi] 信号弱事件已上报: rssi=%d\n", rssi);
}





// ===================== 启动时加载热参数 =====================
void loadHotConfig() {
    configPrefs.begin("hotcfg", false);
    
    // 原有配置参数
    gConfig.engineStartPullLowTime = configPrefs.getULong("startLow", 7000);
    gConfig.engineStartWaitTime    = configPrefs.getULong("startWait", 3000);
    gConfig.engineStopPullLowTime  = configPrefs.getULong("stopLow", 2000);
    gConfig.engineStopWaitTime     = configPrefs.getULong("stopWait", 3000);
    gConfig.rpmCalibration         = configPrefs.getFloat("rpmCalib", 1.0f);
    gConfig.engineOnRpmThreshold   = configPrefs.getInt("rpmThres", 1800);
    gConfig.wakePulseMs            = configPrefs.getULong("wakePulse", 100);
    gConfig.wakeToLongMs           = configPrefs.getULong("wakeToLong", 300);
    
    // 新增热配置参数
    gConfig.deviceStatus           = configPrefs.getBool("devStatus", false);
    gConfig.rpmAlarmThreshold      = configPrefs.getUShort("rpmAlarm", 2200);
    gConfig.maintenanceIntervalHours = configPrefs.getUShort("maintHours", 200);
    gConfig.wifiRssiWeakThreshold  = configPrefs.getShort("wifiWeak", -75);
    gConfig.memoryWarningThreshold = configPrefs.getULong("memWarn", 20480);
    gConfig.otaCheckIntervalMinutes = configPrefs.getUShort("otaInterval", 60); // 默认60分钟
    
    // OTA配置
    g_auto_update = configPrefs.getBool("autoUpdate", true);
    
    configPrefs.end();
    
    // 同步deviceStatus到全局变量
    deviceStatus = gConfig.deviceStatus;
    
    DEBUG_PRINTF("[热配置] 加载完成 - deviceStatus:%s, rpmAlarm:%d, maintenance:%dh, wifiWeak:%ddBm, memWarn:%luB, otaInterval:%dmin\n",
                 gConfig.deviceStatus ? "ON" : "OFF", 
                 gConfig.rpmAlarmThreshold,
                 gConfig.maintenanceIntervalHours,
                 gConfig.wifiRssiWeakThreshold,
                 gConfig.memoryWarningThreshold,
                 gConfig.otaCheckIntervalMinutes);
}

// 传入：None（直接用全局gProductID/gDeviceName/mqttClient）
// 依赖：ArduinoJson，gProductID/gDeviceName，mqttClient
// 说明：params数组内容一定要和物模型标识符一字不差！

void clearDesiredProperties() {
    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "%llu", millis() % 10000000000000UL);

    JsonDocument doc;
    doc["id"] = idbuf;
    doc["version"] = "1.0";

    // 必须是对象，不是数组！
    JsonObject obj = doc["params"].to<JsonObject>();
    obj["engineStartPullLowTime"].to<JsonObject>();
    obj["engineStartWaitTime"].to<JsonObject>();
    obj["engineStopPullLowTime"].to<JsonObject>();
    obj["engineStopWaitTime"].to<JsonObject>();
    obj["rpmCalibration"].to<JsonObject>();
    obj["engineOnRpmThreshold"].to<JsonObject>();
    obj["wakePulseMs"].to<JsonObject>();
    obj["wakeToLongMs"].to<JsonObject>();
    obj["auto_update"].to<JsonObject>();  // OTA自动更新字段
    obj["onff"].to<JsonObject>();  // 开关状态字段
    // 新增热配置字段
    // 修正：使用平台物模型的下划线命名
    obj["rpm_alarm_threshold"].to<JsonObject>();
    obj["maintenance_interval_hours"].to<JsonObject>();
    obj["wifi_rssi_weak_threshold"].to<JsonObject>();
    obj["memory_warning_threshold"].to<JsonObject>();

    // 序列化
    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    DEBUG_PRINT("[OneNET] 请求清除所有期望属性Payload：");
    DEBUG_PRINTLN(buf);

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/desired/delete";
    if (mqttClient.connected()) {
        bool ok = mqttClient.publish(topic.c_str(), buf, len);
        (void)ok; // 抑制未使用变量警告
        DEBUG_PRINTF("[OneNET] 清除期望属性已请求\n");
    } else {
        DEBUG_PRINTLN("[OneNET] MQTT未连接，无法清除期望属性");
    }
}



// MQTT回调
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // ========== 栈保护模式检查 ==========
    if (g_stackLowMode) {
        DEBUG_PRINTF("[栈保护] 栈保护模式下拒绝MQTT消息: %s\n", topic);
        wdt_safe_reset();
        return;
    }
    
    // ========== 全局MQTT消息频率保护 ==========
    unsigned long now = millis();
    unsigned long nowSec = now / 1000;
    
    // 全局频率限制：使用配置的最小间隔
    if (now - lastMqttCallbackTime < Config::MQTT_MIN_INTERVAL) {
        DEBUG_PRINTF("[全局节流] MQTT消息过快(<%dms)，已丢弃: %s\n", 
                     (int)(now - lastMqttCallbackTime), topic);
        wdt_safe_reset(); // 即使丢弃也要喂狗
        return;
    }
    
    // 全局频率限制：使用配置的最大每秒消息数
    if (nowSec != lastMqttCallbackSec) {
        mqttCallbackCountPerSec = 0;
        lastMqttCallbackSec = nowSec;
    }
    if (mqttCallbackCountPerSec >= Config::MQTT_GLOBAL_MAX_PER_SEC) {
        DEBUG_PRINTF("[全局节流] MQTT本秒超%d条消息，已丢弃: %s\n", Config::MQTT_GLOBAL_MAX_PER_SEC, topic);
        wdt_safe_reset();
        return;
    }
    
    lastMqttCallbackTime = now;
    mqttCallbackCountPerSec++;
    
    // 检查payload长度，防止超大消息
    if (length > 2048) {
        DEBUG_PRINTF("[安全] MQTT消息过大(%u bytes)，已丢弃: %s\n", length, topic);
        wdt_safe_reset();
        return;
    }
    
    // 定期喂狗
    wdt_safe_reset();
    
    DEBUG_PRINTF("[WiFi MQTT] 收到消息 [%s] (%u bytes)\n", topic, length);
    String topicStr = String(topic);
    
    // ========== 认证错误检测和Token刷新 ==========
    // 检查消息中是否包含认证失败信息
    String payloadStr = String((char*)payload, length);
    if (payloadStr.indexOf("authentication failed") >= 0 || 
        payloadStr.indexOf("request has expired") >= 0 ||
        payloadStr.indexOf("10403") >= 0) {
        DEBUG_PRINTF("[Token] 检测到认证错误消息: %s\n", payloadStr.c_str());
        g_forceTokenRefresh = true; // 标记需要强制刷新Token
        return;
    }

    // 1. 事件上报响应
    String eventReplyTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post/reply";
    if (topicStr == eventReplyTopic) {
        DEBUG_PRINTF("[事件上报] 平台回复: %.*s\n", length, payload);
        
        // 检查事件上报响应中的认证错误
        static JsonDocument eventDoc;
        eventDoc.clear();
        DeserializationError error = deserializeJson(eventDoc, payload, length);
        if (!error) {
            int code = eventDoc["code"] | 0;
            if (code == 10403) {
                DEBUG_PRINTLN("[Token] 事件上报返回认证错误，标记Token刷新");
                g_forceTokenRefresh = true;
            }
        }
        return;
    }

    // 2. 属性设置处理（平台set/set_reply）
    String propertySetTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/set";
    if (topicStr == propertySetTopic) {
        // ========== 属性设置节流保护 ==========
        if (now - lastPropertySetTime < Config::MQTT_PROPERTY_INTERVAL) {
            DEBUG_PRINTF("[属性SET节流] 请求过快(<%dms)，已丢弃\n", 
                         (int)(now - lastPropertySetTime));
            wdt_safe_reset();
            return;
        }
        
        if (nowSec != lastPropertySetSec) {
            propertySetCountPerSec = 0;
            lastPropertySetSec = nowSec;
        }
        if (propertySetCountPerSec >= Config::MQTT_PROPERTY_MAX_PER_SEC) {
            DEBUG_PRINTF("[属性SET节流] 本秒超%d次，已丢弃\n", Config::MQTT_PROPERTY_MAX_PER_SEC);
            wdt_safe_reset();
            return;
        }
        
        lastPropertySetTime = now;
        propertySetCountPerSec++;
        
        // ========== 使用静态JSON文档防栈溢出 ==========
        static JsonDocument setDoc;
        setDoc.clear();
        
        DeserializationError error = deserializeJson(setDoc, payload, length);
        if (error) {
            DEBUG_PRINTF("[属性SET] JSON解析错误: %s\n", error.c_str());
            wdt_safe_reset();
            return;
        }
        
        wdt_safe_reset(); // JSON处理后喂狗
        
        if (setDoc["params"].is<JsonObject>()) {
            JsonObject params = setDoc["params"].as<JsonObject>();
            
            // ---- 热加载参数和本地属性同步 ----
            if (params["engineStartPullLowTime"].is<unsigned long>())
                gConfig.engineStartPullLowTime = params["engineStartPullLowTime"];
            if (params["engineStartWaitTime"].is<unsigned long>())
                gConfig.engineStartWaitTime = params["engineStartWaitTime"];
            if (params["engineStopPullLowTime"].is<unsigned long>())
                gConfig.engineStopPullLowTime = params["engineStopPullLowTime"];
            if (params["engineStopWaitTime"].is<unsigned long>())
                gConfig.engineStopWaitTime = params["engineStopWaitTime"];
            if (params["rpmCalibration"].is<float>() || params["rpmCalibration"].is<double>() || params["rpmCalibration"].is<int>())
                gConfig.rpmCalibration = params["rpmCalibration"];
            if (params["engineOnRpmThreshold"].is<int>())
                gConfig.engineOnRpmThreshold = params["engineOnRpmThreshold"];
            if (params["wakePulseMs"].is<unsigned long>())
                gConfig.wakePulseMs = params["wakePulseMs"];
            if (params["wakeToLongMs"].is<unsigned long>())
                gConfig.wakeToLongMs = params["wakeToLongMs"];
            
            // ========== OTA自动更新字段处理 ==========
            if (params["auto_update"].is<bool>()) {
                bool newAutoUpdate = params["auto_update"];
                if (g_auto_update != newAutoUpdate) {
                    g_auto_update = newAutoUpdate;
                    DEBUG_PRINTF("[OTA] auto_update已通过set更新为: %s\n", g_auto_update ? "true" : "false");
                    
                    // 保存到NVS
                    Preferences otaPrefs;
                    otaPrefs.begin("ota", false);
                    otaPrefs.putBool("auto_update", g_auto_update);
                    otaPrefs.end();
                }
            }
            
            // ========== 修正热配置参数在线设置处理，使用平台物模型下划线命名 ==========
            if (params["rpm_alarm_threshold"].is<int>()) {
                gConfig.rpmAlarmThreshold = params["rpm_alarm_threshold"];
                DEBUG_PRINTF("[MQTT SET] rpm_alarm_threshold => %d\n", gConfig.rpmAlarmThreshold);
            }
            
            if (params["maintenance_interval_hours"].is<int>()) {
                gConfig.maintenanceIntervalHours = params["maintenance_interval_hours"];
                DEBUG_PRINTF("[MQTT SET] maintenance_interval_hours => %d\n", gConfig.maintenanceIntervalHours);
            }
            
            if (params["wifi_rssi_weak_threshold"].is<int>()) {
                gConfig.wifiRssiWeakThreshold = params["wifi_rssi_weak_threshold"];
                DEBUG_PRINTF("[MQTT SET] wifi_rssi_weak_threshold => %d\n", gConfig.wifiRssiWeakThreshold);
            }
            
            if (params["memory_warning_threshold"].is<unsigned long>()) {
                gConfig.memoryWarningThreshold = params["memory_warning_threshold"];
                DEBUG_PRINTF("[MQTT SET] memory_warning_threshold => %lu\n", gConfig.memoryWarningThreshold);
            }
            
            if (params["ota_check_interval_minutes"].is<int>()) {
                uint16_t newInterval = params["ota_check_interval_minutes"];
                if (newInterval >= 5 && newInterval <= 1440) { // 限制5分钟到24小时
                    gConfig.otaCheckIntervalMinutes = newInterval;
                    DEBUG_PRINTF("[MQTT SET] ota_check_interval_minutes => %d分钟\n", gConfig.otaCheckIntervalMinutes);
                } else {
                    DEBUG_PRINTF("[MQTT SET] ota_check_interval_minutes 值无效(%d)，需要5-1440分钟\n", newInterval);
                }
            }

            wdt_safe_reset(); // 参数更新后喂狗
            
            // ========== 立即保存所有热配置参数到NVS ==========
            updateHotConfigFromParams(params);

            // ---- 远程开关机 ----
            if (params["onff"].is<bool>()) {
                deviceStatus = params["onff"];
                gConfig.deviceStatus = deviceStatus; // 同步到热配置
                DEBUG_PRINTF("[WiFi] 指令 => %s\n", deviceStatus ? "ON" : "OFF");
                
                // 保存设备状态到NVS
                Preferences hotPrefs;
                hotPrefs.begin("hotcfg", false);
                hotPrefs.putBool("devStatus", gConfig.deviceStatus);
                hotPrefs.end();
                
                updateEngineState();
                g_needReportSync = true; // 改为设置标志，避免在MQTT回调中直接上报
            }

            // ---- 远程重启处理 ----
            if (params["reboot"].is<bool>() && params["reboot"] == true) {
                static JsonDocument replyDoc;
                replyDoc.clear();
                replyDoc["id"]   = setDoc["id"] | "";
                if (params["reboot_code"].is<const char*>() &&
                    String(params["reboot_code"].as<const char*>()) == Config::REMOTE_REBOOT_CODE) {
                    DEBUG_PRINTLN("[MQTT] 收到合法重启指令，正在重启...");
                    replyDoc["code"] = 0;
                    replyDoc["msg"]  = "rebooting";
                    replyDoc["data"].to<JsonObject>();
                    char outBuf[128];
                    serializeJson(replyDoc, outBuf, sizeof(outBuf));
                    String responseTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/set_reply";
                    
                    // 使用非阻塞发布，避免卡死
                    if (mqttClient.connected()) {
                        mqttClient.publish(responseTopic.c_str(), outBuf, 0);
                        delay(500); // 短暂等待发送
                    }
                    
                    Preferences rebootPrefs;
                    rebootPrefs.begin("reboot", false);
                    rebootPrefs.putBool(Config::REMOTE_REBOOT_FLAG_KEY, true);
                    rebootPrefs.end();
                    ESP.restart();
                    return;
                } else {
                    DEBUG_PRINTLN("[MQTT] 收到非法重启指令，已拒绝（reboot_code 不符）");
                    replyDoc["code"] = 403;
                    replyDoc["msg"]  = "reboot_code error";
                    replyDoc["data"].to<JsonObject>();
                    char outBuf[128];
                    serializeJson(replyDoc, outBuf, sizeof(outBuf));
                    String responseTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/set_reply";
                    
                    // 使用非阻塞发布
                    if (mqttClient.connected()) {
                        mqttClient.publish(responseTopic.c_str(), outBuf, 0);
                    }
                    return;
                }
            }

            // ---- set 操作正常响应 ----
            static JsonDocument normalReplyDoc;
            normalReplyDoc.clear();
            normalReplyDoc["id"]   = setDoc["id"] | "";
            normalReplyDoc["code"] = 0;
            normalReplyDoc["msg"]  = "success";
            normalReplyDoc["data"].to<JsonObject>();
            char replyPayload[128];
            serializeJson(normalReplyDoc, replyPayload, sizeof(replyPayload));
            String responseTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/set_reply";
            
            // 使用非阻塞发布
            if (mqttClient.connected()) {
                mqttClient.publish(responseTopic.c_str(), replyPayload, 0);
            }
            
            wdt_safe_reset(); // 发布后喂狗
        }
        return;
    }

    // 2.1 属性期望值清除 desired/delete/reply
    String desiredDeleteReplyTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/desired/delete/reply";
    if (topicStr == desiredDeleteReplyTopic) {
        // ========== 期望属性节流保护 ==========
        if (now - lastDesiredProcessTime < 250) { // 250ms间隔，期望属性处理稍宽松
            DEBUG_PRINTF("[期望属性节流] 请求过快(<%dms)，已丢弃\n", 
                         (int)(now - lastDesiredProcessTime));
            wdt_safe_reset();
            return;
        }
        
        if (nowSec != lastDesiredProcessSec) {
            desiredProcessCountPerSec = 0;
            lastDesiredProcessSec = nowSec;
        }
        if (desiredProcessCountPerSec >= 3) { // 提升至3次/秒
            DEBUG_PRINTLN("[期望属性节流] 本秒超3次，已丢弃");
            wdt_safe_reset();
            return;
        }
        
        lastDesiredProcessTime = now;
        desiredProcessCountPerSec++;
        
        static JsonDocument desiredDelDoc;
        desiredDelDoc.clear();
        DeserializationError error = deserializeJson(desiredDelDoc, payload, length);
        if (error) {
            DEBUG_PRINTF("[OneNET] desired/delete/reply JSON解析错误: %s\n", error.c_str());
            wdt_safe_reset();
            return;
        }
        int code = desiredDelDoc["code"] | -1;
        const char* msg = desiredDelDoc["msg"] | "";
        if (code == 200) {
            DEBUG_PRINTLN("[OneNET] 期望属性清除成功！");
        } else {
            (void)msg; // 抑制未使用变量警告
            DEBUG_PRINTF("[OneNET] 期望属性清除失败\n");
        }
        wdt_safe_reset();
        return;
    }

    // 2.2 属性期望值 desired/get/reply
    String desiredGetReplyTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/desired/get/reply";
    if (topicStr == desiredGetReplyTopic) {
        // ========== 期望属性GET节流保护（复用计数器） ==========
        if (now - lastDesiredProcessTime < 250) { // 250ms间隔，保持一致
            DEBUG_PRINTF("[期望属性GET节流] 请求过快(<%dms)，已丢弃\n", 
                         (int)(now - lastDesiredProcessTime));
            wdt_safe_reset();
            return;
        }
        
        if (nowSec != lastDesiredProcessSec) {
            desiredProcessCountPerSec = 0;
            lastDesiredProcessSec = nowSec;
        }
        if (desiredProcessCountPerSec >= 3) { // 提升至3次/秒，保持一致
            DEBUG_PRINTLN("[期望属性GET节流] 本秒超3次，已丢弃");
            wdt_safe_reset();
            return;
        }
        
        lastDesiredProcessTime = now;
        desiredProcessCountPerSec++;
        
        static JsonDocument desiredGetDoc;
        desiredGetDoc.clear();
        DeserializationError error = deserializeJson(desiredGetDoc, payload, length);
        if (error) {
            DEBUG_PRINTF("[OneNET] desired/reply JSON解析错误: %s\n", error.c_str());
            wdt_safe_reset();
            return;
        }
        int code = desiredGetDoc["code"] | -1;
        if (code != 200) {
            String errorMsg = desiredGetDoc["msg"] | "";
            // 对于 auto_update 字段缺失的情况，给出友好提示
            if (errorMsg.indexOf("auto_update") != -1) {
                DEBUG_PRINTLN("[OneNET] 平台端尚未配置auto_update字段，将使用默认值true");
            } else {
                DEBUG_PRINTF("[OneNET] 获取期望属性失败: %s\n", errorMsg.c_str());
            }
            
            // 即使获取失败，也标记为已同步，避免OTA任务无限等待
            g_desiredPropertySynced = true;
            
            wdt_safe_reset();
            return;
        }
        JsonObject data = desiredGetDoc["data"];
        if (!data.isNull()) {
            bool updated = false;
            int processCount = 0;
            for (JsonPair kv : data) {
                // 限制单次处理的属性数量，防止卡死
                if (++processCount > 20) {
                    DEBUG_PRINTLN("[OneNET] 单次期望属性数量过多，截断处理");
                    break;
                }
                
                String key = kv.key().c_str();
                JsonVariant val = kv.value()["value"];
                DEBUG_PRINTF("[OneNET] 期望属性 %s = %s\n", key.c_str(), val.as<String>().c_str());
                // 跟set处理一致，全部本地同步
                if (key == "onff" && val.is<bool>()) {
                    deviceStatus = val.as<bool>();
                    updateEngineState();
                    updated = true;
                }
                else if (key == "engineStartPullLowTime" && val.is<unsigned long>()) {
                    gConfig.engineStartPullLowTime = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "engineStartWaitTime" && val.is<unsigned long>()) {
                    gConfig.engineStartWaitTime = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "engineStopPullLowTime" && val.is<unsigned long>()) {
                    gConfig.engineStopPullLowTime = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "engineStopWaitTime" && val.is<unsigned long>()) {
                    gConfig.engineStopWaitTime = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "rpmCalibration" && (val.is<float>() || val.is<double>() || val.is<int>())) {
                    gConfig.rpmCalibration = val.as<float>();
                    updated = true;
                }
                else if (key == "engineOnRpmThreshold" && val.is<int>()) {
                    gConfig.engineOnRpmThreshold = val.as<int>();
                    updated = true;
                }
                else if (key == "wakePulseMs" && val.is<unsigned long>()) {
                    gConfig.wakePulseMs = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "wakeToLongMs" && val.is<unsigned long>()) {
                    gConfig.wakeToLongMs = val.as<unsigned long>();
                    updated = true;
                }
                else if (key == "auto_update" && val.is<bool>()) {
                    bool newAutoUpdate = val.as<bool>();
                    if (g_auto_update != newAutoUpdate) {
                        g_auto_update = newAutoUpdate;
                        DEBUG_PRINTF("[OTA] auto_update已更新为: %s\n", g_auto_update ? "true" : "false");
                        
                        // 保存到NVS
                        Preferences otaPrefs;
                        otaPrefs.begin("ota", false);
                        otaPrefs.putBool("auto_update", g_auto_update);
                        otaPrefs.end();
                    }
                    updated = true;
                }
                
                // 每处理几个属性就喂一次狗
                if (processCount % 3 == 0) {
                    wdt_safe_reset();
                }
            }
            if (updated) {
                DEBUG_PRINTLN("[OneNET] 期望属性已同步到本地变量");
                
                // ========== 立即保存热配置参数到NVS ==========
                configPrefs.begin("hotcfg", false);
                configPrefs.putULong("startLow", gConfig.engineStartPullLowTime);
                configPrefs.putULong("startWait", gConfig.engineStartWaitTime);
                configPrefs.putULong("stopLow", gConfig.engineStopPullLowTime);
                configPrefs.putULong("stopWait", gConfig.engineStopWaitTime);
                configPrefs.putFloat("rpmCalib", gConfig.rpmCalibration);
                configPrefs.putInt("rpmThres", gConfig.engineOnRpmThreshold);
                configPrefs.putULong("wakePulse", gConfig.wakePulseMs);
                configPrefs.putULong("wakeToLong", gConfig.wakeToLongMs);
                configPrefs.putBool("devStatus", gConfig.deviceStatus);
                configPrefs.putUShort("rpmAlarm", gConfig.rpmAlarmThreshold);
                configPrefs.putUShort("maintHours", gConfig.maintenanceIntervalHours);
                configPrefs.putShort("wifiWeak", gConfig.wifiRssiWeakThreshold);
                configPrefs.putULong("memWarn", gConfig.memoryWarningThreshold);
                configPrefs.putUShort("otaInterval", gConfig.otaCheckIntervalMinutes);
                configPrefs.end();
                DEBUG_PRINTLN("[期望属性] 热配置参数已保存到NVS");
                
                g_needReportSync = true;   // 仅设flag
                
                // 标记期望属性已同步完成
                g_desiredPropertySynced = true;
                
                // 立即强制上报同步后的数据
                reportSensorData(true);
            }
        } else {
            // 期望属性数据为空，也标记为已同步
            DEBUG_PRINTLN("[OneNET] 期望属性数据为空，标记为已同步");
            g_desiredPropertySynced = true;
        }
        wdt_safe_reset();
        return;
    }

    // 3. 属性查询处理
    String propertyGetTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/get";
    if (topicStr == propertyGetTopic) {

        // ★ 节流保护，务必在第一行
        unsigned long now = millis();
        unsigned long nowSec = now / 1000;
        if (now - lastGetReplyTime < Config::MQTT_PROPERTY_INTERVAL) {
            DEBUG_PRINTF("[GET节流] 属性查询过快(<%dms)，已丢弃\n", 
                         (int)(now - lastGetReplyTime));
            return;
        }
        if (nowSec != lastGetReplySec) {
            getReplyCountPerSec = 0;
            lastGetReplySec = nowSec;
        }
        if (getReplyCountPerSec >= 3) { // 查询操作适当放宽至3次/秒
            DEBUG_PRINTLN("[GET节流] 属性查询本秒超3次，已丢弃");
            return;
        }
        lastGetReplyTime = now;
        getReplyCountPerSec++;

        // ========== 栈保护：检查栈空间 ==========
        UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
        if (stackRemaining < 800) { // 少于800字节栈空间时拒绝处理
            DEBUG_PRINTF("[栈保护] get属性处理被拒绝，栈空间不足: %u bytes\n", stackRemaining);
            wdt_safe_reset();
            return;
        }

        // ========== 静态全局JSON变量防栈爆 ==========
        staticGetDoc.clear();
        DeserializationError error = deserializeJson(staticGetDoc, payload, length);
        if (error) {
            DEBUG_PRINTLN("[属性GET] JSON解析错误");
            wdt_safe_reset();
            return;
        }
        if (!staticGetDoc["params"].is<JsonArray>()) {
            wdt_safe_reset();
            return;
        }
        JsonArray reqParams = staticGetDoc["params"].as<JsonArray>();

        // ★ 调整参数数量限制，支持完整的工业级属性
        if (reqParams.size() > 50) { // 提升至35个以支持完整工业级物模型
            DEBUG_PRINTF("[属性GET] 请求参数过多(%u个)，截断处理\n", reqParams.size());
        }

        // 用静态全局变量
        staticReplyDoc.clear();
        staticReplyDoc["id"] = staticGetDoc["id"] | "";
        staticReplyDoc["code"] = 0;
        staticReplyDoc["msg"] = "success";
        JsonObject data = staticReplyDoc["data"].to<JsonObject>();

        // ★ 只采集一次数据，防递归栈爆
        SensorData _sensor = readSensorData();
        bool _engine_actual = isEngineRunning();

        int processCount = 0;
        for (JsonVariant v : reqParams) {
            // 限制单次处理数量，支持完整工业级物模型
            if (++processCount > 50) {
                DEBUG_PRINTLN("[属性GET] 单次处理参数过多，截断");
                break;
            }
            
            String key = v.as<String>();
            if (key == "engineStartPullLowTime")      data[key] = gConfig.engineStartPullLowTime;
            else if (key == "engineStartWaitTime")    data[key] = gConfig.engineStartWaitTime;
            else if (key == "engineStopPullLowTime")  data[key] = gConfig.engineStopPullLowTime;
            else if (key == "engineStopWaitTime")     data[key] = gConfig.engineStopWaitTime;
            else if (key == "rpmCalibration")         data[key] = gConfig.rpmCalibration;
            else if (key == "engineOnRpmThreshold")   data[key] = gConfig.engineOnRpmThreshold;
            else if (key == "version")                data[key] = setnewFirmwareVersion;
            else if (key == "online")                 data[key] = (WiFi.status() == WL_CONNECTED && mqttClient.connected());
            else if (key == "oil_level")              data[key] = _sensor.oil_level;
            else if (key == "temp")                   data[key] = _sensor.temperature;
            else if (key == "rpm")                    data[key] = _sensor.rpm;
            else if (key == "onff")                   data[key] = deviceStatus;
            else if (key == "engine_actual")          data[key] = _engine_actual;
            else if (key == "oil_alarm")              data[key] = _sensor.oil_alarm;
            else if (key == "net")                    data[key] = (WiFi.status() == WL_CONNECTED && mqttClient.connected());
            else if (key == "timestamp")              data[key] = (uint64_t)getCurrentTime() * 1000ULL;
            else if (key == "wakePulseMs")            data[key] = gConfig.wakePulseMs;
            else if (key == "wakeToLongMs")           data[key] = gConfig.wakeToLongMs;
            else if (key == "wifi_rssi")              data[key] = getWifiRSSI();
                    // ========== 新增：OTA相关字段 ==========
        else if (key == "auto_update")            data[key] = g_auto_update;
        else if (key == "ota_state")              data[key] = g_ota_state;
        else if (key == "last_restart_reason")    data[key] = lastRestartReason;
        
        // ========== 工业级统计属性 GET 支持 ==========
        else if (key == "engine_total_hours")     data[key] = (int)(g_stats.engineTotalMinutes / 60);
        else if (key == "engine_total_minutes")   data[key] = g_stats.engineTotalMinutes;
        else if (key == "engine_start_count")     data[key] = g_stats.engineStartCount;
        else if (key == "oil_alarm_count")        data[key] = g_stats.oilAlarmCount;
        else if (key == "wifi_disconnect_count")  data[key] = g_stats.wifiDisconnectCount;
        else if (key == "mqtt_failure_count")     data[key] = g_stats.mqttFailureCount;
        else if (key == "memory_warning_count")   data[key] = g_stats.memoryWarningCount;
        else if (key == "restart_count_today")    data[key] = g_stats.restartCountToday;
        else if (key == "maintenance_interval_hours") data[key] = gConfig.maintenanceIntervalHours;
        else if (key == "rpm_alarm_threshold")    data[key] = gConfig.rpmAlarmThreshold;
        else if (key == "uptime_seconds")         data[key] = millis() / 1000;
        else if (key == "free_heap")              data[key] = ESP.getFreeHeap();
        else if (key == "ota_check_interval_minutes") data[key] = gConfig.otaCheckIntervalMinutes;
        
        // ========== 新增热配置参数 GET 支持 ==========
        else if (key == "wifi_rssi_weak_threshold") data[key] = gConfig.wifiRssiWeakThreshold;
        else if (key == "memory_warning_threshold") data[key] = gConfig.memoryWarningThreshold;
        
        // ========== 新增缺失的属性支持 ==========
        else if (key == "battery_voltage") {
            // 如果没有电池电压传感器，返回默认值或模拟值
            data[key] = 12.6; // 默认12.6V，您可以根据实际硬件修改
        }
        else if (key == "ota_rssi_min") {
            data[key] = Config::OTA_RSSI_MIN; // 使用已定义的常量
        }

            // 每处理8个参数就喂一次狗和检查栈
            if (processCount % 8 == 0) {
                wdt_safe_reset();
                UBaseType_t stackNow = uxTaskGetStackHighWaterMark(NULL);
                if (stackNow < 600) {
                    DEBUG_PRINTF("[栈保护] get属性处理中栈不足，提前终止: %u bytes\n", stackNow);
                    break;
                }
            }
        }

        // 静态全局buffer输出
        memset(staticGetOutBuf, 0, sizeof(staticGetOutBuf));
        serializeJson(staticReplyDoc, staticGetOutBuf, sizeof(staticGetOutBuf));
        String replyTopic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/get_reply";
        
        // 使用非阻塞发布，避免长时间阻塞
        if (mqttClient.connected()) {
            mqttClient.publish(replyTopic.c_str(), staticGetOutBuf, 0);
        }
        
        DEBUG_PRINTF("[OneNET] get属性已回复: %s\n", staticGetOutBuf);
        wdt_safe_reset();
        return;
    }

    // 4. 服务调用处理
    String servicePrefix = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/service/";
    if (topicStr.startsWith(servicePrefix) && topicStr.endsWith("/invoke")) {
        // ========== 服务调用节流保护 ==========
        if (now - lastServiceInvokeTime < 400) { // 400ms间隔，服务调用适度放宽
            DEBUG_PRINTF("[服务调用节流] 请求过快(<%dms)，已丢弃\n", 
                         (int)(now - lastServiceInvokeTime));
            wdt_safe_reset();
            return;
        }
        
        if (nowSec != lastServiceInvokeSec) {
            serviceInvokeCountPerSec = 0;
            lastServiceInvokeSec = nowSec;
        }
        if (serviceInvokeCountPerSec >= 3) { // 提升至3次/秒，改善工业应用响应性
            DEBUG_PRINTLN("[服务调用节流] 本秒超3次，已丢弃");
            wdt_safe_reset();
            return;
        }
        
        lastServiceInvokeTime = now;
        serviceInvokeCountPerSec++;
        
        int s = servicePrefix.length();
        int e = topicStr.lastIndexOf("/invoke");
        String identifier = topicStr.substring(s, e);

        static JsonDocument serviceDoc;
        serviceDoc.clear();
        DeserializationError error = deserializeJson(serviceDoc, payload, length);
        if (error) {
            DEBUG_PRINTF("[服务] JSON解析错误: %s\n", error.c_str());
            wdt_safe_reset();
            return;
        }
        
        wdt_safe_reset(); // JSON处理后喂狗

        if (identifier == "remote_reboot") {
            JsonObject params = serviceDoc["params"].as<JsonObject>();
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"]   = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            if (params["reboot"].is<bool>() && params["reboot"] == true &&
                params["reboot_code"].is<const char*>() &&
                String(params["reboot_code"].as<const char*>()) == Config::REMOTE_REBOOT_CODE) {
                serviceReplyDoc["code"] = 0;
                serviceReplyDoc["msg"]  = "rebooting";
                serviceReplyDoc["data"].to<JsonObject>();
                char outBuf[256];
                serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
                
                // 使用非阻塞发布
                if (mqttClient.connected()) {
                    mqttClient.publish(replyTopic.c_str(), outBuf, 0);
                    delay(500); // 等待发送
                }

                Preferences rebootPrefs;
                rebootPrefs.begin("reboot", false);
                rebootPrefs.putBool(Config::REMOTE_REBOOT_FLAG_KEY, true);
                rebootPrefs.end();
                ESP.restart();
                return;
            } else {
                serviceReplyDoc["code"] = 403;
                serviceReplyDoc["msg"]  = "reboot_code error";
                serviceReplyDoc["data"].to<JsonObject>();
                char outBuf[256];
                serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
                
                // 使用非阻塞发布
                if (mqttClient.connected()) {
                    mqttClient.publish(replyTopic.c_str(), outBuf, 0);
                }
                wdt_safe_reset();
                return;
            }
        } else if (identifier == "start_update") {
            // ========== start_update 服务处理 ==========
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            // 强制输出调试信息，即使DEBUG_MODE=0
            Serial.println("[OTA] 收到start_update服务调用，立即触发OTA检查");
            Serial.printf("[OTA] 当前状态: g_forceOTAStart=%s, g_auto_update=%s, g_desiredPropertySynced=%s\n", 
                         g_forceOTAStart ? "true" : "false",
                         g_auto_update ? "true" : "false", 
                         g_desiredPropertySynced ? "true" : "false");
            
            g_forceOTAStart = true; // 设置强制OTA标志
            g_lastOTACheck = 0;     // 重置OTA检查时间，立即执行
            
            Serial.printf("[OTA] 已设置标志: g_forceOTAStart=%s, g_lastOTACheck=%lu\n", 
                         g_forceOTAStart ? "true" : "false", g_lastOTACheck);
            
            serviceReplyDoc["code"] = 0;
            serviceReplyDoc["msg"]  = "OTA check triggered";
            serviceReplyDoc["data"].to<JsonObject>();
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            // 使用非阻塞发布
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
        
        } else if (identifier == "reset_version") {
            // ========== 版本号重置服务处理 ==========
            JsonObject params = serviceDoc["params"].as<JsonObject>();
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            int oldVersion = setnewFirmwareVersion;
            int newVersion = 1; // 默认重置到版本1
            
            // 如果指定了目标版本号
            if (params["target_version"].is<int>()) {
                newVersion = params["target_version"];
                if (newVersion < 1 || newVersion > 9999) {
                    serviceReplyDoc["code"] = 400;
                    serviceReplyDoc["msg"] = "Invalid version range (1-9999)";
                    serviceReplyDoc["data"].to<JsonObject>();
                } else {
                    // 执行版本重置
                    Preferences otaPrefs;
                    otaPrefs.begin("ota", false);
                    otaPrefs.putInt("version", newVersion);
                    otaPrefs.end();
                    
                    setnewFirmwareVersion = newVersion;
                    
                    serviceReplyDoc["code"] = 0;
                    serviceReplyDoc["msg"] = "Version reset successfully";
                    JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                    data["old_version"] = oldVersion;
                    data["new_version"] = newVersion;
                    
                    DEBUG_PRINTF("[版本重置] 版本已从 %d 重置为 %d\n", oldVersion, newVersion);
                }
            } else {
                // 重置到默认版本1
                Preferences otaPrefs;
                otaPrefs.begin("ota", false);
                otaPrefs.putInt("version", newVersion);
                otaPrefs.end();
                
                setnewFirmwareVersion = newVersion;
                
                serviceReplyDoc["code"] = 0;
                serviceReplyDoc["msg"] = "Version reset to default";
                JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                data["old_version"] = oldVersion;
                data["new_version"] = newVersion;
                
                DEBUG_PRINTF("[版本重置] 版本已从 %d 重置为默认值 %d\n", oldVersion, newVersion);
            }
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            // 使用非阻塞发布
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
            
        } else if (identifier == "force_report") {
            // ========== 强制立即上报数据服务 ==========
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            DEBUG_PRINTLN("[服务] 收到force_report调用，立即上报数据");
            reportSensorData(true); // 强制立即上报
            
            serviceReplyDoc["code"] = 0;
            serviceReplyDoc["msg"] = "Data reported successfully";
            JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
            data["result"] = true;
            data["message"] = "All sensor data has been reported";
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
            
        } else if (identifier == "reset_counters") {
            // ========== 重置所有统计计数器服务 ==========
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            DEBUG_PRINTLN("[服务] 收到reset_counters调用，重置所有统计计数器");
            
            // 重置统计计数器
            g_stats.oilAlarmCount = 0;
            g_stats.wifiDisconnectCount = 0;
            g_stats.mqttFailureCount = 0;
            g_stats.memoryWarningCount = 0;
            g_stats.restartCountToday = 0;
            
            // 保存到NVS
            saveIndustrialStats();
            
            serviceReplyDoc["code"] = 0;
            serviceReplyDoc["msg"] = "All counters reset successfully";
            JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
            data["result"] = true;
            data["message"] = "Oil alarm, WiFi disconnect, MQTT failure, memory warning and restart counters have been reset";
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
            
        } else if (identifier == "reset_engine_hours") {
            // ========== 重置发动机运行时间服务 ==========
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            DEBUG_PRINTLN("[服务] 收到reset_engine_hours调用，重置发动机运行统计");
            
            uint32_t oldMinutes = g_stats.engineTotalMinutes;
            uint32_t oldStartCount = g_stats.engineStartCount;
            
            // 重置发动机统计
            g_stats.engineTotalMinutes = 0;
            g_stats.engineStartCount = 0;
            
            // 保存到NVS
            saveIndustrialStats();
            
            serviceReplyDoc["code"] = 0;
            serviceReplyDoc["msg"] = "Engine hours reset successfully";
            JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
            data["result"] = true;
            data["message"] = "Engine total minutes and start count have been reset";
            data["old_minutes"] = oldMinutes;
            data["old_start_count"] = oldStartCount;
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
            
        } else if (identifier == "update_maintenance_config") {
            // ========== 更新维护配置服务 ==========
            JsonObject params = serviceDoc["params"].as<JsonObject>();
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            bool updated = false;
            String updateMsg = "";
            
            // 检查并更新维护间隔
            if (params["maintenance_interval_hours"].is<int>()) {
                int newInterval = params["maintenance_interval_hours"];
                if (newInterval >= 50 && newInterval <= 2000) {
                    gConfig.maintenanceIntervalHours = newInterval;
                    updated = true;
                    updateMsg += "维护间隔: " + String(newInterval) + "h ";
                } else {
                    serviceReplyDoc["code"] = 400;
                    serviceReplyDoc["msg"] = "Invalid maintenance interval (50-2000 hours)";
                    JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                    data["result"] = false;
                    data["message"] = "Maintenance interval must be between 50-2000 hours";
                    
                    char outBuf[256];
                    serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
                    if (mqttClient.connected()) {
                        mqttClient.publish(replyTopic.c_str(), outBuf, 0);
                    }
                    wdt_safe_reset();
                    return;
                }
            }
            
            // 检查并更新转速报警阈值
            if (params["rpm_alarm_threshold"].is<int>()) {
                int newThreshold = params["rpm_alarm_threshold"];
                if (newThreshold >= 1500 && newThreshold <= 4000) {
                    gConfig.rpmAlarmThreshold = newThreshold;
                    updated = true;
                    updateMsg += "转速阈值: " + String(newThreshold) + "rpm ";
                } else {
                    serviceReplyDoc["code"] = 400;
                    serviceReplyDoc["msg"] = "Invalid RPM threshold (1500-4000 rpm)";
                    JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                    data["result"] = false;
                    data["message"] = "RPM threshold must be between 1500-4000 rpm";
                    
                    char outBuf[256];
                    serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
                    if (mqttClient.connected()) {
                        mqttClient.publish(replyTopic.c_str(), outBuf, 0);
                    }
                    wdt_safe_reset();
                    return;
                }
            }
            
            if (updated) {
                // 保存到NVS
                configPrefs.begin("hotcfg", false);
                configPrefs.putUShort("maintHours", gConfig.maintenanceIntervalHours);
                configPrefs.putUShort("rpmAlarm", gConfig.rpmAlarmThreshold);
                configPrefs.end();
                
                serviceReplyDoc["code"] = 0;
                serviceReplyDoc["msg"] = "Configuration updated successfully";
                JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                data["result"] = true;
                data["message"] = updateMsg.c_str();
                data["maintenance_interval_hours"] = gConfig.maintenanceIntervalHours;
                data["rpm_alarm_threshold"] = gConfig.rpmAlarmThreshold;
                
                DEBUG_PRINTF("[服务] 维护配置已更新: %s\n", updateMsg.c_str());
            } else {
                serviceReplyDoc["code"] = 400;
                serviceReplyDoc["msg"] = "No valid parameters provided";
                JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
                data["result"] = false;
                data["message"] = "No valid maintenance configuration parameters found";
            }
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
            wdt_safe_reset();
            return;
            
        } else {
            // ========== 未知服务处理 ==========
            static JsonDocument serviceReplyDoc;
            serviceReplyDoc.clear();
            serviceReplyDoc["id"] = serviceDoc["id"] | "";
            String replyTopic = servicePrefix + identifier + "/invoke_reply";
            
            DEBUG_PRINTF("[服务] 未知服务调用: %s\n", identifier.c_str());
            
            serviceReplyDoc["code"] = 404;
            serviceReplyDoc["msg"] = "Service not found";
            JsonObject data = serviceReplyDoc["data"].to<JsonObject>();
            data["result"] = false;
            data["message"] = "Unknown service identifier";
            
            char outBuf[256];
            serializeJson(serviceReplyDoc, outBuf, sizeof(outBuf));
            
            if (mqttClient.connected()) {
                mqttClient.publish(replyTopic.c_str(), outBuf, 0);
            }
        }
        wdt_safe_reset();
        return;
    }
}



bool initWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(true);
  DEBUG_PRINTF("[WiFi] 连接 SSID=%s\n", configuredSSID.c_str());
  WiFi.begin(configuredSSID.c_str(), configuredPASS.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
    safeDelay(200);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN();
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTF("[WiFi] 连接成功, IP: %s\n", WiFi.localIP().toString().c_str());
    espClient.setInsecure();
    mqttClient.setBufferSize(2048); // 增加到2048字节支持完整工业级数据上报
    mqttClient.setCallback(mqttCallback);
    return true;
  } else {
    DEBUG_PRINTLN("[WiFi] 连接失败");
    return false;
  }
}
void syncTimeNTP() {
  for (int i=0; i<2; i++) {
    DEBUG_PRINTF("[NTP] 尝试服务器: %s\n", Config::NTP_SERVERS[i]);
    
    wdt_safe_reset(); // NTP同步前喂狗
    configTime(8 * 3600, 0, Config::NTP_SERVERS[i]);
    
    unsigned long start = millis();
    time_t now;
    while ((now = time(nullptr)) < Config::MIN_VALID_TIME && (millis() - start < 10000)) { // 增加到10秒
      wdt_safe_reset(); // 定期喂狗
      delay(500); // 增加延时
      DEBUG_PRINT(".");
    }
    DEBUG_PRINTLN();
    
    if (now >= Config::MIN_VALID_TIME) {
      timeSynced   = true;
      lastSyncTime = now;
      setenv("TZ", "CST-8", 1); tzset();
      
      // 简化时间同步日志
      DEBUG_PRINTLN("[NTP] 时间同步成功");
      
      wdt_safe_reset(); // 成功后喂狗
      return;
    }
    DEBUG_PRINTLN("[NTP] 同步失败");
    wdt_safe_reset(); // 失败后也喂狗
  }
  DEBUG_PRINTLN("[NTP] 全部失败");
}
void syncNetworkTime() {
  time_t now = time(nullptr);
  
  // 如果时间明显不对（小于2020年），强制同步
  if (now < Config::MIN_VALID_TIME) {
    DEBUG_PRINTLN("[NTP] 时间异常，强制同步");
    if (WiFi.status() == WL_CONNECTED) syncTimeNTP();
    return;
  }
  
  // 定期同步（24小时）
  if (timeSynced && (now - lastSyncTime < syncInterval)) return;
  
  // 时间跳跃检测
  static time_t lastCheckTime = 0;
  if (lastCheckTime != 0) {
    long timeDiff = (long)(now - lastCheckTime);
    if (timeDiff < 0) timeDiff = -timeDiff; // 安全的绝对值计算
    if (timeDiff > 3600) { // 1小时跳跃
      DEBUG_PRINTF("[NTP] 检测到时间跳跃，上次: %lld, 现在: %lld\n", (long long)lastCheckTime, (long long)now);
      if (WiFi.status() == WL_CONNECTED) syncTimeNTP();
    }
  }
  lastCheckTime = now;
  
  if (WiFi.status() == WL_CONNECTED) syncTimeNTP();
}
void connectMQTT() {
    // 栈保护
    UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
    if (stackRemaining < 1500) {
        DEBUG_PRINTF("[MQTT] 栈空间不足，拒绝MQTT连接: %u bytes\n", stackRemaining);
        return;
    }

    // 检查时钟是否同步
    time_t nowTime = getCurrentTime();
    if (nowTime < Config::MIN_VALID_TIME) {
        DEBUG_PRINTLN("[MQTT] 系统时钟未同步，等待NTP...");
        return;
    }

    // 若已连接，仅判定Token是否过期
    if (mqttClient.connected()) {
        time_t currentTime = nowTime;
        if (g_lastTokenTime != 0 && currentTime > 0) {
            long timeDiff = (long)(currentTime - g_lastTokenTime);
            long absTimeDiff = timeDiff < 0 ? -timeDiff : timeDiff;
            if (timeDiff > 72000 || absTimeDiff > 86400) {
                DEBUG_PRINTF("[Token] Token即将过期或时间异常，断开重连刷新Token\n");
                DEBUG_PRINTF("[Token] 上次Token时间: %lld, 当前时间: %lld, 差值: %ld秒\n",
                             (long long)g_lastTokenTime, (long long)currentTime, timeDiff);
                mqttClient.disconnect();
                g_lastTokenTime = 0;
                return;
            }
        }
        if (g_lastTokenTime == 0) {
            g_lastTokenTime = currentTime;
            DEBUG_PRINTF("[Token] 记录Token生成时间: %lld\n", (long long)g_lastTokenTime);
        }
        return;
    }

    static unsigned long lastConnectAttempt = 0;
    static int failedAttempts = 0;
    unsigned long now = millis();

    uint32_t retryInterval = 8000 + (failedAttempts * 2000);
    if (retryInterval > 30000) retryInterval = 30000;
    if (now - lastConnectAttempt < retryInterval) return;
    lastConnectAttempt = now;

    int wifiRSSI = getWifiRSSI();
    if (wifiRSSI < -85) {
        DEBUG_PRINTF("[WiFi MQTT] 信号极差(%d dBm)，跳过连接尝试\n", wifiRSSI);
        failedAttempts++;
        return;
    }

    stackRemaining = uxTaskGetStackHighWaterMark(NULL);
    if (stackRemaining < 2048) {
        DEBUG_PRINTF("[MQTT] Token生成前栈空间不足: %u bytes\n", stackRemaining);
        failedAttempts++;
        return;
    }
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 15000) {
        DEBUG_PRINTF("[MQTT] 可用内存不足，跳过连接: %u bytes\n", freeHeap);
        failedAttempts++;
        return;
    }

    // 打印三元组主要参数便于检查
    DEBUG_PRINTF("[MQTT] connect参数: device=%s, product=%s\n", gDeviceName.c_str(), gProductID.c_str());
    if (gDeviceKey.length() > 8) {
        DEBUG_PRINTF("[MQTT] DeviceKey前4后4: %s...%s (len=%d)\n",
            gDeviceKey.substring(0,4).c_str(),
            gDeviceKey.substring(gDeviceKey.length()-4).c_str(),
            gDeviceKey.length());
    } else {
        DEBUG_PRINTF("[MQTT] DeviceKey: %s (len=%d)\n", gDeviceKey.c_str(), gDeviceKey.length());
    }

    // Token生成及打印
    String token = generateSecureToken();
    DEBUG_PRINTF("[MQTT] Token: %s\n", token.c_str());

    if (token.isEmpty()) {
        DEBUG_PRINTLN("[WiFi MQTT] Token生成失败");
        failedAttempts++;
        return;
    }
    if (token.length() < 50 || token.indexOf("version=") == -1 || token.indexOf("&sign=") == -1) {
        DEBUG_PRINTLN("[WiFi MQTT] Token格式异常");
        failedAttempts++;
        return;
    }
    g_lastTokenTime = nowTime;

    mqttClient.setServer(Config::MQTT_SERVER, Config::MQTT_PORT);
    mqttClient.setKeepAlive(Config::MQTT_KEEP_ALIVE);
    mqttClient.setSocketTimeout(6);

    DEBUG_PRINTLN("[WiFi MQTT] 正在连接...");

    bool ok = false;
    unsigned long tStart = millis();
    unsigned long maxTimeout = 6000;
    if (wifiRSSI < -70) maxTimeout = 8000;
    if (wifiRSSI < -80) maxTimeout = 10000;

    while (millis() - tStart < maxTimeout) {
        SystemUtils::safe_wdt_reset();
        stackRemaining = uxTaskGetStackHighWaterMark(NULL);
        if (stackRemaining < 1000) {
            DEBUG_PRINTF("[MQTT] 循环中栈空间不足: %u bytes\n", stackRemaining);
            break;
        }
        unsigned long callBegin = millis();
        DEBUG_PRINTLN("[MQTT] 尝试连接...");
        ok = mqttClient.connect(gDeviceName.c_str(), gProductID.c_str(), token.c_str());
        unsigned long callCost = millis() - callBegin;
        int err = mqttClient.state();
        DEBUG_PRINTF("[MQTT] 连接尝试耗时: %lums, 结果: %s, 错误码: %d\n", callCost, ok ? "成功" : "失败", err);

        // 错误码说明
        if (!ok) {
            switch (err) {
                case -4: DEBUG_PRINTLN("[MQTT] 连接被服务器拒绝，参数或Token不正确（认证错误）"); break;
                case -2: DEBUG_PRINTLN("[MQTT] 连接超时/网络异常，可能TLS或域名或时间未同步"); break;
                case -1: DEBUG_PRINTLN("[MQTT] 网络未连接"); break;
                case 2: DEBUG_PRINTLN("[MQTT] 服务器返回: Identifier rejected"); break;
                case 3: DEBUG_PRINTLN("[MQTT] 服务器返回: Server unavailable"); break;
                case 4: DEBUG_PRINTLN("[MQTT] 服务器返回: Bad username or password"); break;
                case 5: DEBUG_PRINTLN("[MQTT] 服务器返回: Not authorized"); break;
                default: break;
            }
        }

        if (ok) break;
        unsigned long singleCallTimeout = 4000;
        if (wifiRSSI < -70) singleCallTimeout = 5000;
        if (wifiRSSI < -80) singleCallTimeout = 6000;
        if (callCost > singleCallTimeout) {
            DEBUG_PRINTF("[WDT] mqttClient.connect() 单次超时(%lums)，终止本次尝试\n", callCost);
            break;
        }
        SystemUtils::safe_delay(1500);
    }

    if (!ok) {
        int state = mqttClient.state();
        DEBUG_PRINTF("[WiFi MQTT] 连接失败, RSSI=%d, 第%d次失败, state=%d\n", wifiRSSI, failedAttempts + 1, state);
        failedAttempts++;
        if (failedAttempts >= 10) {
            DEBUG_PRINTLN("[WiFi MQTT] 多次失败，准备重启");
            g_stats.mqttFailureCount++;
            saveIndustrialStats();
            if (wifiRSSI < -80) {
                DEBUG_PRINTLN("[WiFi MQTT] 信号极差，延迟重启以等待网络恢复");
                SystemUtils::safe_delay(10000);
            }
            failedAttempts = 0;
            ESP.restart();
        }
        return;
    }

    DEBUG_PRINTLN("[WiFi MQTT] 连接成功");
    failedAttempts = 0;

    // 订阅
    struct { const char* topic; const char* note; } subs[] = {
        { "$sys/%s/%s/thing/property/set",      "property/set" },
        { "$sys/%s/%s/thing/property/get",      "property/get" },
        { "$sys/%s/%s/thing/service/+/invoke",  "service" },
        { "$sys/%s/%s/thing/event/post/reply",  "event/reply" },
        { "$sys/%s/%s/thing/property/desired/get/reply", "desired/get/reply" },
        { "$sys/%s/%s/thing/property/desired/delete/reply", "desired/delete/reply" },
    };
    for (size_t i = 0; i < sizeof(subs)/sizeof(subs[0]); ++i) {
        char t[128];
        snprintf(t, sizeof(t), subs[i].topic, gProductID.c_str(), gDeviceName.c_str());
        bool subOk = mqttClient.subscribe(t);
        if (!subOk) DEBUG_PRINTF("[MQTT] 订阅%s失败: %s\n", subs[i].note, t);
    }

    deviceOnline = true;
    reportSensorData(true);
    requestDesiredProperty();
}





void requestDesiredProperty() {
    static uint32_t lastRequest = 0;
    if (millis() - lastRequest < 3000) return; // 3秒内只发一次，防止重连风暴
    lastRequest = millis();

    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "%llu", millis() % 10000000000000UL);

    // 需要哪些属性就填哪些，比如支持 onff、temperature等
    JsonDocument doc;
    doc["id"] = idbuf;
    doc["version"] = "1.0";
    JsonArray arr = doc["params"].to<JsonArray>();
    arr.add("engineStartPullLowTime");
    arr.add("engineStartWaitTime");
    arr.add("engineStopPullLowTime");
    arr.add("engineStopWaitTime");
    arr.add("rpmCalibration");
    arr.add("engineOnRpmThreshold");
    arr.add("wakePulseMs");
    arr.add("wakeToLongMs");
    arr.add("auto_update");  // OTA自动更新字段
    arr.add("onff");  // 开关状态字段
    // 修正热配置字段标识符，匹配OneNet平台物模型
    arr.add("rpm_alarm_threshold");          // 修正：平台使用下划线命名
    arr.add("maintenance_interval_hours");   // 修正：平台使用下划线命名
    arr.add("wifi_rssi_weak_threshold");     // 修正：平台使用下划线命名
    arr.add("memory_warning_threshold");     // 修正：平台使用下划线命名
    arr.add("ota_check_interval_minutes");   // 修正：平台使用下划线命名
    // 你有啥属性就全加进来，跟平台物模型一致

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/desired/get";
    mqttClient.publish(topic.c_str(), buf);
    DEBUG_PRINTF("[OneNET] 主动获取期望属性: %s\n", buf);
}


// ========== 新增：OTA相关事件和属性上报函数 ==========

// 上报OTA开始事件
void reportOTAStartEvent(int currentVersion, int newVersion) {
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"ota_start\":{"
                    "\"value\":{"
                        "\"current_version\":%d,"
                        "\"new_version\":%d"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, currentVersion, newVersion, (uint64_t)time(nullptr)*1000ULL
    );
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[OTA] 已上报OTA开始事件: %d -> %d\n", currentVersion, newVersion);
}

// 上报OTA升级失败事件 (匹配平台ota_fail事件)
void reportOTAResult(const char* result, int errorCode, String errorMsg) {
    // 成功的情况不需要单独上报，会通过ota_success事件上报
    if (String(result) == "success") {
        DEBUG_PRINTLN("[OTA] 成功结果将通过ota_success事件上报");
        return;
    }
    
    // 失败的情况使用平台的ota_fail事件格式
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[512];
    
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"ota_fail\":{"
                    "\"value\":{"
                        "\"error_code\":%d,"
                        "\"msg\":\"%s\""
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, errorCode, errorMsg.c_str(), (uint64_t)time(nullptr)*1000ULL
    );
    
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[OTA] 已上报OTA失败事件: error_code=%d, msg=%s\n", errorCode, errorMsg.c_str());
}

// 上报OTA状态
void reportOTAState() {
    // 无论MQTT是否连接，都记录本地状态
    DEBUG_PRINTF("[OTA] 本地状态记录: state=%s\n", g_ota_state ? "true" : "false");
    
    // 如果MQTT连接，则上报到云端
    if (mqttClient.connected()) {
        char idbuf[24];
        snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
        uint64_t tsNowMs = (uint64_t)getCurrentTime() * 1000ULL;
        char payload[256];
        snprintf(payload, sizeof(payload),
            "{"
                "\"id\":\"%s\","
                "\"version\":\"1.0\","
                "\"params\":{"
                    "\"ota_state\":{"
                        "\"value\":%s,"
                        "\"time\":%llu"
                    "}"
                "}"
            "}",
            idbuf, g_ota_state ? "true" : "false", tsNowMs
        );
        
        String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/property/post";
        mqttClient.publish(topic.c_str(), payload);
        DEBUG_PRINTF("[OTA] 已上报到云端: state=%s (格式:{\"value\":%s,\"time\":%llu})\n", 
                     g_ota_state ? "true" : "false", g_ota_state ? "true" : "false", tsNowMs);
    } else {
        DEBUG_PRINTLN("[OTA] MQTT未连接，状态将在连接后自动上报");
    }
}

void reportOTAUpdateSuccess(int newVersion) {
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"ota_success\":{"
                    "\"value\":{"
                        "\"new_version\":%d"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, newVersion, (uint64_t)time(nullptr)*1000ULL
    );
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTLN("[OTA] 已上报OTA成功事件");
}


// 按用户要求：移除MQTT断开逻辑，OTA期间保持MQTT连接并行运行











/* ------------------------------------------------------------------
 *  OTA系统预检查 - 智能环境评估
 * ------------------------------------------------------------------ */
bool otaPreflightCheck() {
    DEBUG_PRINTLN("[OTA] 执行系统预检查...");
    
    // 1. 网络质量检查
    int rssi = getWifiRSSI();
    if (rssi < -85) {
        DEBUG_PRINTF("[OTA] 网络信号过差(%d dBm)，延迟OTA\n", rssi);
        return false;
    }
    
    // 2. 内存检查
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) { // 需要至少50KB可用内存 (降低阈值)
        DEBUG_PRINTF("[OTA] 可用内存不足(%u bytes)，延迟OTA\n", freeHeap);
        return false;
    }
    
    // 3. 存储空间检查
    size_t sketchSize = ESP.getSketchSize();
    size_t freeSketchSpace = ESP.getFreeSketchSpace();
    if (freeSketchSpace < sketchSize * 1.2) { // 需要至少120%当前固件大小的空间
        DEBUG_PRINTF("[OTA] 存储空间不足，当前:%u 可用:%u\n", sketchSize, freeSketchSpace);
        
        // 上报存储空间不足事件
        reportStorageInsufficientEvent(sketchSize, freeSketchSpace);
        
        return false;
    }
    
    // 4. 系统稳定性检查 - 只在异常重启后才启用
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (millis() < 30000 && (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT || 
                             resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT)) {
        DEBUG_PRINTF("[OTA] 异常重启后稳定时间不足(重启原因:%d)，延迟OTA\n", resetReason);
        return false;
    }
    
    DEBUG_PRINTF("[OTA] 预检查通过 - 信号:%ddBm 内存:%uKB 存储:%uKB\n", 
                  rssi, freeHeap/1024, freeSketchSpace/1024);
    return true;
}

// 按原代码风格简化，移除getRetryDelay函数



/* ------------------------------------------------------------------
 *  安全 OTA 更新  ——  增强智能保障版本
 * ------------------------------------------------------------------ */
void safeOTAUpdate(const char* otaUrl, const char* md5, int newVersion)
{
    DEBUG_PRINTLN("========== [OTA] 开始OTA升级流程 ==========");
    otaPrefs.begin("ota", false);
    int curVer = otaPrefs.getInt("version", 1);
    if (newVersion <= curVer) {
        DEBUG_PRINTF("[OTA] 设备已是最新版本(%d), 不升级\n", curVer);
        otaPrefs.end();
        wdt_safe_reset();
        return;
    }
    
    DEBUG_PRINTF("[OTA] 发现新版本: %d -> %d\n", curVer, newVersion);
    
    // ========== 上报OTA开始事件并设置状态 ==========
    reportOTAStartEvent(curVer, newVersion);
    g_ota_state = true;
    reportOTAState();
    
    // 智能预检查
    if (!otaPreflightCheck()) {
        DEBUG_PRINTLN("[OTA] 预检查失败，本次升级取消");
        g_ota_state = false;
        reportOTAState();
        otaPrefs.end();
        wdt_safe_reset();
        return;
    }
    
    otaInProgress = true;  // 让LED进入黄灯状态
    wdt_safe_reset();

    DEBUG_PRINTLN("[OTA] 开始升级，临时断开MQTT避免资源冲突");
    
    // ========== 临时断开MQTT，避免网络资源竞争 ==========
    bool mqttWasConnected = mqttClient.connected();
    if (mqttWasConnected) {
        DEBUG_PRINTLN("[OTA] 暂停MQTT连接...");
        mqttClient.disconnect();
        delay(2000); // 等待MQTT连接完全关闭
    }

    DEBUG_PRINTF("[OTA] 准备下载 %s\n", otaUrl);
    
    // 系统状态检查
    DEBUG_PRINTF("[OTA] 当前可用内存: %lu bytes, WiFi信号: %d dBm\n", ESP.getFreeHeap(), WiFi.RSSI());
    
    // ========== 使用独立的SSL客户端实例，避免状态污染 ==========
    WiFiClientSecure* otaClientPtr = new WiFiClientSecure();
    if (!otaClientPtr) {
        DEBUG_PRINTLN("[OTA] 内存不足，无法创建SSL客户端");
        g_ota_state = false;
        reportOTAState();
        otaInProgress = false;
        otaPrefs.end();
        return;
    }
    
    otaClientPtr->setInsecure();  // 跳过证书验证，提高兼容性
    otaClientPtr->setTimeout(30000); // 增加到30秒超时，适应大文件下载
    DEBUG_PRINTLN("[OTA] SSL客户端配置完成");
    
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.onProgress(otaProgress); // 仅用于串口进度显示
    
    // 设置重定向（HTTPUpdate类的内置功能）
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (md5 && strlen(md5) == 32) {
        httpUpdate.setMD5sum(md5);
        DEBUG_PRINTF("[OTA] 设置MD5校验: %s\n", md5);
    }

    int failCount = 0;
    const int OTA_MAX_FAIL_RETRY = 3;
    t_httpUpdate_return ret = HTTP_UPDATE_FAILED;

    while (failCount < OTA_MAX_FAIL_RETRY) {
        wdt_safe_reset();
        DEBUG_PRINTF("[OTA] 第%d次尝试下载固件...\n", failCount + 1);
        
        // 每次重试前重新创建客户端，确保干净的连接状态
        if (failCount > 0) {
            DEBUG_PRINTLN("[OTA] 重新创建SSL客户端...");
            delete otaClientPtr; // 删除旧的客户端
            delay(3000);         // 等待更长时间，让网络完全释放
            
            otaClientPtr = new WiFiClientSecure();
            if (!otaClientPtr) {
                DEBUG_PRINTLN("[OTA] 重试失败：无法创建新的SSL客户端");
                break;
            }
            
            otaClientPtr->setInsecure();
            otaClientPtr->setTimeout(30000);
            
            // 网络连接测试
            if (otaClientPtr->connect("tenkon.sunrays.top", 443)) {
                DEBUG_PRINTLN("[OTA] 服务器连接测试成功");
                otaClientPtr->stop();
                delay(2000);
            } else {
                DEBUG_PRINTLN("[OTA] 服务器连接测试失败");
                delay(5000);
            }
        }
        
        ret = httpUpdate.update(*otaClientPtr, otaUrl);
        
        if (ret == HTTP_UPDATE_OK) {
            DEBUG_PRINTLN("[OTA] ✅ 升级成功！写入新版本并保存状态，准备重启");
            otaPrefs.putInt("version", newVersion);
            otaPrefs.end();
            
            // ========== 立即同步全局版本号变量 ==========
            setnewFirmwareVersion = newVersion;
            DEBUG_PRINTF("[OTA] 版本号已同步 - NVS: %d, 全局变量: %d\n", newVersion, setnewFirmwareVersion);
            
            // ========== 保存OTA升级结果到持久化存储 ==========
            Preferences otaResultPrefs;
            otaResultPrefs.begin("ota_result", false);
            otaResultPrefs.putBool("had_upgrade", true);
            otaResultPrefs.putInt("version", newVersion);
            otaResultPrefs.putString("status", "success");
            otaResultPrefs.end();
            DEBUG_PRINTF("[OTA] 已保存升级状态到NVS，版本：%d\n", newVersion);
            
            g_ota_state = false;
            reportOTAState();
            
            // ========== 尝试立即上报，但不依赖结果 ==========
            if (mqttClient.connected()) {
                DEBUG_PRINTLN("[OTA] MQTT连接正常，尝试立即上报成功事件");
                reportOTAResult("success", 0, "");
                delay(2000); // 给MQTT足够时间发送
            } else {
                DEBUG_PRINTLN("[OTA] MQTT未连接，成功事件将在重启后上报");
            }
            
            otaInProgress = false;
            delete otaClientPtr; // 清理客户端
            delay(1000);
            ESP.restart();
            return;
        } else if (ret == HTTP_UPDATE_NO_UPDATES) {
            DEBUG_PRINTLN("[OTA] 没有新固件，不用升级");
            delete otaClientPtr; // 清理客户端
            g_ota_state = false;
            reportOTAState();
            otaInProgress = false;
            otaPrefs.end();
            
            // 恢复MQTT连接
            if (mqttWasConnected) {
                DEBUG_PRINTLN("[OTA] 恢复MQTT连接...");
                delay(2000);
            }
            wdt_safe_reset();
            return;
        } else {
            int error = httpUpdate.getLastError();
            String errorStr = httpUpdate.getLastErrorString();
            
            DEBUG_PRINTF("[OTA] 下载失败 (err %d): %s\n", error, errorStr.c_str());
            
            // 只在最后一次失败时保存失败状态，避免重复上报
            if (failCount == OTA_MAX_FAIL_RETRY - 1) {
                // ========== 保存OTA升级失败状态到持久化存储 ==========
                Preferences otaResultPrefs;
                otaResultPrefs.begin("ota_result", false);
                otaResultPrefs.putBool("had_upgrade", true);
                otaResultPrefs.putInt("version", newVersion);
                otaResultPrefs.putString("status", "failed");
                otaResultPrefs.putInt("error_code", error);
                otaResultPrefs.putString("error_msg", errorStr);
                otaResultPrefs.end();
                DEBUG_PRINTF("[OTA] 已保存失败状态到NVS，错误码：%d\n", error);
                
                // ========== 尝试立即上报，但不依赖结果 ==========
                if (mqttClient.connected()) {
                    DEBUG_PRINTLN("[OTA] MQTT连接正常，尝试立即上报失败事件");
                    reportOTAResult("fail", error, errorStr);
                    delay(2000); // 给MQTT足够时间发送
                } else {
                    DEBUG_PRINTLN("[OTA] MQTT未连接，失败事件将在重启后上报");
                }
            }
            
            failCount++;
            if (failCount < OTA_MAX_FAIL_RETRY) {
                unsigned long retryDelay = 3000 + (failCount * 2000); // 递增延时：3s, 5s, 7s
                DEBUG_PRINTF("[OTA] 等待%lus后重试...\n", retryDelay / 1000);
                SystemUtils::safe_delay(retryDelay);
                wdt_safe_reset();
            }
        }
    }
    
    // ========== OTA全部失败后清理状态 ==========
    g_ota_state = false;
    reportOTAState();
    
    otaInProgress = false; // LED 状态恢复
    otaPrefs.end();
    DEBUG_PRINTLN("[OTA] 固件升级失败。");
    
    // ========== 清理资源并恢复MQTT连接 ==========
    delete otaClientPtr; // 清理SSL客户端
    
    if (mqttWasConnected) {
        DEBUG_PRINTLN("[OTA] 恢复MQTT连接...");
        delay(2000); // 给网络缓冲时间
        
        // 尝试重新连接MQTT以便上报失败事件
        int reconnectAttempts = 0;
        while (!mqttClient.connected() && reconnectAttempts < 3) {
            DEBUG_PRINTF("[OTA] 尝试重连MQTT第%d次...\n", reconnectAttempts + 1);
            connectMQTT();
            if (!mqttClient.connected()) {
                delay(3000); // 失败后等待3秒再重试
            }
            reconnectAttempts++;
        }
        
        if (mqttClient.connected()) {
            DEBUG_PRINTLN("[OTA] MQTT重连成功，上报失败事件将在主循环中执行");
        } else {
            DEBUG_PRINTLN("[OTA] MQTT重连失败，失败事件将延后上报");
        }
    }
    
    wdt_safe_reset();
}





/* ------------------------------------------------------------------
 *  简化的OTA下载进度回调 - 仅用于串口输出
 * ------------------------------------------------------------------ */
static void otaProgress(int cur, int total)
{
    static unsigned long lastPrint = 0;
    unsigned long now = millis();
    
    if (now - lastPrint > 2000) { // 每2秒输出一次进度
        DEBUG_PRINTF("[OTA] 下载进度: %d / %d  ( %d%% )\n",
                      cur, total, total ? cur * 100 / total : 0);
        lastPrint = now;
    }
    wdt_safe_reset();
}

/* ====================== 主函数：检查并升级 ====================== */
/* ====================== 主函数：检查并升级 ====================== */
void checkForUpdatesWiFi()
{
    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINTLN("[WiFi OTA] WiFi 未连接, 跳过");
        wdt_safe_reset();
        return;
    }
    
    // ========== 静态变量声明 ==========
    static int consecutiveFailures = 0;
    
    // ========== 新增：OTA触发逻辑检查 ==========
    bool shouldCheckOTA = false;
    
    // 情况1：强制OTA（服务调用start_update触发）
    if (g_forceOTAStart) {
        Serial.println("[OTA] 服务调用触发，忽略auto_update设置，强制执行OTA检查");
        shouldCheckOTA = true;
        g_forceOTAStart = false; // 清除标志
    }
    // 情况2：auto_update为true的定期检查
    else if (g_auto_update) {
        // 使用热配置的OTA检查间隔（防止过于频繁）
        unsigned long minInterval = gConfig.otaCheckIntervalMinutes * 60000UL; // 转换为毫秒
        if (consecutiveFailures > 0) {
            minInterval += consecutiveFailures * 1800000; // 每次失败增加30分钟间隔
            if (minInterval > 21600000) minInterval = 21600000; // 最大6小时
        }
        
        unsigned long timeSinceLastAttempt = g_lastOTACheck == 0 ? ULONG_MAX : millis() - g_lastOTACheck;
        DEBUG_PRINTF("[OTA] 检查间隔: %lu分钟, 距离上次检查: %lu分钟, 连续失败: %d次\n", 
                     minInterval/60000, timeSinceLastAttempt/60000, consecutiveFailures);
        
        if (g_lastOTACheck == 0 || timeSinceLastAttempt >= minInterval) {
            DEBUG_PRINTF("[OTA] auto_update=true，满足间隔条件，执行定期OTA检查\n");
            shouldCheckOTA = true;
            g_lastOTACheck = millis();
        } else {
            DEBUG_PRINTF("[OTA] auto_update=true，但未到检查时间，还需等待%lu分钟\n", 
                         (minInterval - timeSinceLastAttempt)/60000);
        }
    }
    // 情况3：auto_update为false，跳过检查
    else {
        DEBUG_PRINTLN("[OTA] auto_update=false，跳过本次OTA检查");
        wdt_safe_reset();
        return;
    }
    
    if (!shouldCheckOTA) {
        return; // 跳过本次检查
    }
    
    // 系统负载检查 - 避免在系统繁忙时进行OTA (调整阈值)
    size_t currentFreeHeap = ESP.getFreeHeap();
    UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
    if (currentFreeHeap < 45000 || stackRemaining < 1500) {
        DEBUG_PRINTF("[OTA] 系统负载过高，延迟OTA检查 (内存:%u 栈:%u)\n", 
                     currentFreeHeap, stackRemaining);
        return;
    }
    
    // 网络质量检查 - 避免在网络不稳定时进行OTA
    int currentRSSI = getWifiRSSI();
    if (currentRSSI < -80) {
        DEBUG_PRINTF("[OTA] 网络信号过弱(%d dBm)，延迟OTA检查\n", currentRSSI);
        return;
    }
    
    // WiFi连接检查 - OTA不依赖MQTT连接
    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINTLN("[OTA] WiFi未连接，延迟OTA检查");
        return;
    }
    
    DEBUG_PRINTLN("[OTA] 开始版本检查");
    
    // ========== 版本检查重试机制（3次重试） ==========
    String httpUrl = String(Config::UPDATE_INFO_URL);
    DEBUG_PRINTF("[OTA] 检查更新: %s\n", httpUrl.c_str());
    
    int httpCode = 0;
    String payload = "";
    bool versionCheckSuccess = false;
    
    for (int retry = 1; retry <= 3 && !versionCheckSuccess; retry++) {
        DEBUG_PRINTF("[OTA] 版本检查第%d次尝试...\n", retry);
        
        WiFiClientSecure httpClient;
        httpClient.setInsecure();
        httpClient.setTimeout(15000);
        
        HTTPClient http;
        
        if (!http.begin(httpClient, httpUrl)) {
            DEBUG_PRINTF("[OTA] 第%d次HTTP连接初始化失败\n", retry);
            if (retry < 3) {
                delay(2000); // 重试前等待2秒
                continue;
            } else {
                DEBUG_PRINTLN("[OTA] 版本检查全部失败，跳过本次OTA");
                return;
            }
        }
        
        http.setTimeout(15000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.addHeader("Connection", "close");
        http.addHeader("User-Agent", "ESP32-OTA-Checker/1.0");
        
        wdt_safe_reset();
        httpCode = http.GET();
        wdt_safe_reset();
        
        if (httpCode == HTTP_CODE_OK) {
            payload = http.getString();
            http.end();
            versionCheckSuccess = true;
            DEBUG_PRINTF("[OTA] 第%d次版本检查成功\n", retry);
        } else {
            DEBUG_PRINTF("[OTA] 第%d次版本检查失败: HTTP %d (%s)\n", 
                         retry, httpCode, http.errorToString(httpCode).c_str());
            http.end();
            if (retry < 3) {
                delay(3000); // 重试前等待3秒
            }
        }
    }
    
    if (!versionCheckSuccess) {
        DEBUG_PRINTLN("[OTA] 版本检查3次重试全部失败，跳过本次OTA");
        consecutiveFailures++;
        wdt_safe_reset();
        return;
    }
    
    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        DEBUG_PRINTLN("[OTA] JSON解析失败");
        wdt_safe_reset();
        return;
    }
    wdt_safe_reset();

    JsonObject wifiObj = doc["wifi"];
    int    newVersion = atoi(wifiObj["version"] | "0");
    String firmwareUrl = wifiObj["url"] | "";
    const char* md5Str = wifiObj["md5"] | "";

    DEBUG_PRINTF("[OTA] 服务器版本: %d, 当前版本: %d\n", newVersion, setnewFirmwareVersion);
    
    if (firmwareUrl.isEmpty()) {
        DEBUG_PRINTLN("[OTA] 固件URL为空");
        wdt_safe_reset();
        return;
    }

    DEBUG_PRINTF("[OTA] 发现新版本 %d，开始下载\n", newVersion);
    safeOTAUpdate(firmwareUrl.c_str(), md5Str, newVersion);
    
    consecutiveFailures = 0;
    wdt_safe_reset();


}

void checkForUpdatesTask(void *parameter)
{
    esp_task_wdt_add(NULL);
    
    static bool isFirstRun = true;

    while (1) {
        // 在执行OTA检查前喂狗
        wdt_safe_reset();
        unsigned long checkStart = millis();
        
        // ========== 首次运行等待期望属性同步 ==========
        if (isFirstRun) {
            if (!g_desiredPropertySynced) {
                // 等待期望属性同步，最多等待5分钟
                static unsigned long firstRunStart = millis();
                if (millis() - firstRunStart < 300000) { // 5分钟超时
                    DEBUG_PRINTLN("[OTA] 首次运行，等待期望属性同步...");
                    // 短时间休眠，然后重新检查
                    for (int i = 0; i < 100; ++i) { // 10秒
                        wdt_safe_reset();
                        vTaskDelay(pdMS_TO_TICKS(100));
                    }
                    continue;
                } else {
                    DEBUG_PRINTLN("[OTA] 等待期望属性同步超时，继续使用默认配置");
                    g_desiredPropertySynced = true; // 标记为已同步，避免无限等待
                }
            }
            isFirstRun = false;
            DEBUG_PRINTF("[OTA] 期望属性同步完成，auto_update=%s\n", g_auto_update ? "true" : "false");
        }
        
        // ========== 检查OTA触发条件 ==========
        if (g_forceOTAStart || g_auto_update) {
            // 强制输出调试信息
            Serial.printf("[OTA] 触发条件满足: g_forceOTAStart=%s, g_auto_update=%s\n", 
                         g_forceOTAStart ? "true" : "false", g_auto_update ? "true" : "false");
            Serial.println("[OTA] 执行定期检查");
            // OTA检查前再次喂狗
            wdt_safe_reset();
            checkForUpdatesWiFi();
            // OTA检查后再次喂狗
            wdt_safe_reset();
        } else {
            // auto_update=false时，只在收到start_update服务调用时才检查
            Serial.printf("[OTA] 跳过检查: g_forceOTAStart=%s, g_auto_update=%s\n", 
                         g_forceOTAStart ? "true" : "false", g_auto_update ? "true" : "false");
        }
        
        unsigned long checkDuration = millis() - checkStart;
        if (checkDuration > 1000) { // 只有实际执行了检查才记录
    
        }
        
        // 检查完成后立即喂狗
        wdt_safe_reset();

        // 1小时内每100ms喂狗一次，期间可以被其他任务抢占
        // 但如果收到强制OTA标志，立即跳出等待循环
        for (int i = 0; i < 36000; ++i) {
            wdt_safe_reset();         // 使用安全的看门狗重置函数
            vTaskDelay(pdMS_TO_TICKS(100)); // FreeRTOS标准延时
            
            // 检查是否有强制OTA请求，如果有则立即跳出等待循环
            if (g_forceOTAStart) {
                Serial.println("[OTA] 检测到强制OTA请求，立即跳出等待循环");
                break;
            }
        }
    }
}

void updateFirmwareVersion() {
  otaPrefs.begin("ota", false);
  int currentVersion = otaPrefs.getInt("version", Config::FIRMWARE_VERSION);  // 使用配置中的版本号作为默认值
  
  // 同步全局变量为当前实际版本（NVS中的版本）
  setnewFirmwareVersion = currentVersion;
  
  if (currentVersion < Config::FIRMWARE_VERSION) {
    otaPrefs.putInt("version", Config::FIRMWARE_VERSION);
    setnewFirmwareVersion = Config::FIRMWARE_VERSION;  // 立即同步最新版本
    DEBUG_PRINTF("Firmware version updated to %d\n", Config::FIRMWARE_VERSION);
  } else {
    DEBUG_PRINTF("Firmware version is up to date: %d\n", currentVersion);
  }
  otaPrefs.end();
  
  DEBUG_PRINTF("[版本同步] NVS版本: %d, 全局变量: %d, 代码版本: %d\n", 
               currentVersion, setnewFirmwareVersion, Config::FIRMWARE_VERSION);
}





WebServer configServer(80);
void handleRoot() {
  String page = "<html><head><meta charset='UTF-8'></head><body><h1>WiFi 配网</h1>"
                "<form action='/save' method='POST'>"
                "SSID: <input type='text' name='ssid'><br>"
                "Password: <input type='password' name='pass'><br>"
                "<input type='submit' value='Save'>"
                "</form></body></html>";
  configServer.send(200, "text/html; charset=UTF-8", page);
}
void handleSave() {
  String newSSID = configServer.arg("ssid");
  String newPass = configServer.arg("pass");
  if (newSSID != "") {
    wifiPrefs.begin("wifi", false);
    wifiPrefs.putString("ssid", newSSID);
    wifiPrefs.putString("pass", newPass);
    wifiPrefs.end();
    configServer.send(200, "text/html; charset=UTF-8",
                      "<h1>配置成功,设备重启...</h1>");
    safeDelay(1000);
    ESP.restart();
  } else {
    configServer.send(200, "text/html; charset=UTF-8",
                      "<h1>Error: SSID不能为空！</h1>");
  }
}
void startConfigPortal(unsigned long portalTimeout, bool secureTriset = false) {
    String apName = "ConfigPortal_" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX)
                                   + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), "config1234");
    IPAddress IP = WiFi.softAPIP();
    DEBUG_PRINT("[配网] AP IP地址: ");
    DEBUG_PRINTLN(IP);

    // 普通WiFi配置页面
    auto handleRoot = []() {
      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
      html += "<title>WiFi配网 - 工业控制器</title><style>*{margin:0;padding:0;box-sizing:border-box;}";
      html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
      html += "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;display:flex;";
      html += "align-items:center;justify-content:center;padding:20px;}.container{background:rgba(255,255,255,0.95);";
      html += "backdrop-filter:blur(10px);border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,0.1);padding:40px;";
      html += "width:100%;max-width:420px;border:1px solid rgba(255,255,255,0.2);}.logo{text-align:center;margin-bottom:30px;}";
      html += ".logo .icon{width:60px;height:60px;background:linear-gradient(45deg,#4CAF50,#2196F3);border-radius:50%;";
      html += "margin:0 auto 15px;display:flex;align-items:center;justify-content:center;color:white;font-size:24px;font-weight:bold;}";
      html += "h1{color:#2c3e50;text-align:center;margin-bottom:8px;font-size:24px;font-weight:600;}";
      html += ".subtitle{text-align:center;color:#7f8c8d;margin-bottom:30px;font-size:14px;}.form-group{margin-bottom:25px;}";
      html += "label{display:block;margin-bottom:8px;color:#34495e;font-weight:500;font-size:14px;}";
      html += "input[type=text],input[type=password]{width:100%;padding:15px 20px;border:2px solid #e1e8ed;";
      html += "border-radius:12px;font-size:16px;transition:all 0.3s ease;background:#f8f9fa;}";
      html += "input[type=text]:focus,input[type=password]:focus{outline:none;border-color:#667eea;background:white;";
      html += "box-shadow:0 0 0 3px rgba(102,126,234,0.1);}.submit-btn{width:100%;padding:16px;";
      html += "background:linear-gradient(45deg,#667eea,#764ba2);color:white;border:none;border-radius:12px;";
      html += "font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s ease;";
      html += "box-shadow:0 4px 15px rgba(102,126,234,0.3);}.submit-btn:hover{transform:translateY(-2px);";
      html += "box-shadow:0 6px 20px rgba(102,126,234,0.4);}.submit-btn:active{transform:translateY(0);}";
      html += ".device-info{background:#f8f9fa;border-radius:10px;padding:15px;margin-bottom:25px;border-left:4px solid #667eea;}";
      html += ".device-info h3{color:#2c3e50;font-size:14px;margin-bottom:8px;}.device-info p{color:#7f8c8d;font-size:12px;margin:2px 0;}";
      html += ".help-text{text-align:center;color:#95a5a6;font-size:12px;margin-top:20px;line-height:1.4;}";
      html += "@media (max-width:480px){.container{padding:30px 20px;margin:10px;}h1{font-size:20px;}}";
      html += "</style></head><body><div class=\"container\"><div class=\"logo\"><div class=\"icon\">WiFi</div></div>";
      html += "<h1>WiFi网络配置</h1><p class=\"subtitle\">工业级发动机控制系统</p><div class=\"device-info\">";
      html += "<h3>设备信息</h3><p>设备ID: " + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX) + String((uint32_t)ESP.getEfuseMac(), HEX) + "</p>";
      html += "<p>固件版本: v" + String(Config::FIRMWARE_VERSION) + "</p><p>内存使用: " + String(ESP.getFreeHeap()/1024) + "KB 可用</p></div>";
      html += "<form action='/save' method='POST'><div class=\"form-group\"><label for=\"ssid\">WiFi网络名称 (SSID)</label>";
      html += "<input type='text' id=\"ssid\" name='ssid' placeholder=\"请输入WiFi名称\" required></div>";
      html += "<div class=\"form-group\"><label for=\"pass\">WiFi密码</label>";
      html += "<input type='password' id=\"pass\" name='pass' placeholder=\"请输入WiFi密码\"></div>";
      html += "<button type='submit' class=\"submit-btn\">连接WiFi网络</button></form>";
      html += "<p class=\"help-text\">配置成功后设备将自动重启并连接到指定网络<br>如遇问题请检查网络名称和密码是否正确</p></div>";
      html += "<script>document.addEventListener('DOMContentLoaded',function(){";
      html += "const form=document.querySelector('form');const submitBtn=document.querySelector('.submit-btn');";
      html += "const ssidInput=document.getElementById('ssid');form.addEventListener('submit',function(e){";
      html += "if(ssidInput.value.trim()===''){e.preventDefault();alert('请输入WiFi网络名称！');ssidInput.focus();return;}";
      html += "submitBtn.innerHTML='正在配置...';submitBtn.disabled=true;});ssidInput.focus();});</script></body></html>";
      configServer.send(200, "text/html; charset=UTF-8", html);
    };

    auto handleSave = []() {
      String newSSID = configServer.arg("ssid");
      String newPass = configServer.arg("pass");
      if (newSSID != "") {
        wifiPrefs.begin("wifi", false);
        wifiPrefs.putString("ssid", newSSID);
        wifiPrefs.putString("pass", newPass);
        wifiPrefs.end();
        
        String successHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        successHtml += "<title>配置成功</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        successHtml += "background:linear-gradient(135deg,#4CAF50 0%,#45a049 100%);min-height:100vh;display:flex;align-items:center;";
        successHtml += "justify-content:center;margin:0;padding:20px;}.container{background:rgba(255,255,255,0.95);border-radius:20px;";
        successHtml += "padding:40px;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,0.1);max-width:400px;}";
        successHtml += ".success-icon{width:80px;height:80px;background:#4CAF50;border-radius:50%;margin:0 auto 20px;";
        successHtml += "display:flex;align-items:center;justify-content:center;color:white;font-size:40px;}";
        successHtml += "h1{color:#2c3e50;margin-bottom:15px;font-size:24px;}p{color:#7f8c8d;margin-bottom:30px;line-height:1.5;}";
        successHtml += ".spinner{border:4px solid #f3f3f3;border-top:4px solid #4CAF50;border-radius:50%;width:40px;height:40px;";
        successHtml += "animation:spin 1s linear infinite;margin:20px auto;}@keyframes spin{0%{transform:rotate(0deg);}";
        successHtml += "100%{transform:rotate(360deg);}}</style></head><body><div class=\"container\">";
        successHtml += "<div class=\"success-icon\">✓</div><h1>配置成功！</h1>";
        successHtml += "<p>WiFi网络配置已保存，设备正在重启...</p><div class=\"spinner\"></div>";
        successHtml += "<p style=\"font-size:14px;color:#95a5a6;\">请稍候，设备将自动连接网络</p></div>";
        // 添加JavaScript自动检测重启
        successHtml += "<script>";
        successHtml += "var countdown = 5;";
        successHtml += "function updateCountdown() {";
        successHtml += "  if(countdown > 0) {";
        successHtml += "    document.querySelector('h1').innerHTML = 'WiFi配置成功！设备将在 ' + countdown + ' 秒后重启';";
        successHtml += "    countdown--;";
        successHtml += "    setTimeout(updateCountdown, 1000);";
        successHtml += "  } else {";
        successHtml += "    document.querySelector('h1').innerHTML = '设备正在重启中...';";
        successHtml += "    document.querySelector('p').innerHTML = '如果页面长时间无响应，请手动刷新浏览器或重新连接设备热点';";
        successHtml += "  }";
        successHtml += "}";
        successHtml += "setTimeout(updateCountdown, 1000);";
        successHtml += "</script></body></html>";
        
        configServer.send(200, "text/html; charset=UTF-8", successHtml);
        
        // 确保网页内容完全发送给浏览器
        delay(2000);
        
        // 停止配网服务器，释放资源
        configServer.stop();
        
        DEBUG_PRINTLN("[WiFi配网] 配置保存成功，设备即将重启...");
        Serial.flush();
        
        // 再次延时确保所有操作完成
        delay(1000);
        
        // 执行重启
        ESP.restart();
      } else {
        String errorHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        errorHtml += "<title>配置错误</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        errorHtml += "background:linear-gradient(135deg,#ff6b6b 0%,#ffa726 100%);min-height:100vh;display:flex;align-items:center;";
        errorHtml += "justify-content:center;margin:0;padding:20px;}.container{background:rgba(255,255,255,0.95);border-radius:20px;";
        errorHtml += "padding:40px;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,0.1);max-width:400px;}";
        errorHtml += ".error-icon{width:80px;height:80px;background:#ff6b6b;border-radius:50%;margin:0 auto 20px;";
        errorHtml += "display:flex;align-items:center;justify-content:center;color:white;font-size:40px;}";
        errorHtml += "h1{color:#e74c3c;margin-bottom:15px;font-size:24px;}p{color:#7f8c8d;margin-bottom:30px;line-height:1.5;}";
        errorHtml += ".back-btn{background:#ff6b6b;color:white;border:none;padding:12px 30px;border-radius:25px;";
        errorHtml += "font-size:16px;cursor:pointer;transition:all 0.3s;}.back-btn:hover{background:#e55555;transform:translateY(-2px);}";
        errorHtml += "</style></head><body><div class=\"container\"><div class=\"error-icon\">!</div><h1>配置错误</h1>";
        errorHtml += "<p>WiFi网络名称(SSID)不能为空，请重新输入。</p>";
        errorHtml += "<button class=\"back-btn\" onclick=\"history.back()\">返回重试</button></div></body></html>";
        
        configServer.send(200, "text/html; charset=UTF-8", errorHtml);
      }
    };

    // 高级三元组安全配置页面
    auto handleSecureTrisetRoot = []() {
      String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
      html += "<title>安全配置 - 工业控制器</title><style>*{margin:0;padding:0;box-sizing:border-box;}";
      html += "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
      html += "background:linear-gradient(135deg,#ff6b6b 0%,#ffa726 100%);min-height:100vh;display:flex;";
      html += "align-items:center;justify-content:center;padding:20px;}.container{background:rgba(255,255,255,0.95);";
      html += "backdrop-filter:blur(10px);border-radius:20px;box-shadow:0 20px 40px rgba(0,0,0,0.15);padding:40px;";
      html += "width:100%;max-width:480px;border:1px solid rgba(255,255,255,0.2);}.security-header{text-align:center;margin-bottom:30px;}";
      html += ".security-icon{width:70px;height:70px;background:linear-gradient(45deg,#ff6b6b,#ffa726);border-radius:50%;";
      html += "margin:0 auto 15px;display:flex;align-items:center;justify-content:center;color:white;font-size:28px;font-weight:bold;}";
      html += "h1{color:#2c3e50;text-align:center;margin-bottom:8px;font-size:24px;font-weight:600;}";
      html += ".security-warning{background:linear-gradient(45deg,#ff9a9e,#fecfef);border-radius:12px;padding:20px;";
      html += "margin-bottom:25px;border-left:4px solid #ff6b6b;text-align:center;}.security-warning h3{color:#c0392b;font-size:16px;margin-bottom:8px;}";
      html += ".security-warning p{color:#e74c3c;font-size:13px;line-height:1.4;}.form-group{margin-bottom:25px;}";
      html += "label{display:block;margin-bottom:8px;color:#2c3e50;font-weight:600;font-size:14px;}.required{color:#e74c3c;}";
      html += "input[type=text],input[type=password]{width:100%;padding:16px 20px;border:2px solid #e1e8ed;";
      html += "border-radius:12px;font-size:16px;transition:all 0.3s ease;background:#f8f9fa;font-family:'Courier New',monospace;}";
      html += "input[type=password]{font-family:inherit;}input[type=text]:focus,input[type=password]:focus{outline:none;";
      html += "border-color:#ff6b6b;background:white;box-shadow:0 0 0 3px rgba(255,107,107,0.1);}.input-hint{font-size:12px;";
      html += "color:#95a5a6;margin-top:5px;}.submit-btn{width:100%;padding:18px;background:linear-gradient(45deg,#ff6b6b,#ffa726);";
      html += "color:white;border:none;border-radius:12px;font-size:16px;font-weight:600;cursor:pointer;transition:all 0.3s ease;";
      html += "box-shadow:0 4px 15px rgba(255,107,107,0.3);}.submit-btn:hover{transform:translateY(-2px);";
      html += "box-shadow:0 6px 20px rgba(255,107,107,0.4);}.submit-btn:active{transform:translateY(0);}.submit-btn:disabled{opacity:0.6;";
      html += "cursor:not-allowed;transform:none;}.device-info{background:#f8f9fa;border-radius:10px;padding:15px;";
      html += "margin-bottom:25px;border-left:4px solid #ff6b6b;}.device-info h3{color:#2c3e50;font-size:14px;margin-bottom:8px;}";
      html += ".device-info p{color:#7f8c8d;font-size:12px;margin:2px 0;font-family:'Courier New',monospace;}";
      html += ".footer-text{text-align:center;color:#95a5a6;font-size:11px;margin-top:20px;line-height:1.4;}";
      html += "@media (max-width:520px){.container{padding:30px 20px;margin:10px;}h1{font-size:20px;}";
      html += ".security-icon{width:60px;height:60px;font-size:24px;}}</style></head><body><div class=\"container\">";
      html += "<div class=\"security-header\"><div class=\"security-icon\">🔒</div></div><h1>安全设备配置</h1>";
      html += "<div class=\"security-warning\"><h3>⚠️ 安全模式</h3><p>此为高级安全配置页面，仅限授权人员操作<br>错误配置可能导致设备无法正常工作</p></div>";
      html += "<div class=\"device-info\"><h3>设备状态</h3><p>设备MAC: " + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX) + String((uint32_t)ESP.getEfuseMac(), HEX) + "</p>";
      html += "<p>固件版本: v" + String(Config::FIRMWARE_VERSION) + "</p><p>运行时间: " + String(millis()/1000) + " 秒</p></div>";
      html += "<form action='/triset' method='POST' id=\"secureForm\"><div class=\"form-group\">";
      html += "<label for=\"secret\">安全密码 <span class=\"required\">*</span></label>";
      html += "<input type='password' id=\"secret\" name='secret' placeholder=\"请输入管理员密码\" required>";
      html += "<div class=\"input-hint\">需要输入正确的管理员密码才能继续</div></div>";
      html += "<div class=\"form-group\"><label for=\"product_id\">产品ID (Product ID) <span class=\"required\">*</span></label>";
      html += "<input type='text' id=\"product_id\" name='product_id' placeholder=\"例如: 123456\" required>";
      html += "<div class=\"input-hint\">IoT平台分配的产品标识符，至少4位</div></div>";
      html += "<div class=\"form-group\"><label for=\"device_name\">设备名称 (Device Name) <span class=\"required\">*</span></label>";
      html += "<input type='text' id=\"device_name\" name='device_name' placeholder=\"例如: device001\" required>";
      html += "<div class=\"input-hint\">设备在IoT平台中的唯一名称，至少2位</div></div>";
      html += "<div class=\"form-group\"><label for=\"device_key\">设备密钥 (Device Key) <span class=\"required\">*</span></label>";
      html += "<input type='text' id=\"device_key\" name='device_key' placeholder=\"请输入设备密钥\" required>";
      html += "<div class=\"input-hint\">IoT平台生成的设备认证密钥，至少16位</div></div>";
      html += "<button type='submit' class=\"submit-btn\" id=\"submitBtn\">保存安全配置</button></form>";
      html += "<p class=\"footer-text\">配置完成后设备将重启并应用新的安全设置<br>请确保所有信息准确无误</p></div>";
      html += "<script>document.addEventListener('DOMContentLoaded',function(){";
      html += "const form=document.getElementById('secureForm');const submitBtn=document.getElementById('submitBtn');";
      html += "const inputs=form.querySelectorAll('input[required]');function validateForm(){let isValid=true;";
      html += "inputs.forEach(input=>{if(input.value.trim()===''){isValid=false;}});";
      html += "const productId=document.getElementById('product_id').value;const deviceName=document.getElementById('device_name').value;";
      html += "const deviceKey=document.getElementById('device_key').value;";
      html += "if(productId.length<4||deviceName.length<2||deviceKey.length<16){isValid=false;}submitBtn.disabled=!isValid;return isValid;}";
      html += "inputs.forEach(input=>{input.addEventListener('input',validateForm);});";
      html += "form.addEventListener('submit',function(e){if(!validateForm()){e.preventDefault();";
      html += "alert('请检查所有必填项是否正确填写！');return;}";
      html += "if(!confirm('确定要保存这些安全配置吗？设备将会重启。')){e.preventDefault();return;}";
      html += "submitBtn.innerHTML='正在保存配置...';submitBtn.disabled=true;});validateForm();";
      html += "document.getElementById('secret').focus();});</script></body></html>";
      configServer.send(200, "text/html; charset=UTF-8", html);
    };

    auto handleTrisetSave = []() {
      String secret = configServer.arg("secret");
      String pid    = configServer.arg("product_id");
      String dname  = configServer.arg("device_name");
      String dkey   = configServer.arg("device_key");

      if (secret != Config::TRIPLET_SECRET) {
        String errorHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        errorHtml += "<title>密码错误</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        errorHtml += "background:linear-gradient(135deg,#ff6b6b 0%,#ffa726 100%);min-height:100vh;display:flex;align-items:center;";
        errorHtml += "justify-content:center;margin:0;padding:20px;}.container{background:rgba(255,255,255,0.95);border-radius:20px;";
        errorHtml += "padding:40px;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,0.1);max-width:400px;}";
        errorHtml += ".error-icon{width:80px;height:80px;background:#ff6b6b;border-radius:50%;margin:0 auto 20px;";
        errorHtml += "display:flex;align-items:center;justify-content:center;color:white;font-size:40px;}";
        errorHtml += "h1{color:#e74c3c;margin-bottom:15px;font-size:24px;}p{color:#7f8c8d;margin-bottom:30px;line-height:1.5;}";
        errorHtml += ".back-btn{background:#ff6b6b;color:white;border:none;padding:12px 30px;border-radius:25px;";
        errorHtml += "font-size:16px;cursor:pointer;transition:all 0.3s;}.back-btn:hover{background:#e55555;transform:translateY(-2px);}";
        errorHtml += "</style></head><body><div class=\"container\"><div class=\"error-icon\">🔒</div><h1>密码错误</h1>";
        errorHtml += "<p>管理员密码不正确，请重新输入。<br>如果忘记密码请联系系统管理员。</p>";
        errorHtml += "<button class=\"back-btn\" onclick=\"history.back()\">返回重试</button></div></body></html>";
        configServer.send(200, "text/html; charset=UTF-8", errorHtml);
        return;
      }
      if (pid.length() < 4 || dname.length() < 2 || dkey.length() < 16) {
        String errorHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
        errorHtml += "<title>三元组配置错误</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
        errorHtml += "background:linear-gradient(135deg,#ff6b6b 0%,#ffa726 100%);min-height:100vh;display:flex;align-items:center;";
        errorHtml += "justify-content:center;margin:0;padding:20px;}.container{background:rgba(255,255,255,0.95);border-radius:20px;";
        errorHtml += "padding:40px;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,0.1);max-width:450px;}";
        errorHtml += ".error-icon{width:80px;height:80px;background:#ff6b6b;border-radius:50%;margin:0 auto 20px;";
        errorHtml += "display:flex;align-items:center;justify-content:center;color:white;font-size:40px;}";
        errorHtml += "h1{color:#e74c3c;margin-bottom:15px;font-size:24px;}.error-details{background:#fff5f5;";
        errorHtml += "border-left:4px solid #ff6b6b;padding:15px;margin:20px 0;text-align:left;}";
        errorHtml += ".error-details p{margin:5px 0;color:#333;font-size:14px;}p{color:#7f8c8d;margin-bottom:30px;line-height:1.5;}";
        errorHtml += ".back-btn{background:#ff6b6b;color:white;border:none;padding:12px 30px;border-radius:25px;";
        errorHtml += "font-size:16px;cursor:pointer;transition:all 0.3s;}.back-btn:hover{background:#e55555;transform:translateY(-2px);}";
        errorHtml += "</style></head><body><div class=\"container\"><div class=\"error-icon\">⚠</div><h1>三元组配置错误</h1>";
        errorHtml += "<p>设备三元组信息不符合要求，请检查以下项目：</p><div class=\"error-details\">";
        errorHtml += "<p>• 产品ID长度必须至少4位</p><p>• 设备名称长度必须至少2位</p><p>• 设备密钥长度必须至少16位</p></div>";
        errorHtml += "<p style=\"font-size:14px;\">请返回并重新填写正确的设备三元组信息。</p>";
        errorHtml += "<button class=\"back-btn\" onclick=\"history.back()\">返回重试</button></div></body></html>";
        configServer.send(200, "text/html; charset=UTF-8", errorHtml);
        return;
      }
      Preferences devPrefs;
      devPrefs.begin("devcfg", false);
      devPrefs.putString("product_id", pid);
      devPrefs.putString("device_name", dname);
      devPrefs.putString("device_key", dkey);
      devPrefs.end();

      String successHtml = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
      successHtml += "<title>安全配置成功</title><style>body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;";
      successHtml += "background:linear-gradient(135deg,#4CAF50 0%,#45a049 100%);min-height:100vh;display:flex;align-items:center;";
      successHtml += "justify-content:center;margin:0;padding:20px;}.container{background:rgba(255,255,255,0.95);border-radius:20px;";
      successHtml += "padding:40px;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,0.1);max-width:450px;}";
      successHtml += ".success-icon{width:80px;height:80px;background:#4CAF50;border-radius:50%;margin:0 auto 20px;";
      successHtml += "display:flex;align-items:center;justify-content:center;color:white;font-size:40px;}";
      successHtml += "h1{color:#2c3e50;margin-bottom:15px;font-size:24px;}.success-details{background:#f0f9ff;";
      successHtml += "border-left:4px solid #4CAF50;padding:20px;margin:20px 0;text-align:left;}";
      successHtml += ".success-details h3{color:#2c3e50;margin-bottom:10px;font-size:16px;}";
      successHtml += ".success-details p{margin:5px 0;color:#333;font-size:14px;font-family:'Courier New',monospace;}";
      successHtml += "p{color:#7f8c8d;margin-bottom:30px;line-height:1.5;}.spinner{border:4px solid #f3f3f3;";
      successHtml += "border-top:4px solid #4CAF50;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;}";
      successHtml += "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}.security-note{background:#fff3cd;";
      successHtml += "border:1px solid #ffeaa7;border-radius:10px;padding:15px;margin-top:20px;color:#856404;font-size:13px;line-height:1.4;}";
      successHtml += "</style></head><body><div class=\"container\"><div class=\"success-icon\">🔐</div><h1>安全配置成功！</h1>";
      successHtml += "<p>设备三元组已安全保存，系统正在重启...</p><div class=\"success-details\"><h3>✓ 配置已完成</h3>";
      successHtml += "<p>• 产品ID: " + pid + "</p><p>• 设备名称: " + dname + "</p>";
      successHtml += "<p>• 设备密钥: " + dkey.substring(0, 8) + "****</p></div><div class=\"spinner\"></div>";
      successHtml += "<div class=\"security-note\">🔒 设备将使用新的安全凭证连接IoT平台<br>请确保IoT平台中设备信息与此配置匹配</div>";
      successHtml += "</div>";
      // 添加JavaScript自动检测重启
      successHtml += "<script>";
      successHtml += "var countdown = 8;";
      successHtml += "function updateCountdown() {";
      successHtml += "  if(countdown > 0) {";
      successHtml += "    document.querySelector('h1').innerHTML = '安全配置成功！设备将在 ' + countdown + ' 秒后重启';";
      successHtml += "    countdown--;";
      successHtml += "    setTimeout(updateCountdown, 1000);";
      successHtml += "  } else {";
      successHtml += "    document.querySelector('h1').innerHTML = '设备正在重启中...';";
      successHtml += "    document.querySelector('p').innerHTML = '如果页面长时间无响应，请手动刷新浏览器或重新连接设备热点';";
      successHtml += "  }";
      successHtml += "}";
      successHtml += "setTimeout(updateCountdown, 1000);";
      successHtml += "</script></body></html>";
      configServer.send(200, "text/html; charset=UTF-8", successHtml);
      
      // 确保网页内容完全发送给浏览器
      delay(2000);
      
      // 停止配网服务器，释放资源
      configServer.stop();
      
      // 打印重启信息到串口便于调试
      DEBUG_PRINTLN("[三元组配置] 配置保存成功，设备即将重启...");
      Serial.flush(); // 确保串口输出完成
      
      // 再次延时确保所有操作完成
      delay(1000);
      
      // 执行重启
      ESP.restart();
    };

    // 路由注册
    if (secureTriset) {
        configServer.on("/", HTTP_GET, handleSecureTrisetRoot);
        configServer.on("/triset", HTTP_POST, handleTrisetSave);
    } else {
        configServer.on("/", HTTP_GET, handleRoot);
        configServer.on("/save", HTTP_POST, handleSave);
    }

    configServer.begin();
    unsigned long startTime = millis();
    while (millis() - startTime < portalTimeout) {
      configServer.handleClient();
      delay(10);
      if (esp_task_wdt_status(xTaskGetCurrentTaskHandle()) == ESP_OK)
        esp_task_wdt_reset();
    }
    configServer.stop();
    DEBUG_PRINTLN("[配网] 超时结束");
}


void tryNetworkInit() {
  // ====== 三元组合法性优先保障，setup已处理 ======
  if (gProductID.length() < 4 || gDeviceName.length() < 2 || gDeviceKey.length() < 16) {
    DEBUG_PRINTLN("[安全警告] 三元组无效，不允许进入普通配网Portal。");
    // 这里setup已调用安全Portal，这里不重复处理
    return;
  }

  // ====== WiFi常规配网流程 ======
  if (configuredSSID.length() > 0) {
    if (initWiFi()) {
      DEBUG_PRINTLN("[网络] 已用WiFi配置成功");
      return;
    }
    DEBUG_PRINTLN("[网络] WiFi配置失败 => 启动普通配网Portal");
  } else {
    DEBUG_PRINTLN("[网络] 无WiFi配置 => 启动普通配网Portal");
  }
  // 普通WiFi Portal（不带三元组配置！）
  startConfigPortal(5 * 60 * 1000, false); // 5分钟普通配网
  // Portal后再尝试连接
  initWiFi();
}


void handleEngineFSM() {
  switch (engCtrl.action) {

  case ENG_START:
    if (engCtrl.step == 0) { // STEP 0: 唤醒-轻按
      DEBUG_PRINTF("[发动机] 唤醒脉冲：拉低 %lums\n", gConfig.wakePulseMs);
      digitalWrite(Config::ENGINE_CONTROL_PIN, HIGH);  // 低电平唤醒
      engCtrl.step = 1;
      engCtrl.ts = millis();
    }
    else if (engCtrl.step == 1 && millis() - engCtrl.ts >= gConfig.wakePulseMs) { // STEP 1: 脉冲后拉高
      digitalWrite(Config::ENGINE_CONTROL_PIN, LOW); // 拉高
      DEBUG_PRINTF("[发动机] 唤醒后等待 %lums 再长按启动\n", gConfig.wakeToLongMs);
      engCtrl.step = 2;
      engCtrl.ts = millis();
    }
    else if (engCtrl.step == 2 && millis() - engCtrl.ts >= gConfig.wakeToLongMs) { // STEP 2: 等待后进入"长按"主启动
      DEBUG_PRINTF("[发动机] 启动：拉高 %lums\n", gConfig.engineStartPullLowTime);
      digitalWrite(Config::ENGINE_CONTROL_PIN, HIGH); // 拉高
      engCtrl.step = 3;
      engCtrl.ts = millis();
      // 注意：维持高电平，不需要重复digitalWrite
    }
    else if (engCtrl.step == 3 && millis() - engCtrl.ts >= gConfig.engineStartPullLowTime) { // STEP 3: 启动"长按"完成
      digitalWrite(Config::ENGINE_CONTROL_PIN, LOW); // 抬高完毕后释放
      DEBUG_PRINTLN("[发动机] 等待发动机启动…");
      engCtrl.step = 4;
      engCtrl.ts = millis();
    }
    else if (engCtrl.step == 4 && millis() - engCtrl.ts >= gConfig.engineStartWaitTime) { // STEP 4: 启动后等待
      bool running = isEngineRunning();
      DEBUG_PRINTLN(running ? "[发动机] 启动成功" : "[发动机] 启动失败 → 回退为 OFF");
      if (!running) deviceStatus = false;
      engCtrl.action = ENG_NONE;
    }
    break;

  case ENG_STOP:
    if (engCtrl.step == 0) {
      DEBUG_PRINTF("[发动机] 关机：拉低 %lums\n", gConfig.engineStopPullLowTime);
      digitalWrite(Config::ENGINE_CONTROL_PIN, HIGH);
      engCtrl.step = 1;
      engCtrl.ts = millis();
    }
    else if (engCtrl.step == 1 && millis() - engCtrl.ts >= gConfig.engineStopPullLowTime) {
      digitalWrite(Config::ENGINE_CONTROL_PIN, LOW);
      DEBUG_PRINTLN("[发动机] 等待发动机停机…");
      engCtrl.step = 2;
      engCtrl.ts = millis();
    }
    else if (engCtrl.step == 2 && millis() - engCtrl.ts >= gConfig.engineStopWaitTime) {
      bool running = isEngineRunning();
      DEBUG_PRINTLN(!running ? "[发动机] 关机成功" : "[发动机] 关机失败 → 回退为 ON");
      if (running) deviceStatus = true;
      engCtrl.action = ENG_NONE;
    }
    break;

  default:
    break;
  }
}



// 新增：机油异常事件上报
void reportOilAlarmEvent() {
    // OTA期间暂停事件上报
    if (otaInProgress) {
        DEBUG_PRINTLN("[机油] OTA期间暂停机油事件上报");
        return;
    }
    
    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "%lu", millis()); // 13位以内数字字符串

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"oil_alarm_event\":{"
                    "\"value\":{"
                        "\"oil_alarm\":true"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf,
        (uint64_t)time(nullptr)*1000ULL
    );

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0); // QOS0即可
    DEBUG_PRINTLN("[机油] 已上报机油异常事件");
}

// 新增：存储空间不足事件上报
void reportStorageInsufficientEvent(size_t currentSize, size_t availableSize) {
    static unsigned long lastReportTime = 0;
    unsigned long now = millis();

    // 30分钟内只上报一次，避免频繁上报
    if (lastReportTime != 0 && now - lastReportTime < 30 * 60 * 1000) {
        return;
    }
    lastReportTime = now;

    if (!mqttClient.connected()) {
        DEBUG_PRINTLN("[存储] MQTT未连接，无法上报存储不足事件");
        return;
    }

    char idbuf[20];
    snprintf(idbuf, sizeof(idbuf), "%lu", now);

    char payload[512];
    int ret = snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"storage_insufficient\":{"
                    "\"value\":{"
                        "\"current_size\":%u,"
                        "\"available_size\":%u,"
                        "\"required_size\":%u,"
                        "\"usage_percent\":%.1f"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, 
        (unsigned int)currentSize, 
        (unsigned int)availableSize,
        (unsigned int)(currentSize * 1.2), // 所需空间（120%）
        (float)currentSize * 100.0 / availableSize, // 使用率
        (uint64_t)time(nullptr)*1000ULL
    );
    
    if (ret < 0 || (size_t)ret >= sizeof(payload)) {
        DEBUG_PRINTLN("[ERR] storage_insufficient payload溢出，终止上报");
        return;
    }

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[存储] 已上报存储空间不足事件: 当前=%u 可用=%u (%.1f%%)\n", 
                 (unsigned int)currentSize, (unsigned int)availableSize, (float)currentSize * 100.0 / availableSize);
}


// 远程重启后自动上报事件
void reportRemoteRebootEvent() {
    char idbuf[16];
    snprintf(idbuf, sizeof(idbuf), "%lu", millis()); // 13位以内数字字符串

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"remote_reboot_event\":{"
                    "\"value\":{"
                        "\"remote_reboot_event\":true"   // 这里key要和outputData的identifier一致
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf,
        (uint64_t)time(nullptr)*1000ULL
    );

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0); // QOS0即可
    DEBUG_PRINTLN("[重启] 已上报远程重启事件");
}

// 设备重启事件上报（包含重启原因）
void reportDeviceRestartEvent(int restartReason, String reasonStr) {
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"device_restart\":{"
                    "\"value\":{"
                        "\"restart_reason\":%d,"
                        "\"reason_desc\":\"%s\","
                        "\"uptime_ms\":%lu"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, 
        restartReason, 
        reasonStr.c_str(), 
        millis(),
        (uint64_t)time(nullptr)*1000ULL
    );

    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[重启] 已上报设备重启事件: 原因=%d (%s), 运行时间=%lums\n", 
                 restartReason, reasonStr.c_str(), millis());
}

// 获取重启原因描述
String getRestartReasonString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:   return "Power On";
        case ESP_RST_EXT:       return "External Reset";
        case ESP_RST_SW:        return "Software Reset";
        case ESP_RST_PANIC:     return "Exception/Panic";
        case ESP_RST_INT_WDT:   return "Interrupt Watchdog";
        case ESP_RST_TASK_WDT:  return "Task Watchdog";
        case ESP_RST_WDT:       return "Other Watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep Sleep";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO Reset";
        default:                return "Unknown";
    }
}

// ========== 工业级统计数据管理 ==========

// 加载统计数据
void loadIndustrialStats() {
    Preferences statsPrefs;
    statsPrefs.begin("stats", false);
    
    g_stats.engineTotalMinutes = statsPrefs.getULong("engineMin", 0);
    g_stats.engineStartCount = statsPrefs.getULong("startCount", 0);
    g_stats.oilAlarmCount = statsPrefs.getUShort("oilAlarm", 0);
    g_stats.wifiDisconnectCount = statsPrefs.getUShort("wifiDisc", 0);
    g_stats.mqttFailureCount = statsPrefs.getUShort("mqttFail", 0);
    g_stats.memoryWarningCount = statsPrefs.getUShort("memWarn", 0);
    g_stats.restartCountToday = statsPrefs.getUChar("restartDay", 0);
    g_stats.lastSaveDay = statsPrefs.getULong("lastDay", 0);
    // 注意：maintenanceIntervalHours和rpmAlarmThreshold已移至热配置管理
    
    statsPrefs.end();
    
    // 检查是否是新的一天，重置当日计数器
    uint32_t today = millis() / 86400000; // 简化的日期计算
    if (today != g_stats.lastSaveDay) {
        g_stats.restartCountToday = 1; // 当前这次重启
        g_stats.lastSaveDay = today;
        saveIndustrialStats(); // 立即保存
    } else {
        g_stats.restartCountToday++; // 同一天内的重启
    }
    
    DEBUG_PRINTF("[统计] 加载完成: 运行%lu分钟, 启动%lu次, 今日重启%d次\n", 
                 g_stats.engineTotalMinutes, g_stats.engineStartCount, g_stats.restartCountToday);
}

// 保存统计数据
void saveIndustrialStats() {
    static unsigned long lastSave = 0;
    // 防止频繁保存，最少间隔10秒
    if (millis() - lastSave < 10000) return;
    lastSave = millis();
    
    Preferences statsPrefs;
    statsPrefs.begin("stats", false);
    
    statsPrefs.putULong("engineMin", g_stats.engineTotalMinutes);
    statsPrefs.putULong("startCount", g_stats.engineStartCount);
    statsPrefs.putUShort("oilAlarm", g_stats.oilAlarmCount);
    statsPrefs.putUShort("wifiDisc", g_stats.wifiDisconnectCount);
    statsPrefs.putUShort("mqttFail", g_stats.mqttFailureCount);
    statsPrefs.putUShort("memWarn", g_stats.memoryWarningCount);
    statsPrefs.putUChar("restartDay", g_stats.restartCountToday);
    statsPrefs.putULong("lastDay", g_stats.lastSaveDay);
    // 注意：maintenanceIntervalHours和rpmAlarmThreshold已移至热配置管理
    
    statsPrefs.end();
    g_stats.lastStatsSaveTime = millis();
}

// 更新发动机运行统计
void updateEngineStats() {
    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    
    // 每分钟检查一次
    if (now - lastCheck < 60000) return;
    lastCheck = now;
    
    bool currentlyRunning = isEngineRunning();
    
    // 检测启动事件
    if (currentlyRunning && !g_stats.engineWasRunning) {
        g_stats.engineStartCount++;
        g_stats.lastEngineStartTime = now;
        DEBUG_PRINTF("[统计] 发动机启动，累计启动次数: %lu\n", g_stats.engineStartCount);
    }
    
    // 累计运行时间（按分钟）
    if (currentlyRunning) {
        g_stats.engineTotalMinutes++;
        
        // 每10分钟保存一次数据
        if (g_stats.engineTotalMinutes % 10 == 0) {
            saveIndustrialStats();
        }
    }
    
    g_stats.engineWasRunning = currentlyRunning;
}

// 工业级事件上报函数
void reportMemoryCriticalEvent(size_t freeBytes) {
    if (otaInProgress) return; // OTA期间跳过
    
    g_stats.memoryWarningCount++;
    saveIndustrialStats();
    
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"memory_critical\":{"
                    "\"value\":{"
                        "\"free_bytes\":%u,"
                        "\"timestamp\":%lu"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, (unsigned int)freeBytes, millis()/1000, (uint64_t)time(nullptr)*1000ULL
    );
    
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[统计] 内存严重不足事件已上报: %u bytes, 累计警告%d次\n", 
                 (unsigned int)freeBytes, g_stats.memoryWarningCount);
}

void reportEngineOverspeedEvent(uint32_t currentRPM) {
    if (otaInProgress) return; // OTA期间跳过
    
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"engine_overspeed\":{"
                    "\"value\":{"
                        "\"current_rpm\":%u,"
                        "\"threshold\":%d,"
                        "\"timestamp\":%lu"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, (unsigned int)currentRPM, gConfig.rpmAlarmThreshold, millis()/1000, (uint64_t)time(nullptr)*1000ULL
    );
    
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[统计] 发动机超速事件已上报: %u RPM (阈值 %d)\n", 
                 (unsigned int)currentRPM, gConfig.rpmAlarmThreshold);
}

void reportMaintenanceDueEvent() {
    if (otaInProgress) return; // OTA期间跳过
    
    uint32_t engineHours = g_stats.engineTotalMinutes / 60;
    char idbuf[24];
    snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{"
            "\"id\":\"%s\","
            "\"version\":\"1.0\","
            "\"params\":{"
                "\"maintenance_due\":{"
                    "\"value\":{"
                        "\"engine_hours\":%u,"
                        "\"interval_hours\":%d,"
                        "\"start_count\":%lu,"
                        "\"timestamp\":%lu"
                    "},"
                    "\"time\":%llu"
                "}"
            "}"
        "}",
        idbuf, (unsigned int)engineHours, gConfig.maintenanceIntervalHours, 
        g_stats.engineStartCount, millis()/1000, (uint64_t)time(nullptr)*1000ULL
    );
    
    String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
    publishSyncAndWait(topic.c_str(), payload, 0);
    DEBUG_PRINTF("[统计] 维护到期提醒已上报: %u小时 (间隔 %d小时)\n", 
                 (unsigned int)engineHours, gConfig.maintenanceIntervalHours);
}

// 检查并上报各种工业级事件
void checkIndustrialEvents() {
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck < 30000) return; // 30秒检查一次
    lastCheck = millis();
    
    // 检查内存状态
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < gConfig.memoryWarningThreshold) { // 使用热配置内存警告阈值
        reportMemoryCriticalEvent(freeHeap);
    }
    
    // 检查转速超限
    SensorData sensor = readSensorData();
    if (sensor.rpm > gConfig.rpmAlarmThreshold) {
        static unsigned long lastOverspeedReport = 0;
        if (millis() - lastOverspeedReport > 300000) { // 5分钟内只报一次
            reportEngineOverspeedEvent(sensor.rpm);
            lastOverspeedReport = millis();
        }
    }
    
    // 检查维护到期
    uint32_t engineHours = g_stats.engineTotalMinutes / 60;
    if (engineHours >= gConfig.maintenanceIntervalHours) {
        static bool maintenanceReported = false;
        if (!maintenanceReported) {
            reportMaintenanceDueEvent();
            maintenanceReported = true;
        }
    }
    
    // 检查频繁重启
    if (g_stats.restartCountToday >= 5) {
        static bool frequentRestartReported = false;
        if (!frequentRestartReported && mqttClient.connected()) {
            char idbuf[24];
            snprintf(idbuf, sizeof(idbuf), "%llu", (uint64_t)time(nullptr)*1000ULL + millis() % 1000);
            char payload[256];
            snprintf(payload, sizeof(payload),
                "{"
                    "\"id\":\"%s\","
                    "\"version\":\"1.0\","
                    "\"params\":{"
                        "\"frequent_restart\":{"
                            "\"value\":{"
                                "\"count\":%d,"
                                "\"last_reason\":%d,"
                                "\"timestamp\":%lu"
                            "},"
                            "\"time\":%llu"
                        "}"
                    "}"
                "}",
                idbuf, g_stats.restartCountToday, lastRestartReason, 
                millis()/1000, (uint64_t)time(nullptr)*1000ULL
            );
            
            String topic = String("$sys/") + gProductID + "/" + gDeviceName + "/thing/event/post";
            publishSyncAndWait(topic.c_str(), payload, 0);
            DEBUG_PRINTF("[统计] 频繁重启警报已上报: 今日%d次\n", g_stats.restartCountToday);
            frequentRestartReported = true;
        }
    }
}


static const esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 15000,  // 增加到15秒，为MQTT连接提供足够时间
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
};



// ==== setup ====
void setup() {
  // 🔧 强制禁用所有系统日志输出
  #if !DEBUG_MODE
    esp_log_level_set("*", ESP_LOG_NONE);          // 禁用所有ESP32系统日志
    esp_log_level_set("wifi", ESP_LOG_NONE);       // 禁用WiFi日志
    esp_log_level_set("mbedtls", ESP_LOG_NONE);    // 禁用TLS日志
    esp_log_level_set("esp-tls", ESP_LOG_NONE);    // 禁用TLS日志
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE); // 禁用HTTP客户端日志
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_NONE); // 禁用MQTT客户端日志
  #endif
  
  DEBUG_INIT();  // 条件编译的串口初始化
  randomSeed(analogRead(39));  // 改用GPIO39（无功能冲突）
  
  // 设置初始化LED状态
  setSpecialLedMode(LED_MODE_INIT_BLINK);

  if (!FFat.begin(true)) {
    DEBUG_PRINTLN("[ERROR] FFat挂载失败");
    setSpecialLedMode(LED_MODE_SYSTEM_ERROR);
    while (true) { delay(1000); }
  }
  uartMutex = xSemaphoreCreateMutex();
  if (!uartMutex) {
    DEBUG_PRINTLN("[ERROR] 创建uartMutex失败");
    while (true) { delay(1000); }
  }

  loadDeviceConfig();

  // 🔧 新增：数据完整性检查和恢复
  if (!DataIntegrityManager::verifyConfigIntegrity()) {
    DEBUG_PRINTLN("[启动] 配置数据不完整，尝试从备份恢复");
    DataIntegrityManager::restoreFromBackup();
  }

  // 🔧 新增：电源监控初始化
  PowerManager::init();

  // 看门狗初始化 - 优雅处理已初始化的情况
  esp_err_t err = esp_task_wdt_init(&twdt_config);
  if (err == ESP_ERR_INVALID_STATE) {
      DEBUG_PRINTLN("[WDT] 已经初始化，无需再次初始化");
      // 即使已经初始化，我们也需要重新配置超时时间
      esp_task_wdt_reconfigure(&twdt_config);
  } else if (err == ESP_OK) {
      DEBUG_PRINTLN("[WDT] 成功初始化");
  } else {
      DEBUG_PRINTF("[WDT] 初始化失败，错误码：%d\n", err);
      // 看门狗初始化失败可能导致系统不稳定，记录警告但继续运行
      DEBUG_PRINTLN("[WDT] 警告：看门狗初始化失败，系统稳定性可能受影响");
  }


  wifiPrefs.begin("wifi", false);
  configuredSSID = wifiPrefs.getString("ssid", "");
  configuredPASS = wifiPrefs.getString("pass", "");
  wifiPrefs.end();

  pinMode(Config::LED_RED_PIN, OUTPUT);
  pinMode(Config::LED_GREEN_PIN, OUTPUT);
  digitalWrite(Config::LED_RED_PIN, LED_OFF_LVL);
  digitalWrite(Config::LED_GREEN_PIN, LED_OFF_LVL);

  pinMode(Config::ENGINE_CONTROL_PIN, OUTPUT);
  digitalWrite(Config::ENGINE_CONTROL_PIN, LOW);
  pinMode(Config::ENGINE_OIL_STATUS_PIN, INPUT);

  pinMode(Config::BOOT_BUTTON_PIN, INPUT_PULLUP); 
  pinMode(0, INPUT_PULLUP);

  initPulseCounter();

  loadHotConfig(); // === 加载热参数 ===
  
  // ========== 初始化工业级统计数据 ==========
  loadIndustrialStats();

  // ========== 检查重启原因和设置上报标志 ==========
  esp_reset_reason_t currentResetReason = esp_reset_reason();
  lastRestartReason = (int)currentResetReason;
  lastRestartReasonStr = getRestartReasonString(currentResetReason);
  
  DEBUG_PRINTF("[重启检测] 设备重启原因: %d (%s)\n", 
               lastRestartReason, lastRestartReasonStr.c_str());
  
  // 检查是否为远程重启
  Preferences rebootPrefs;
  rebootPrefs.begin("reboot", false);
  bool wasRemoteReboot = rebootPrefs.getBool(Config::REMOTE_REBOOT_FLAG_KEY, false);
  if (wasRemoteReboot) {
      needReportRemoteReboot = true;
      rebootPrefs.putBool(Config::REMOTE_REBOOT_FLAG_KEY, false); // 清除标志
      DEBUG_PRINTLN("[重启检测] 检测到远程重启标志");
  } else {
      // 非远程重启，设置设备重启事件上报标志
      needReportDeviceRestart = true;
      DEBUG_PRINTLN("[重启检测] 设置设备重启事件上报标志");
  }
  rebootPrefs.end();

  // ========== 新增：检查OTA升级结果 ==========
  Preferences otaResultPrefs;
  otaResultPrefs.begin("ota_result", false);
  bool hadOTAUpgrade = otaResultPrefs.getBool("had_upgrade", false);
  int lastOTAVersion = otaResultPrefs.getInt("version", 0);
  String otaStatus = otaResultPrefs.getString("status", "");
  
        if (hadOTAUpgrade) {
       DEBUG_PRINTF("[OTA] 检测到重启前的OTA升级，版本：%d，状态：%s\n", lastOTAVersion, otaStatus.c_str());
       
       if (otaStatus == "success") {
           // 比较当前固件版本与记录的版本
           if (lastOTAVersion == Config::FIRMWARE_VERSION) {
               DEBUG_PRINTLN("[OTA] OTA升级成功，将在MQTT连接后上报");
               needReportOTASuccess = true;
               reportedOTAVersion = lastOTAVersion;
           } else {
               DEBUG_PRINTF("[OTA] 版本不匹配，预期：%d，当前：%d，可能升级失败\n", 
                            lastOTAVersion, Config::FIRMWARE_VERSION);
               needReportOTAFailure = true;
               otaFailureCode = -1;
               otaFailureMsg = "Version mismatch after restart";
           }
       } else if (otaStatus == "failed") {
           DEBUG_PRINTLN("[OTA] 检测到OTA升级失败，将在MQTT连接后上报");
           needReportOTAFailure = true;
           otaFailureCode = otaResultPrefs.getInt("error_code", -1);
           otaFailureMsg = otaResultPrefs.getString("error_msg", "Unknown error");
       }
       
       // 清除OTA升级标志
       otaResultPrefs.putBool("had_upgrade", false);
       otaResultPrefs.remove("version");
       otaResultPrefs.remove("status");
       otaResultPrefs.remove("error_code");
       otaResultPrefs.remove("error_msg");
   }
   otaResultPrefs.end();

   // ========== 重启后重置OTA状态 ==========
   g_ota_state = false; // 重启后确保OTA状态为false

  // 三元组安全检测
  if (gProductID.length() < 4 || gDeviceName.length() < 2 || gDeviceKey.length() < 16) {
    DEBUG_PRINTLN("[致命] NVS三元组无效，进入安全配网Portal等待人工修复！");
    setSpecialLedMode(LED_MODE_CONFIG_PORTAL);
    startConfigPortal(15 * 60 * 1000, true); // 15分钟安全Portal，第二参数true进入安全三元组配置
    ESP.restart(); // 完成后重启
  }

  tryNetworkInit();
  syncNetworkTime();

  updateFirmwareVersion();  // 现在自己管理Preferences



  xTaskCreatePinnedToCore(checkForUpdatesTask, "CheckUpdates", 14000, NULL, 1, NULL, 1);


  DEBUG_PRINTLN("[Setup] 完成");
  
  // 检查主循环栈状态
  UBaseType_t mainStackSize = uxTaskGetStackHighWaterMark(NULL);
  (void)mainStackSize; // 抑制未使用变量警告
  DEBUG_PRINTF("[Setup] 主循环任务栈剩余检查完成\n");
  
  lastEngineRunning = isEngineRunning();
  if (lastEngineRunning) {
    deviceStatus = true;
    reportSensorData(true);
  }
}


// ==== loop ====
void loop() {
  // 崩溃恢复检测 - 检查是否从异常重启恢复
  static bool crashChecked = false;
  if (!crashChecked) {
    crashChecked = true;
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT || 
        resetReason == ESP_RST_TASK_WDT) {
      DEBUG_PRINTF("[CRASH] 检测到异常重启，原因: %d\n", resetReason);
      // 重置OTA状态，防止卡在异常状态
      otaInProgress = false;
      g_otaStuckDetected = false;
      delay(1000); // 给系统时间稳定
    }
  }
  
  // 🔧 内存健康检查优化 - 检测潜在的内存损坏
  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 10000) { // 每10秒检查一次
    lastMemCheck = millis();
    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap(); // 获取历史最低内存
    size_t heapSize = ESP.getHeapSize();
    
    // 🔧 更智能的内存阈值判断
    if (freeHeap < 8000) { // 提升警告阈值到8KB
      DEBUG_PRINTF("[MEMORY] 警告：可用内存不足: %d bytes (历史最低: %d bytes)\n", freeHeap, minFreeHeap);
      g_stats.memoryWarningCount++; // 统计内存警告次数
      
      // 🔧 内存碎片检测
      if (minFreeHeap < freeHeap * 0.7) {
        DEBUG_PRINTLN("[MEMORY] 检测到严重内存碎片");
      }
      
      if (freeHeap < 5000) { // 提升强制重启阈值到5KB
        DEBUG_PRINTLN("[MEMORY] 内存严重不足，强制重启");
        saveIndustrialStats(); // 保存统计数据
        SystemUtils::safe_delay(1000); // 🔧 使用安全延时
        ESP.restart();
      }
    }
    
    // 🔧 内存使用率监控
    float memUsagePercent = (float)(heapSize - freeHeap) / heapSize * 100;
    if (memUsagePercent > 85.0) {
      DEBUG_PRINTF("[MEMORY] 内存使用率高: %.1f%%\n", memUsagePercent);
    }
  }

  static bool wdtAdded = false;
  if (!wdtAdded) {
    TaskHandle_t loopHandle = xTaskGetCurrentTaskHandle();
    esp_err_t err = esp_task_wdt_add(loopHandle);
    if (err == ESP_OK) {
      DEBUG_PRINTLN("[WDT] 主循环已加到看门狗");
      wdtAdded = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
      DEBUG_PRINTLN("[WDT] 主循环已在看门狗，无需重复添加");
      wdtAdded = true;
    } else if (err == ESP_ERR_NOT_FOUND) {
      DEBUG_PRINTLN("[WDT] 注册失败（未初始化？），setup里得有esp_task_wdt_init");
    } else {
      DEBUG_PRINTF("[WDT] 加入主循环任务失败: %d\n", err);
    }
  }
  // 🔧 主循环强总超时保护增强版
  unsigned long t_loop_start = millis();
  
  // 🔧 循环开始立即喂狗
  SystemUtils::safe_wdt_reset();
  
  // 🔧 检查循环执行频率，防止过于频繁导致看门狗超时
  static unsigned long lastLoopTime = 0;
  unsigned long loopInterval = millis() - lastLoopTime;
  if (loopInterval < 50) { // 如果循环间隔小于50ms，说明执行过于频繁
    SystemUtils::safe_delay(50 - loopInterval); // 强制延时到50ms
  }
  lastLoopTime = millis();

  // ========== OTA状态定期上报 ==========
  static unsigned long lastOTAStatusReport = 0;
  if (!otaInProgress && millis() - lastOTAStatusReport > 300000) { // 5分钟上报一次OTA状态
      if (mqttClient.connected()) {
          reportOTAState();
          lastOTAStatusReport = millis();
      }
  }
  
  if (g_needReportSync) {
      reportSensorData(true);
      g_needReportSync = false;
  }

  updateRPM();
  
  // ========== 工业级统计数据更新 ==========
  updateEngineStats();
  
  // ========== 工业级事件检查 ==========
  if (mqttClient.connected()) {
    checkIndustrialEvents();
  }
  
  // ========== 增强时间同步检查 ==========
  static unsigned long lastTimeSyncCheck = 0;
  if (millis() - lastTimeSyncCheck > 60000) { // 每分钟检查一次
    syncNetworkTime();
    lastTimeSyncCheck = millis();
  }

  // NVS三元组参数合法性检测
  if (gProductID.length() < 4 || gDeviceName.length() < 2 || gDeviceKey.length() < 16) {
    DEBUG_PRINTLN("[致命] NVS三元组无效，拉起配网Portal等待人工修复");
    startConfigPortal(10*60*1000); // 10分钟Portal
    ESP.restart();
  }

  // WiFi断线检测与自动重连 - 智能优化版
  static unsigned long lastWifiRetry = 0;
  static unsigned long wifiLostSince = 0;
  static unsigned long lastAnyOnline = millis();
  static bool wifiConnecting = false;
  static uint8_t wifiRetryCount = 0;  // 新增：重试计数
  static int lastWifiReason = 0;      // 新增：断开原因

  if (WiFi.status() != WL_CONNECTED) {
    if (deviceOnline) {
      deviceOnline = false;
      // 🔧 修复：WiFi断开时不发送数据
      // reportSensorData(true); // 删除这行危险代码
    }
    
      if (wifiLostSince == 0) {
    wifiLostSince = millis();
    // 🔧 修复：使用正确的WiFi状态获取方式
    lastWifiReason = (int)WiFi.status();
    DEBUG_PRINTF("[WiFi] 连接丢失，状态代码: %d\n", lastWifiReason);
  }
    
    // 🔧 智能重连策略
    uint32_t retryInterval = 15000;  // 基础15秒间隔
    
       // 🔧 根据WiFi状态调整重连策略
   switch(lastWifiReason) {
     case WL_NO_SSID_AVAIL:      // 找不到SSID
       retryInterval = 30000;     // 30秒后重试
       break;
     case WL_CONNECT_FAILED:     // 连接失败
       retryInterval = 60000;     // 60秒后重试
       break;
     case WL_CONNECTION_LOST:    // 连接丢失
       retryInterval = 20000;     // 20秒后重试
       break;
     case WL_DISCONNECTED:       // 已断开
     default:
       retryInterval = 15000 + (wifiRetryCount * 5000);  // 递增延时
       break;
   }
    
    // 检查信号环境，信号太差时延长重试间隔
    int lastKnownRSSI = WiFi.RSSI();
    if (lastKnownRSSI < -85) {
      retryInterval *= 2;  // 信号极差时翻倍延时
    }
    
    // 非阻塞式WiFi重连
    if (!wifiConnecting && millis() - lastWifiRetry > retryInterval) {
      DEBUG_PRINTF("[WiFi] 启动智能重连 (第%d次)\n", wifiRetryCount + 1);
      WiFi.disconnect(true);
      // 🔧 使用安全延时替代阻塞延时
      SystemUtils::safe_delay(200);
      WiFi.begin(configuredSSID.c_str(), configuredPASS.c_str());
      wifiConnecting = true;
      lastWifiRetry = millis();
      wifiRetryCount++;
    }
    
    // 🔧 新增：WiFi连接状态监控
    if (wifiConnecting) {
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnecting = false;
        wifiRetryCount = 0;  // 重置重试计数
        DEBUG_PRINTLN("[WiFi] 重连成功");
      } else if (millis() - lastWifiRetry > 25000) {  // 25秒连接超时
        wifiConnecting = false;
        DEBUG_PRINTF("[WiFi] 第%d次重连超时\n", wifiRetryCount);
      }
    }
    
    // 🔧 优化：10分钟改为15分钟，给网络更多恢复时间
    if (millis() - wifiLostSince > 15 * 60 * 1000) {
      DEBUG_PRINTLN("[WiFi] 长时间无法连接，自动重启设备");
      g_stats.wifiDisconnectCount++; // 工业级统计：WiFi断线计数
      saveIndustrialStats();
      SystemUtils::safe_delay(1000);  // 🔧 使用安全延时
      ESP.restart();
    }
  } else {
    // WiFi已连接，重置相关状态
    if (wifiLostSince != 0) {
      DEBUG_PRINTF("[WiFi] 连接恢复，断线时长: %lu秒\n", (millis() - wifiLostSince) / 1000);
    }
    wifiLostSince = 0;
    wifiConnecting = false;
    wifiRetryCount = 0;
    lastAnyOnline = millis(); // 一旦WiFi连上，刷新在线时间
  }

  // 60分钟内还没联网/上云，强制配网 (原来30分钟太短)
  if (millis() - lastAnyOnline > 60 * 60 * 1000) {
    DEBUG_PRINTLN("[极限恢复] 60分钟无法联网，进入强制配网Portal并重启");
    startConfigPortal(10*60*1000);
    ESP.restart();
  }

  // ========== 强制Token刷新检查 ==========
  if (g_forceTokenRefresh && mqttClient.connected()) {
    DEBUG_PRINTLN("[Token] 检测到认证错误，强制断开连接以刷新Token");
    mqttClient.disconnect();
    g_forceTokenRefresh = false;
    g_lastTokenTime = 0;
    delay(1000); // 等待断开完成
  }

  // ========== MQTT自动重连，带外层超时保护 ==========
  if (!mqttClient.connected() && !otaInProgress) { // 关键：OTA期间不进行MQTT重连
    deviceOnline = false;
    reportSensorData(true);
    
    // 快速网络质量检查，避免在极差网络下进行长时间连接尝试
    int currentRSSI = getWifiRSSI();
    if (currentRSSI < -90) { // 信号极差时跳过本次连接
      DEBUG_PRINTF("[MQTT] 信号极差(%d dBm)，跳过本次连接尝试\n", currentRSSI);
    } else {
      unsigned long t0 = millis();
      connectMQTT(); // 内部已做强超时处理
      unsigned long connectDuration = millis() - t0;
      if (connectDuration > 15000) { // 增加到15秒，匹配内部最大超时
        DEBUG_PRINTF("[WDT] connectMQTT 超时(%lums)，主循环自愈重启！\n", connectDuration);
        ESP.restart();
      }
    }
  } 
  // ========== 按用户要求：移除OTA期间的网络活动暂停 ==========
  // OTA期间保持设备在线，继续网络活动

  // 🔧 MQTT主循环保护增强版
  // 按用户要求：OTA期间继续MQTT循环
  if (mqttClient.connected()) {
    // 🔧 提升栈空间检查阈值
    UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
    if (stackRemaining > 1200) { // 🔧 提升到1.2KB栈空间要求
      unsigned long t0 = millis();
      
      // 🔧 MQTT循环前喂狗
      SystemUtils::safe_wdt_reset();
      
      mqttClient.loop();
      
      unsigned long mqttDuration = millis() - t0;
      if (mqttDuration > 2000) { // 🔧 减少到2秒，更严格的超时控制
        DEBUG_PRINTF("[WDT] mqttClient.loop()超时异常(%lums)，主循环自愈重启！\n", mqttDuration);
        saveIndustrialStats(); // 保存统计数据
        SystemUtils::safe_delay(1000);
        ESP.restart();
      }
      
      // 🔧 MQTT循环后再次喂狗
      SystemUtils::safe_wdt_reset();
      
    } else {
      DEBUG_PRINTF("[MQTT] 栈空间不足，跳过MQTT循环: %u bytes\n", stackRemaining);
      // 🔧 栈空间不足时也要喂狗
      SystemUtils::safe_wdt_reset();
    }
  }

  // 内存状态检查
  checkMemoryStatus();
  
  // 栈状态检查
  checkStackStatus();
  
  // 🔧 新增：电源状态监控
  PowerManager::checkPowerStatus();
  
  // 🔧 新增：数据备份管理
  DataIntegrityManager::performDataBackup();

  // 按用户要求：OTA期间继续数据上报
  reportSensorData();

  // 心跳保活 - 按用户要求：OTA期间继续心跳
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 600000) {
    mqttClient.publish("ping", "");
    lastHeartbeat = millis();
  }

  // 远程重启事件上报 - 按用户要求：OTA期间继续上报
  static bool remoteRebootReported = false;
  if (needReportRemoteReboot && mqttClient.connected() && !remoteRebootReported) {
    reportRemoteRebootEvent();
    remoteRebootReported = true;
    needReportRemoteReboot = false;
  }
  
  // 设备重启事件上报 - 包含所有类型的重启
  static bool deviceRestartReported = false;
  if (needReportDeviceRestart && mqttClient.connected() && !deviceRestartReported) {
    reportDeviceRestartEvent(lastRestartReason, lastRestartReasonStr);
    deviceRestartReported = true;
    needReportDeviceRestart = false;
  }

  // ========== 新增：OTA升级结果事件上报 ==========
  static bool otaSuccessReported = false;
  static bool otaFailureReported = false;
  
  // OTA升级成功事件上报
  if (needReportOTASuccess && mqttClient.connected() && !otaSuccessReported) {
    DEBUG_PRINTF("[OTA] 上报OTA升级成功事件，版本：%d\n", reportedOTAVersion);
    reportOTAResult("success", 0, "");
    reportOTAUpdateSuccess(reportedOTAVersion);
    
    // 同时上报ota_state=false
    g_ota_state = false;
    reportOTAState();
    DEBUG_PRINTLN("[OTA] OTA状态已重置为false");
    
    otaSuccessReported = true;
    needReportOTASuccess = false;
  }
  
  // OTA升级失败事件上报
  if (needReportOTAFailure && mqttClient.connected() && !otaFailureReported) {
    DEBUG_PRINTF("[OTA] 上报OTA升级失败事件，错误码：%d，信息：%s\n", otaFailureCode, otaFailureMsg.c_str());
    reportOTAResult("fail", otaFailureCode, otaFailureMsg);
    
    // 同时上报ota_state=false
    g_ota_state = false;
    reportOTAState();
    DEBUG_PRINTLN("[OTA] OTA状态已重置为false");
    
    otaFailureReported = true;
    needReportOTAFailure = false;
  }

  // 发动机状态变化检测与上报 - 按用户要求：OTA期间继续上报
  bool currentEngineRunning = isEngineRunning();
  unsigned long now = millis();
  if (currentEngineRunning != lastEngineRunning &&
      (now - lastEngineChangeTime > engineStateDebounceTime)) {
    lastEngineChangeTime = now;
    if (currentEngineRunning) {
      DEBUG_PRINTLN("[检测] 检测到发动机手动启动 => 同步状态为 ON");
      deviceStatus = true;
    } else {
      DEBUG_PRINTLN("[检测] 检测到发动机手动关闭 => 同步状态为 OFF");
      deviceStatus = false;
    }
    // 按用户要求：OTA期间继续数据上报
    reportSensorData(true);
    lastEngineRunning = currentEngineRunning;
  }

  // LED与机油事件逻辑
  updateLedState();

  // 机油异常检测与上报 - 按用户要求：OTA期间继续上报
  static bool lastOilAlarm = false;
  static unsigned long lastOilEvent = 0;
  SensorData curSensor = readSensorData();
  if (curSensor.oil_alarm && !lastOilAlarm) {
      unsigned long nowMs = millis();
      if (nowMs - lastOilEvent > 10000) {
          reportOilAlarmEvent();
          g_stats.oilAlarmCount++; // 工业级统计：油压报警计数
          saveIndustrialStats();
          lastOilEvent = nowMs;
      }
  }
  lastOilAlarm = curSensor.oil_alarm;

  handleEngineFSM();

  // 主动定时获取属性期望值 - 按用户要求：OTA期间继续获取
  static unsigned long lastDesiredRequest = 0;
  if (mqttClient.connected() && millis() - lastDesiredRequest > 3600000) { // 1小时主动拉一次
      requestDesiredProperty();  // 你已有的函数
      lastDesiredRequest = millis();
  }
  
  // ========== Token健康检查 - 按用户要求：OTA期间继续检查 ==========
  static unsigned long lastTokenHealthCheck = 0;
  if (mqttClient.connected() && millis() - lastTokenHealthCheck > 1800000) { // 30分钟检查一次
    time_t currentTime = getCurrentTime();
    if (g_lastTokenTime != 0 && currentTime - g_lastTokenTime > 64800) { // 18小时后主动刷新
      DEBUG_PRINTF("[Token] Token使用超过18小时，主动刷新。当前时间: %lld, Token时间: %lld\n", 
                   (long long)currentTime, (long long)g_lastTokenTime);
      g_forceTokenRefresh = true;
    }
    lastTokenHealthCheck = millis();
  }

  // RESET按钮逻辑 - 按用户要求：OTA期间继续处理
  static bool lastResetBtn = HIGH;
  static unsigned long resetBtnPressTime = 0;
  bool nowResetBtn = digitalRead(Config::BOOT_BUTTON_PIN);
  // 按用户要求：OTA期间继续按钮处理
    if (lastResetBtn == HIGH && nowResetBtn == LOW) {
      resetBtnPressTime = millis();
    }
    if (lastResetBtn == LOW && nowResetBtn == LOW) {
      if (millis() - resetBtnPressTime > 2000) {
        if (mqttClient.connected()) {
          clearDesiredProperties();
          DEBUG_PRINTLN("[RESET] 已执行属性期望值清除。");
        } else {
          DEBUG_PRINTLN("[RESET] 未联网，无法清除属性期望值。");
        }
        while (digitalRead(Config::BOOT_BUTTON_PIN) == LOW) {
          delay(2);
          if (wdtAdded) esp_task_wdt_reset();
        }
        resetBtnPressTime = millis();
      }
    }
  lastResetBtn = nowResetBtn;

  // 主循环整体执行超时保护，强制WDT自愈
  unsigned long loopDuration = millis() - t_loop_start;
  unsigned long maxLoopTime = 6000; // 基础6秒
  
  // OTA期间使用更宽松的超时限制
  if (otaInProgress) {
    maxLoopTime = 20000; // OTA期间放宽到20秒
  } else {
    // 根据网络状态动态调整超时时间
    if (WiFi.status() != WL_CONNECTED) {
      maxLoopTime = 10000; // WiFi未连接时放宽到10秒
    } else if (!mqttClient.connected()) {
      maxLoopTime = 8000; // MQTT未连接时放宽到8秒
    } else {
      int rssi = getWifiRSSI();
      if (rssi < -80) maxLoopTime = 8000; // 信号差时放宽到8秒
      else if (rssi < -70) maxLoopTime = 7000; // 信号一般时7秒
    }
  }
  
  if (loopDuration > maxLoopTime) {
    DEBUG_PRINTF("[WDT] 主循环整体执行超时（%lums > %lums），强制重启！\n", 
                  loopDuration, maxLoopTime);
    DEBUG_PRINTF("[系统] WiFi状态: %d, MQTT连接: %d, RSSI: %d, OTA: %s\n", 
                  WiFi.status(), mqttClient.connected(), getWifiRSSI(), 
                  otaInProgress ? "进行中" : "否");
    ESP.restart();
  }

  if (wdtAdded) esp_task_wdt_reset();
  delay(10);
}

// 内存监控函数
void checkMemoryStatus() {
    static unsigned long lastMemCheck = 0;
    if (millis() - lastMemCheck < 5000) return; // 5秒检查一次
    lastMemCheck = millis();
    
    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
    (void)minFreeHeap; // 抑制未使用变量警告
    
    // 内存预警（OTA期间减少日志）
    if (freeHeap < 20480) { // 20KB以下预警
        if (!otaInProgress) {
            DEBUG_PRINTF("[内存警告] 剩余堆内存: %u bytes\n", freeHeap);
        }
    }
    
    // 内存危险，强制垃圾回收
    if (freeHeap < 10240) { // 10KB以下危险
        if (!otaInProgress) {
            DEBUG_PRINTF("[内存危险] 执行强制垃圾回收！剩余: %u bytes\n", freeHeap);
        }
        // 这里可以添加一些清理操作
        delay(100);
    }
    
    // 内存极度危险，重启自救
    if (freeHeap < 5120) { // 5KB以下极危险
        DEBUG_PRINTF("[内存极危险] 内存不足，自动重启！剩余: %u bytes\n", freeHeap);
        delay(1000);
        ESP.restart();
    }
}

// 栈监控函数
void checkStackStatus() {
    static unsigned long lastStackCheck = 0;
    if (millis() - lastStackCheck < 10000) return; // 10秒检查一次
    lastStackCheck = millis();
    
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    
    // 栈空间检查和模式切换
    if (stackHighWaterMark < 800 && !g_stackLowMode) {
        g_stackLowMode = true;
        g_lastStackModeChange = millis();
        if (!otaInProgress) {
            DEBUG_PRINTF("[栈保护] 进入栈保护模式，剩余: %u bytes\n", stackHighWaterMark);
        }
    } else if (stackHighWaterMark > 1500 && g_stackLowMode && 
               millis() - g_lastStackModeChange > 30000) { // 30秒后才能退出保护模式
        g_stackLowMode = false;
        g_lastStackModeChange = millis();
        if (!otaInProgress) {
            DEBUG_PRINTF("[栈保护] 退出栈保护模式，剩余: %u bytes\n", stackHighWaterMark);
        }
    }
    
    if (stackHighWaterMark < 1024 && !otaInProgress) { // 小于1KB剩余栈空间预警（OTA期间禁用）
        DEBUG_PRINTF("[栈警告] 主循环栈剩余: %u bytes\n", stackHighWaterMark);
    }
    if (stackHighWaterMark < 512 && !otaInProgress) { // 小于512字节危险（OTA期间禁用）
        DEBUG_PRINTF("[栈危险] 主循环栈严重不足: %u bytes，减少功能调用\n", stackHighWaterMark);
    }
}

