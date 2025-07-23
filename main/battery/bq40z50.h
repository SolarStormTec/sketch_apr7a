/**
 * @file bq40z50.h
 * @brief BQ40Z50-R2 电池管理芯片驱动
 * @author SmartJet Team
 * @date 2024
 */

#ifndef BQ40Z50_H
#define BQ40Z50_H

#include "smartjet_common.h"
#include "driver/i2c.h"

// BQ40Z50 I2C地址
#define BQ40Z50_I2C_ADDR                0x0B    // 7位地址

// I2C配置参数 - 根据最新硬件设计更新
#define BQ40Z50_I2C_MASTER_SCL_IO       GPIO_NUM_39     // SCL引脚
#define BQ40Z50_I2C_MASTER_SDA_IO       GPIO_NUM_38     // SDA引脚
#define BQ40Z50_I2C_MASTER_NUM          I2C_NUM_0       // I2C端口号
#define BQ40Z50_I2C_MASTER_FREQ_HZ      100000          // I2C频率 100kHz
#define BQ40Z50_I2C_MASTER_TX_BUF_LEN   0               // 发送缓冲区
#define BQ40Z50_I2C_MASTER_RX_BUF_LEN   0               // 接收缓冲区
#define BQ40Z50_I2C_TIMEOUT_MS          1000            // I2C超时时间

// SBS (Smart Battery System) 标准命令
#define BQ40Z50_CMD_MANUFACTURER_ACCESS     0x00    // 制造商访问
#define BQ40Z50_CMD_REMAINING_CAPACITY_ALARM 0x01   // 剩余容量报警阈值
#define BQ40Z50_CMD_REMAINING_TIME_ALARM    0x02    // 剩余时间报警阈值
#define BQ40Z50_CMD_BATTERY_MODE            0x03    // 电池模式
#define BQ40Z50_CMD_AT_RATE                 0x04    // 额定功率
#define BQ40Z50_CMD_AT_RATE_TIME_TO_FULL    0x05    // 以额定功率充满时间
#define BQ40Z50_CMD_AT_RATE_TIME_TO_EMPTY   0x06    // 以额定功率耗空时间
#define BQ40Z50_CMD_AT_RATE_OK              0x07    // 额定功率状态
#define BQ40Z50_CMD_TEMPERATURE             0x08    // 温度
#define BQ40Z50_CMD_VOLTAGE                 0x09    // 电压
#define BQ40Z50_CMD_CURRENT                 0x0A    // 电流
#define BQ40Z50_CMD_AVERAGE_CURRENT         0x0B    // 平均电流
#define BQ40Z50_CMD_MAX_ERROR               0x0C    // 最大误差
#define BQ40Z50_CMD_RELATIVE_SOC            0x0D    // 相对电量百分比
#define BQ40Z50_CMD_ABSOLUTE_SOC            0x0E    // 绝对电量百分比
#define BQ40Z50_CMD_REMAINING_CAPACITY      0x0F    // 剩余容量
#define BQ40Z50_CMD_FULL_CHARGE_CAPACITY    0x10    // 满充容量
#define BQ40Z50_CMD_RUN_TIME_TO_EMPTY       0x11    // 运行时间至空
#define BQ40Z50_CMD_AVERAGE_TIME_TO_EMPTY   0x12    // 平均时间至空
#define BQ40Z50_CMD_AVERAGE_TIME_TO_FULL    0x13    // 平均时间至满
#define BQ40Z50_CMD_CHARGING_CURRENT        0x14    // 充电电流
#define BQ40Z50_CMD_CHARGING_VOLTAGE        0x15    // 充电电压
#define BQ40Z50_CMD_BATTERY_STATUS          0x16    // 电池状态
#define BQ40Z50_CMD_CYCLE_COUNT             0x17    // 循环计数
#define BQ40Z50_CMD_DESIGN_CAPACITY         0x18    // 设计容量
#define BQ40Z50_CMD_DESIGN_VOLTAGE          0x19    // 设计电压
#define BQ40Z50_CMD_SPEC_INFO               0x1A    // 规格信息
#define BQ40Z50_CMD_MFG_DATE                0x1B    // 制造日期
#define BQ40Z50_CMD_SERIAL_NUMBER           0x1C    // 序列号
#define BQ40Z50_CMD_MFG_NAME                0x20    // 制造商名称
#define BQ40Z50_CMD_DEVICE_NAME             0x21    // 设备名称
#define BQ40Z50_CMD_DEVICE_CHEMISTRY        0x22    // 设备化学成分
#define BQ40Z50_CMD_MFG_DATA                0x23    // 制造商数据

// 电池状态标志位
#define BQ40Z50_STATUS_OVER_CHARGED_ALARM   (1 << 15)   // 过充报警
#define BQ40Z50_STATUS_TERMINATE_CHARGE_ALARM (1 << 14) // 终止充电报警
#define BQ40Z50_STATUS_OVER_TEMP_ALARM      (1 << 12)   // 过温报警
#define BQ40Z50_STATUS_TERMINATE_DISCHARGE_ALARM (1 << 11) // 终止放电报警
#define BQ40Z50_STATUS_REMAINING_CAPACITY_ALARM (1 << 9) // 剩余容量报警
#define BQ40Z50_STATUS_REMAINING_TIME_ALARM (1 << 8)    // 剩余时间报警
#define BQ40Z50_STATUS_INITIALIZED          (1 << 7)    // 已初始化
#define BQ40Z50_STATUS_DISCHARGING          (1 << 6)    // 放电中
#define BQ40Z50_STATUS_FULLY_CHARGED        (1 << 5)    // 已满充
#define BQ40Z50_STATUS_FULLY_DISCHARGED     (1 << 4)    // 已放空

// 电池模式标志位
#define BQ40Z50_MODE_INTERNAL_CHARGE_CONTROLLER (1 << 0)  // 内部充电控制器
#define BQ40Z50_MODE_PRIMARY_BATTERY_SUPPORT    (1 << 1)  // 主电池支持
#define BQ40Z50_MODE_CONDITION_FLAG             (1 << 7)  // 状况标志
#define BQ40Z50_MODE_CHARGE_CONTROLLER_ENABLED  (1 << 8)  // 充电控制器使能
#define BQ40Z50_MODE_PRIMARY_BATTERY            (1 << 9)  // 主电池
#define BQ40Z50_MODE_ALARM_MODE                 (1 << 13) // 报警模式
#define BQ40Z50_MODE_CHARGER_MODE               (1 << 14) // 充电器模式
#define BQ40Z50_MODE_CAPACITY_MODE              (1 << 15) // 容量模式

// BQ40Z50电池数据结构
typedef struct {
    // 基本电池信息
    uint16_t voltage_mv;                // 电池电压 (mV)
    int16_t current_ma;                 // 电池电流 (mA, 正数为充电，负数为放电)
    int16_t average_current_ma;         // 平均电流 (mA)
    uint16_t temperature_k;             // 电池温度 (0.1K)
    float temperature_c;                // 电池温度 (°C)
    
    // 电量信息
    uint8_t relative_soc;               // 相对电量百分比 (%)
    uint8_t absolute_soc;               // 绝对电量百分比 (%)
    uint16_t remaining_capacity_mah;    // 剩余容量 (mAh)
    uint16_t full_charge_capacity_mah;  // 满充容量 (mAh)
    uint16_t design_capacity_mah;       // 设计容量 (mAh)
    
    // 时间信息
    uint16_t time_to_empty_min;         // 至空时间 (分钟)
    uint16_t time_to_full_min;          // 至满时间 (分钟)
    
    // 状态信息
    uint16_t battery_status;            // 电池状态寄存器
    uint16_t battery_mode;              // 电池模式寄存器
    uint16_t cycle_count;               // 循环计数
    
    // 充电信息
    uint16_t charging_current_ma;       // 充电电流 (mA)
    uint16_t charging_voltage_mv;       // 充电电压 (mV)
    
    // 设计规格
    uint16_t design_voltage_mv;         // 设计电压 (mV)
    uint16_t serial_number;             // 序列号
    uint16_t manufacture_date;          // 制造日期
    
    // 标志位
    bool is_charging;                   // 充电状态
    bool is_discharging;                // 放电状态
    bool is_fully_charged;              // 满充状态
    bool is_fully_discharged;           // 放空状态
    bool has_error;                     // 错误状态
    
    // 报警标志
    bool over_charged_alarm;            // 过充报警
    bool over_temp_alarm;               // 过温报警
    bool remaining_capacity_alarm;      // 剩余容量报警
    bool remaining_time_alarm;          // 剩余时间报警
    
    // 数据有效性
    bool data_valid;                    // 数据有效标志
    uint64_t last_update_time;          // 最后更新时间
} bq40z50_data_t;

// BQ40Z50控制器结构
typedef struct {
    bool initialized;                   // 初始化标志
    i2c_port_t i2c_port;              // I2C端口
    uint8_t device_address;            // 设备地址
    bq40z50_data_t battery_data;       // 电池数据
    SemaphoreHandle_t data_mutex;      // 数据互斥锁
    
    // 配置参数
    uint32_t update_interval_ms;       // 更新间隔
    uint16_t low_voltage_threshold;    // 低电压阈值
    uint16_t high_voltage_threshold;   // 高电压阈值
    uint8_t low_soc_threshold;         // 低电量阈值
    uint8_t critical_soc_threshold;    // 危险电量阈值
} bq40z50_controller_t;

// 函数声明
esp_err_t bq40z50_init(void);
void bq40z50_deinit(void);

// 基本读写操作
esp_err_t bq40z50_read_word(uint8_t command, uint16_t* data);
esp_err_t bq40z50_write_word(uint8_t command, uint16_t data);
esp_err_t bq40z50_read_block(uint8_t command, uint8_t* data, size_t* length);

// 数据更新和获取
esp_err_t bq40z50_update_all_data(void);
esp_err_t bq40z50_update_basic_data(void);
esp_err_t bq40z50_update_status_data(void);

// 电池信息获取
bq40z50_data_t bq40z50_get_battery_data(void);
uint16_t bq40z50_get_voltage_mv(void);
int16_t bq40z50_get_current_ma(void);
uint8_t bq40z50_get_soc_percent(void);
float bq40z50_get_temperature_c(void);
bool bq40z50_is_charging(void);
bool bq40z50_is_fully_charged(void);

// 状态检查
bool bq40z50_is_battery_present(void);
bool bq40z50_has_error(void);
bool bq40z50_has_alarm(void);
esp_err_t bq40z50_check_safety_status(void);

// 配置和校准
esp_err_t bq40z50_set_update_interval(uint32_t interval_ms);
esp_err_t bq40z50_set_safety_thresholds(uint16_t low_voltage, uint16_t high_voltage, 
                                       uint8_t low_soc, uint8_t critical_soc);
esp_err_t bq40z50_calibrate(void);

// 制造商信息
esp_err_t bq40z50_get_manufacturer_name(char* name, size_t max_length);
esp_err_t bq40z50_get_device_name(char* name, size_t max_length);
esp_err_t bq40z50_get_device_chemistry(char* chemistry, size_t max_length);

// 诊断和维护
void bq40z50_dump_all_registers(void);
esp_err_t bq40z50_self_test(void);
void bq40z50_print_battery_info(void);

// 内部函数声明 - 部分已实现，部分预留
static bool bq40z50_validate_data(void);  // 已实现

// 预留函数声明 - 后续开发使用
/*
static esp_err_t bq40z50_i2c_init(void);
static esp_err_t bq40z50_i2c_deinit(void);
static esp_err_t bq40z50_parse_battery_status(uint16_t status);
static esp_err_t bq40z50_parse_battery_mode(uint16_t mode);
static float bq40z50_temperature_k_to_c(uint16_t temp_k);
*/

#endif // BQ40Z50_H 