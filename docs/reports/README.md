# SmartJet v2.0 - ESP32-S3 IoT发电机控制器

SmartJet v2.0是基于ESP32-S3的智能发电机控制系统，采用ESP-IDF框架开发，实现了发电机的远程启停控制、状态监控和IoT云平台接入。

## 🚀 项目特性

### 核心功能
- **多种启动方式**：本地按键、RF遥控、云端远程启动
- **智能监控**：发动机状态、机油压力、电池电量、转速等全方位监控
- **云平台集成**：接入OneNet物联网平台，支持远程监控和控制
- **OTA升级**：支持固件和参数的远程热更新
- **低功耗设计**：待机状态下的智能功耗管理
- **工业级稳定性**：看门狗保护、异常恢复、故障统计

### 技术特性
- **模块化架构**：清晰的模块分工，易于维护和扩展
- **实时响应**：关键控制逻辑具备毫秒级响应能力
- **异常容错**：完善的错误处理和自恢复机制
- **高可靠性**：工业级设计，适用于恶劣环境

## 📋 系统架构

### 硬件平台
- **主控芯片**：ESP32-S3 N8R2 (双核, 240MHz, ≥4MB Flash)
- **通信接口**：WiFi, BLE, UART, I²C, SPI
- **传感器接口**：RPM检测、机油压力、温度监控
- **控制输出**：启动继电器、熄火控制、步进电机驱动
- **人机交互**：ST7789 TFT显示屏、LED指示、按键输入

### 软件架构
```
├── 系统管理模块 (System Manager)     - 看门狗、内存监控、状态管理
├── 发动机控制模块 (Engine Control)   - 启停控制、状态检测、安全保护
├── 传感器管理模块 (Sensor Manager)   - 数据采集、滤波处理、状态监控
├── 电池管理模块 (Battery Manager)    - BQ40Z50通信、电池监控、保护
├── 云通信模块 (Cloud Communication)  - OneNet MQTT、数据上报、命令处理
├── 显示控制模块 (Display Control)    - ST7789驱动、UI管理、交互逻辑
├── OTA升级模块 (OTA Update)          - 固件升级、参数更新、版本管理
└── 配置管理模块 (Configuration)      - NVS存储、配置加载、热更新
```

### 任务分配 (FreeRTOS)
**Core 0 (Protocol CPU)**:
- `task_system_manager` (优先级: 24) - 系统管理和看门狗
- `task_engine_control` (优先级: 20) - 发动机控制
- `task_sensor_monitor` (优先级: 15) - 传感器监控
- `task_cloud_comm` (优先级: 12) - 云通信

**Core 1 (Application CPU)**:
- `task_display_ui` (优先级: 10) - 显示和用户界面
- `task_battery_monitor` (优先级: 8) - 电池监控
- `task_ota_manager` (优先级: 5) - OTA管理

## 🛠️ 开发环境

### 必要工具
- **ESP-IDF**: v4.4 或更高版本
- **Python**: 3.7 或更高版本
- **Git**: 版本控制工具
- **编译器**: 支持C99标准的GCC工具链

### 开发板要求
- ESP32-S3 开发板 (推荐 ESP32-S3-DevKitC-1)
- Flash: 4MB 或更大
- PSRAM: 8MB (可选，用于性能提升)

## 🔧 编译和烧录

### 1. 环境准备
```bash
# 安装ESP-IDF
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
source export.sh

# 或使用Windows批处理
install.bat esp32s3
export.bat
```

### 2. 获取项目代码
```bash
git clone <project-repository>
cd smartjet_v2
```

### 3. 配置项目
```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置项目（可选，使用默认配置）
idf.py menuconfig
```

### 4. 编译项目
```bash
# 清理并编译
idf.py clean
idf.py build
```

### 5. 烧录固件
```bash
# 烧录固件和分区表
idf.py flash

# 烧录并开始监控
idf.py flash monitor
```

### 6. 监控调试
```bash
# 查看串口输出
idf.py monitor

# 退出监控: Ctrl+]
```

## 📊 配置说明

### 硬件引脚映射
```c
// 发动机控制
#define GPIO_ENGINE_STARTER_RELAY   GPIO_NUM_26  // 启动继电器
#define GPIO_ENGINE_KILL_OUTPUT     GPIO_NUM_25  // 熄火输出
#define GPIO_ENGINE_RPM_INPUT       GPIO_NUM_36  // 转速信号输入
#define GPIO_ENGINE_OIL_ALARM       GPIO_NUM_14  // 机油报警输入

// 步进电机(风门控制)
#define GPIO_STEPPER_A              GPIO_NUM_21
#define GPIO_STEPPER_B              GPIO_NUM_22
#define GPIO_STEPPER_C              GPIO_NUM_23
#define GPIO_STEPPER_D              GPIO_NUM_19

// 用户交互
#define GPIO_BUTTON_START           GPIO_NUM_15  // 启动按键
#define GPIO_LED_START_RED          GPIO_NUM_17  // 红色LED
#define GPIO_LED_START_GREEN        GPIO_NUM_16  // 绿色LED

// 显示器(ST7789)
#define GPIO_TFT_CS                 GPIO_NUM_5
#define GPIO_TFT_DC                 GPIO_NUM_27
#define GPIO_TFT_RST                GPIO_NUM_33
#define GPIO_TFT_MOSI               GPIO_NUM_23
#define GPIO_TFT_SCLK               GPIO_NUM_18

// I2C设备 (BQ40Z50电池管理)
#define I2C_SCL_GPIO                GPIO_NUM_22
#define I2C_SDA_GPIO                GPIO_NUM_21
```

### 默认配置参数
```c
// 发动机控制参数
engine_start_pull_time = 3000ms      // 启动拉低时间
engine_start_wait_time = 5000ms      // 启动后等待时间
engine_stop_pull_time = 2000ms       // 停机拉低时间
engine_stop_wait_time = 3000ms       // 停机后等待时间

// 传感器参数
engine_on_rpm_threshold = 1800rpm    // 发动机启动判定阈值
rpm_alarm_threshold = 3600rpm        // 转速报警阈值
rpm_calibration = 1.0               // 转速校准系数

// 电压保护参数
high_voltage_warning = 15.0V         // 高电压警告阈值
high_voltage_protect = 16.0V         // 高电压保护阈值
low_voltage_warning = 11.0V          // 低电压警告阈值
low_voltage_protect = 10.0V          // 低电压保护阈值
```

## 🌐 OneNet云平台集成

### 设备配置
在使用前需要配置OneNet设备信息：
```c
// 在NVS中配置或通过代码设置
product_id = "your_product_id"       // OneNet产品ID
device_name = "your_device_name"     // 设备名称
device_key = "your_device_key"       // 设备密钥
```

### 支持的属性和事件
- **属性上报**：发动机状态、电池电压、转速、告警状态等
- **事件推送**：启动/停止事件、告警事件、维护提醒等
- **服务调用**：远程启停、参数配置、OTA升级等

## 🚨 故障排除

### 常见问题

1. **编译错误**
   - 检查ESP-IDF版本是否正确
   - 确认目标芯片设置为esp32s3
   - 检查依赖库是否完整

2. **烧录失败**
   - 确认开发板连接正常
   - 检查串口权限 (Linux: `sudo chmod 666 /dev/ttyUSB0`)
   - 尝试擦除flash: `idf.py erase-flash`

3. **WiFi连接问题**
   - 检查WiFi凭据配置
   - 确认信号强度足够
   - 查看串口日志了解详细错误

4. **OneNet连接失败**
   - 确认设备三元组配置正确
   - 检查网络连接状态
   - 验证OneNet平台设备状态

### 调试技巧
```bash
# 查看详细日志
idf.py monitor

# 设置日志级别
idf.py menuconfig → Component config → Log output → Default log verbosity

# 检查内存使用
# 在代码中调用: esp_get_free_heap_size()

# 分析堆栈使用
# 在代码中调用: uxTaskGetStackHighWaterMark()
```

## 📈 性能优化

### 内存优化
- 使用静态内存分配减少碎片
- 合理设置任务栈大小
- 定期监控内存使用情况

### 功耗优化
- 使用FreeRTOS tickless idle
- 在空闲时关闭外设
- 合理配置WiFi功耗模式

### 实时性优化
- 合理设置任务优先级
- 使用硬件定时器处理关键时序
- 避免在中断中执行耗时操作

## 🔄 OTA升级

系统支持远程固件升级：
1. 云平台触发升级命令
2. 设备检查网络状态和电池电量
3. 下载并验证新固件
4. 安全切换到新版本
5. 自动回滚机制保障可靠性

## 📝 开发规范

### 代码风格
- 函数命名: `snake_case`
- 变量命名: `snake_case`  
- 常量命名: `UPPER_CASE`
- 结构体: `struct_name_t`

### 错误处理
- 使用ESP-IDF标准错误码
- 关键操作必须检查返回值
- 异常情况记录日志并上报

### 注释规范
```c
/**
 * @brief 函数功能描述
 * @param param1 参数1说明
 * @param param2 参数2说明
 * @return 返回值说明
 *   - ESP_OK: 成功
 *   - ESP_FAIL: 失败
 */
esp_err_t function_name(int param1, bool param2);
```

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](LICENSE) 文件了解详情。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目。

## 📞 支持

如有问题，请通过以下方式联系：
- 提交Github Issue
- 发送邮件至项目维护者
- 查看项目Wiki获取更多文档

---

**SmartJet Team © 2024** 