# SmartJet v2.0 - ESP32-S3 IoT发电机控制器软件架构设计

## 项目概述

SmartJet v2.0是基于ESP32-S3的智能发电机控制系统，采用ESP-IDF框架开发，实现了发电机的远程启停控制、状态监控和IoT云平台接入。系统摒弃传统12V大电瓶启动方式，采用4节18650电池串联启动，大幅降低系统重量和复杂度。

### 核心特性
- **多种启动方式**：本地按键、RF遥控、云端远程启动
- **智能监控**：发动机状态、机油压力、电池电量、转速等全方位监控
- **云平台集成**：接入OneNet物联网平台，支持远程监控和控制
- **OTA升级**：支持固件和参数的远程热更新
- **低功耗设计**：待机状态下的智能功耗管理
- **工业级稳定性**：看门狗保护、异常恢复、故障统计

### 系统架构原则
1. **模块化设计**：各功能模块独立，接口清晰
2. **实时响应**：关键控制逻辑具备毫秒级响应能力
3. **异常容错**：完善的错误处理和自恢复机制
4. **可扩展性**：支持后续功能扩展和硬件升级

## 核心模块设计

### 1. 系统管理模块 (System Manager)
**职责**: 系统初始化、状态管理、看门狗控制
**文件**: `src/system/sys_manager.c`, `src/system/sys_manager.h`

```c
typedef enum {
    SYS_STATE_BOOT,           // 系统启动中
    SYS_STATE_INITIALIZING,   // 初始化
    SYS_STATE_WIFI_CONFIG,    // WiFi配网
    SYS_STATE_NORMAL,         // 正常运行
    SYS_STATE_OTA_UPDATE,     // OTA升级
    SYS_STATE_ERROR_RECOVERY, // 异常恢复
    SYS_STATE_LOW_POWER       // 低功耗模式
} sys_state_t;

typedef struct {
    sys_state_t current_state;
    sys_state_t previous_state;
    uint64_t state_enter_time;
    uint32_t uptime_seconds;
    esp_reset_reason_t last_reset_reason;
} sys_status_t;
```

**核心功能**:
- 系统状态机管理
- 任务调度和优先级控制
- 看门狗监控（硬件WDT + 任务WDT）
- 异常处理和系统恢复
- 内存和栈监控

### 2. 发动机控制模块 (Engine Control)
**职责**: 发动机启停控制、状态检测、安全保护
**文件**: `src/engine/engine_ctrl.c`, `src/engine/engine_ctrl.h`

```c
typedef enum {
    ENGINE_STATE_STOPPED,     // 停机状态
    ENGINE_STATE_STARTING,    // 启动中
    ENGINE_STATE_RUNNING,     // 运行中
    ENGINE_STATE_STOPPING,    // 停机中
    ENGINE_STATE_FAULT        // 故障状态
} engine_state_t;

typedef enum {
    START_SOURCE_LOCAL,       // 本地按键启动
    START_SOURCE_REMOTE_RF,   // RF遥控启动
    START_SOURCE_CLOUD,       // 云端远程启动
    START_SOURCE_MANUAL       // 手拉启动（检测）
} start_source_t;

typedef struct {
    engine_state_t state;
    start_source_t last_start_source;
    uint32_t rpm;
    bool oil_pressure_ok;
    bool engine_running_detected;
    uint32_t total_runtime_minutes;
    uint32_t start_count;
    uint64_t last_start_time;
} engine_status_t;
```

**启动序列设计**:
1. **预启动检查**: 机油压力、电池电量、系统状态
2. **风门控制**: 复位到关闭位置 → 开启90度
3. **启动序列**: 
   - Step 0: 唤醒脉冲 (wakePulseMs)
   - Step 1: 等待间隔 (wakeToLongMs)  
   - Step 2: 主启动脉冲 (engineStartPullLowTime)
   - Step 3: 释放启动继电器
   - Step 4: 等待启动检测 (engineStartWaitTime)
4. **启动验证**: RPM检测，成功/失败判定
5. **重试逻辑**: 失败后自动重试（可配置次数）

### 3. 传感器管理模块 (Sensor Manager)
**职责**: 传感器数据采集、滤波处理、状态监控
**文件**: `src/sensors/sensor_mgr.c`, `src/sensors/sensor_mgr.h`

```c
typedef struct {
    // 发动机相关传感器
    uint32_t rpm;                    // 发动机转速
    bool oil_pressure_alarm;         // 机油压力报警
    float engine_temperature;        // 发动机温度
    uint32_t generator_frequency;    // 发电机频率
    
    // 系统传感器
    float board_temperature;         // 主板温度
    int16_t wifi_rssi;              // WiFi信号强度
    bool usb_power_connected;        // USB充电状态
    bool rf_remote_signal;           // RF遥控信号
    
    // 时间戳
    uint64_t timestamp_ms;
} sensor_data_t;
```

**RPM检测算法**:
- 使用GPIO中断捕获点火脉冲
- 多点采样平均滤波 (4点滑动平均)
- 可配置校准系数 (rpmCalibration)
- 防抖动设计 (最小脉冲间隔限制)

### 4. 电池管理模块 (Battery Manager)
**职责**: BQ40Z50通信、电池状态监控、安全保护
**文件**: `src/battery/bq40z50.c`, `src/battery/bq40z50.h`

```c
typedef struct {
    float voltage;              // 电池电压(V)
    float current;              // 充放电流(A) 正值=放电，负值=充电
    uint8_t soc;               // 剩余电量(%)
    float temperature;          // 电池温度(°C)
    uint16_t cycle_count;       // 充电循环次数
    bool charging;              // 充电状态
    bool protection_active;     // 保护功能激活
    uint32_t last_update_time; // 最后更新时间
} battery_status_t;
```

**SBS协议实现**:
- I2C通信 (地址0x16)
- 标准SBS命令支持
- 电压/电流/SOC/温度读取
- 保护状态监控
- 异常处理和重连机制

### 5. 云通信模块 (Cloud Communication)
**职责**: OneNet MQTT通信、数据上报、命令处理
**文件**: `src/cloud/onenet_mqtt.c`, `src/cloud/onenet_mqtt.h`

```c
typedef enum {
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_SUBSCRIBED
} mqtt_state_t;

typedef struct {
    char product_id[32];
    char device_name[64];
    char device_key[64];
    mqtt_state_t state;
    uint32_t last_publish_time;
    uint32_t publish_interval;
    uint16_t message_id;
    bool auto_report;
} mqtt_config_t;
```

**OneNet集成**:
- MQTT over TLS (mqttstls.heclouds.com:8883)
- 设备三元组认证
- 属性上报 (thing/property/post)
- 事件上报 (thing/event/post) 
- 服务调用响应 (thing/service/)
- 自适应上报频率 (运行时1.5s，待机时60s)

### 6. 显示控制模块 (Display Control)
**职责**: ST7789 TFT显示控制、用户界面管理
**文件**: `src/display/st7789_driver.c`, `src/display/ui_manager.c`

```c
typedef enum {
    UI_PAGE_MAIN,           // 主状态页面
    UI_PAGE_ALARMS,         // 告警信息页面
    UI_PAGE_SETTINGS,       // 设置页面
    UI_PAGE_WIFI_CONFIG,    // WiFi配网页面
    UI_PAGE_OTA_UPDATE      // OTA升级页面
} ui_page_t;

typedef struct {
    ui_page_t current_page;
    bool backlight_on;
    uint32_t last_activity_time;
    bool auto_sleep_enabled;
    uint8_t brightness_level;
} display_status_t;
```

**显示内容设计**:
- **主页面**: 发动机状态、转速、电池电压、充电状态、告警图标、网络状态
- **告警页面**: 活动告警列表、历史告警记录
- **设置页面**: 系统信息、版本号、设备ID、配置参数查看
- **配网页面**: WiFi配置状态、二维码/指引信息
- **OTA页面**: 升级进度条、版本信息

### 7. OTA升级模块 (OTA Update)
**职责**: 固件升级、参数热更新、版本管理
**文件**: `src/ota/ota_manager.c`, `src/ota/ota_manager.h`

```c
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_UPDATING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint32_t current_version;
    uint32_t target_version;
    uint8_t progress_percent;
    char error_message[128];
    bool auto_update_enabled;
    uint32_t check_interval_minutes;
} ota_status_t;
```

**升级流程**:
1. 定期检查更新 (update_info.json)
2. 版本比较和决策
3. 安全状态检查 (电池电量、信号强度)
4. 固件下载和校验
5. 分区切换和重启
6. 新版本验证和上报

### 8. 配置管理模块 (Configuration Manager)
**职责**: NVS存储管理、配置加载保存、热更新
**文件**: `src/config/config_mgr.c`, `src/config/config_mgr.h`

```c
typedef struct {
    // 发动机控制参数
    uint32_t engine_start_pull_time;      // 启动拉低时间(ms)
    uint32_t engine_start_wait_time;      // 启动后等待时间(ms)
    uint32_t engine_stop_pull_time;       // 停机拉低时间(ms)
    uint32_t engine_stop_wait_time;       // 停机后等待时间(ms)
    uint32_t wake_pulse_ms;               // 唤醒脉冲时间(ms)
    uint32_t wake_to_long_ms;             // 唤醒到长脉冲间隔(ms)
    
    // 传感器参数
    uint32_t engine_on_rpm_threshold;     // 发动机启动RPM阈值
    uint32_t rpm_alarm_threshold;         // 转速报警阈值
    float rpm_calibration;                // 转速校准系数
    
    // 电压保护参数
    float high_voltage_warning_threshold; // 高电压警告阈值
    float high_voltage_protect_threshold; // 高电压保护阈值
    float low_voltage_warning_threshold;  // 低电压警告阈值
    float low_voltage_protect_threshold;  // 低电压保护阈值
    
    // 维护参数
    uint32_t maintenance_interval_hours;  // 维护间隔小时数
    
    // 网络参数
    int16_t wifi_rssi_weak_threshold;     // WiFi弱信号阈值
    uint32_t mqtt_report_interval;        // MQTT上报间隔
    
    // OTA参数
    bool auto_update_enabled;             // 自动更新使能
    uint32_t ota_check_interval_minutes;  // OTA检查间隔
    int16_t ota_rssi_min;                // OTA最小信号要求
} sys_config_t;
```

## FreeRTOS任务设计

### 任务分配策略
**Core 0 (Protocol CPU)**:
- `task_system_manager` (优先级: 24) - 系统管理和看门狗
- `task_engine_control` (优先级: 20) - 发动机控制
- `task_sensor_monitor` (优先级: 15) - 传感器监控
- `task_cloud_comm` (优先级: 12) - 云通信

**Core 1 (Application CPU)**:
- `task_display_ui` (优先级: 10) - 显示和用户界面
- `task_battery_monitor` (优先级: 8) - 电池监控
- `task_ota_manager` (优先级: 5) - OTA管理

### 任务间通信
- **消息队列**: 用于异步事件传递
- **信号量**: 用于资源保护和同步
- **事件组**: 用于状态同步
- **共享内存**: 用于高频数据交换（互斥锁保护）

## 数据结构和接口

### 全局系统状态
```c
typedef struct {
    sys_status_t system;
    engine_status_t engine;
    sensor_data_t sensors;
    battery_status_t battery;
    display_status_t display;
    mqtt_config_t cloud;
    ota_status_t ota;
    sys_config_t config;
} smartjet_global_t;

extern smartjet_global_t g_smartjet;
```

### 事件系统
```c
typedef enum {
    EVENT_SYSTEM_BOOT,
    EVENT_WIFI_CONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_ENGINE_START_REQUEST,
    EVENT_ENGINE_STOP_REQUEST,
    EVENT_ENGINE_STARTED,
    EVENT_ENGINE_STOPPED,
    EVENT_OIL_ALARM,
    EVENT_OVERSPEED_ALARM,
    EVENT_BATTERY_LOW,
    EVENT_BATTERY_HIGH,
    EVENT_OTA_START,
    EVENT_OTA_SUCCESS,
    EVENT_OTA_FAILED,
    EVENT_BUTTON_PRESSED,
    EVENT_RF_REMOTE_RECEIVED,
    EVENT_CLOUD_COMMAND_RECEIVED
} system_event_t;

typedef struct {
    system_event_t type;
    void *data;
    size_t data_len;
    uint64_t timestamp;
} event_message_t;
```

## 硬件抽象层

### GPIO定义
```c
// 发动机控制
#define GPIO_ENGINE_STARTER_RELAY    GPIO_NUM_26  // 启动继电器
#define GPIO_ENGINE_KILL_OUTPUT      GPIO_NUM_25  // 熄火输出
#define GPIO_ENGINE_RPM_INPUT        GPIO_NUM_36  // 转速信号输入
#define GPIO_ENGINE_OIL_ALARM        GPIO_NUM_14  // 机油报警输入
#define GPIO_ENGINE_CHARGE_STATUS    GPIO_NUM_13  // 充电状态检测

// 步进电机(风门控制)
#define GPIO_STEPPER_A               GPIO_NUM_21
#define GPIO_STEPPER_B               GPIO_NUM_22
#define GPIO_STEPPER_C               GPIO_NUM_23
#define GPIO_STEPPER_D               GPIO_NUM_19

// 用户交互
#define GPIO_BUTTON_START            GPIO_NUM_15  // 启动按键
#define GPIO_LED_START_RED           GPIO_NUM_17  // 启动按键红灯
#define GPIO_LED_START_GREEN         GPIO_NUM_16  // 启动按键绿灯
#define GPIO_WS2812_DATA             GPIO_NUM_18  // WS2812 RGB LED

// 显示器(ST7789)
#define GPIO_TFT_CS                  GPIO_NUM_5
#define GPIO_TFT_DC                  GPIO_NUM_27
#define GPIO_TFT_RST                 GPIO_NUM_33
#define GPIO_TFT_BACKLIGHT           GPIO_NUM_32
#define GPIO_TFT_MOSI                GPIO_NUM_23
#define GPIO_TFT_SCLK                GPIO_NUM_18

// RF遥控
#define GPIO_RF_DATA_OUT             GPIO_NUM_35  // SYN590R数据输出

// 其他
#define GPIO_BUZZER                  GPIO_NUM_4   // 无源蜂鸣器
#define GPIO_USB_DETECT              GPIO_NUM_34  // USB供电检测
#define GPIO_POWER_SAVE_CTRL         GPIO_NUM_12  // 低功耗控制
```

### I2C设备
```c
// BQ40Z50电池管理芯片
#define I2C_BQ40Z50_ADDR            0x16
#define I2C_SCL_GPIO                GPIO_NUM_22
#define I2C_SDA_GPIO                GPIO_NUM_21
```

## 实现计划

### 第一阶段: 核心框架搭建
1. ✅ ESP-IDF项目初始化和配置
2. ✅ FreeRTOS任务框架搭建
3. ✅ 硬件抽象层实现
4. ✅ 系统管理模块基础功能
5. ✅ 配置管理和NVS存储

### 第二阶段: 硬件驱动开发
1. 🔲 GPIO中断和定时器配置
2. 🔲 I2C驱动和BQ40Z50通信
3. 🔲 SPI驱动和ST7789显示
4. 🔲 步进电机驱动实现
5. 🔲 传感器数据采集

### 第三阶段: 核心功能实现
1. 🔲 发动机控制状态机
2. 🔲 传感器监控和数据处理
3. 🔲 显示界面和用户交互
4. 🔲 电池管理和保护逻辑
5. 🔲 本地控制逻辑验证

### 第四阶段: 网络和云服务
1. 🔲 WiFi连接和配网
2. 🔲 OneNet MQTT集成
3. 🔲 云端命令处理
4. 🔲 数据上报和事件推送
5. 🔲 远程控制功能验证

### 第五阶段: 高级功能
1. 🔲 OTA升级机制
2. 🔲 低功耗管理
3. 🔲 故障诊断和恢复
4. 🔲 数据统计和分析
5. 🔲 系统优化和调试

### 第六阶段: 测试和优化
1. 🔲 单元测试和集成测试
2. 🔲 压力测试和稳定性验证
3. 🔲 性能优化和内存优化
4. 🔲 安全性测试
5. 🔲 用户体验优化

## 开发规范

### 代码风格
- 函数命名: `snake_case`
- 变量命名: `snake_case`
- 常量命名: `UPPER_CASE`
- 结构体: `struct_name_t`
- 枚举: `enum_name_t`

### 注释规范
```c
/**
 * @brief 发动机启动控制函数
 * @param start_source 启动源类型
 * @param timeout_ms 启动超时时间(毫秒)
 * @return 启动结果
 *   - ESP_OK: 启动成功
 *   - ESP_FAIL: 启动失败
 *   - ESP_ERR_TIMEOUT: 启动超时
 */
esp_err_t engine_start(start_source_t start_source, uint32_t timeout_ms);
```

### 错误处理
- 使用ESP-IDF标准错误码
- 关键操作必须检查返回值
- 异常情况记录日志并上报
- 系统级错误触发看门狗复位

这个架构设计为SmartJet v2.0项目提供了清晰的技术路线图，后续将按照此架构逐步实现各个模块的具体代码。 