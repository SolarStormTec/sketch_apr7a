/**
 * @file engine_ctrl.h
 * @brief 发动机控制模块
 * @author SmartJet Team
 * @date 2024
 */

#ifndef ENGINE_CTRL_H
#define ENGINE_CTRL_H

#include "smartjet_common.h"
#include "esp_timer.h"

// 发动机控制常量
#define ENGINE_START_WAKE_PULSE_MS      200     // 启动唤醒脉冲时长
#define ENGINE_START_CRANK_MS           3000    // 启动转动时长
#define ENGINE_START_WAIT_RPM_MS        5000    // 等待发动机启动的时间
#define ENGINE_START_RETRY_DELAY_MS     8000    // 启动重试间隔
#define ENGINE_MAX_START_ATTEMPTS       3       // 最大启动尝试次数
#define ENGINE_STOP_KILL_DURATION_MS    2000    // 熄火持续时间

#define ENGINE_RPM_THRESHOLD_DEFAULT    500     // 默认发动机运行RPM阈值
#define ENGINE_RPM_OVERSPEED_DEFAULT    4000    // 默认转速过高阈值

#define ENGINE_CHOKE_RESET_STEPS        200     // 风门复位步数
#define ENGINE_CHOKE_OPEN_STEPS         90      // 风门开启步数（90度）

// 发动机启动序列状态
typedef enum {
    ENGINE_START_IDLE = 0,          // 空闲状态
    ENGINE_START_CHOKE_RESET,       // 风门复位
    ENGINE_START_CHOKE_CLOSE,       // 风门关闭
    ENGINE_START_WAKE_PULSE,        // 启动唤醒脉冲
    ENGINE_START_CRANKING,          // 启动转动
    ENGINE_START_WAIT_RPM,          // 等待RPM启动
    ENGINE_START_CHOKE_OPEN,        // 风门开启
    ENGINE_START_SUCCESS,           // 启动成功
    ENGINE_START_FAILED,            // 启动失败
    ENGINE_START_RETRY_WAIT         // 重试等待
} engine_start_sequence_t;

// 发动机停止序列状态
typedef enum {
    ENGINE_STOP_IDLE = 0,           // 空闲状态
    ENGINE_STOP_CHOKE_RESET,        // 风门复位
    ENGINE_STOP_KILL_IGNITION,      // 熄火
    ENGINE_STOP_FUEL_OFF,           // 关闭燃油
    ENGINE_STOP_SUCCESS             // 停止成功
} engine_stop_sequence_t;

// 发动机启动触发源
typedef enum {
    ENGINE_TRIGGER_NONE = 0,        // 无触发
    ENGINE_TRIGGER_BUTTON,          // 按钮启动
    ENGINE_TRIGGER_REMOTE,          // 遥控启动
    ENGINE_TRIGGER_CLOUD,           // 云端启动
    ENGINE_TRIGGER_MANUAL_PULL      // 手拉启动检测
} engine_trigger_source_t;

// 发动机控制器状态
typedef struct {
    engine_state_t current_state;               // 当前发动机状态
    engine_start_sequence_t start_sequence;     // 启动序列状态
    engine_stop_sequence_t stop_sequence;       // 停止序列状态
    engine_trigger_source_t last_trigger;       // 最后触发源
    
    // 启动统计
    uint32_t start_attempts;                    // 当前启动尝试次数
    uint32_t total_start_count;                 // 总启动次数
    uint32_t failed_start_count;                // 失败启动次数
    
    // 运行时间统计
    uint64_t engine_start_time;                 // 发动机启动时间戳
    uint32_t current_run_duration;              // 当前运行时长(秒)
    uint32_t total_run_hours;                   // 总运行小时数
    
    // 控制标志
    bool start_requested;                       // 启动请求标志
    bool stop_requested;                        // 停止请求标志
    bool emergency_stop;                        // 紧急停止标志
    bool manual_pull_detected;                  // 手拉启动检测标志
    
    // 定时器句柄
    esp_timer_handle_t sequence_timer;          // 序列控制定时器
    esp_timer_handle_t runtime_timer;           // 运行时间统计定时器
    
    // 互斥锁
    SemaphoreHandle_t ctrl_mutex;               // 控制互斥锁
} engine_controller_t;

// 发动机安全检查结果
typedef struct {
    bool oil_pressure_ok;       // 机油压力正常
    bool battery_voltage_ok;    // 电池电压正常
    bool temperature_ok;        // 温度正常
    bool rpm_normal;           // 转速正常
    bool can_start;            // 可以启动
    bool can_continue_run;     // 可以继续运行
} engine_safety_check_t;

// 函数声明
esp_err_t engine_controller_init(void);
void engine_controller_deinit(void);

// 发动机控制接口
esp_err_t engine_start(engine_trigger_source_t trigger);
esp_err_t engine_stop(bool emergency);
esp_err_t engine_emergency_stop(void);

// 状态查询接口
engine_state_t engine_get_state(void);
engine_start_sequence_t engine_get_start_sequence(void);
bool engine_is_running(void);
bool engine_is_starting(void);
bool engine_is_stopping(void);

// 统计信息接口
uint32_t engine_get_total_start_count(void);
uint32_t engine_get_failed_start_count(void);
uint32_t engine_get_total_run_hours(void);
uint32_t engine_get_current_run_duration(void);

// 安全检查接口
engine_safety_check_t engine_safety_check(void);
esp_err_t engine_set_safety_thresholds(uint16_t rpm_threshold, uint16_t overspeed_threshold);

// 配置接口
esp_err_t engine_set_start_parameters(uint32_t wake_pulse_ms, uint32_t crank_ms, 
                                     uint32_t wait_rpm_ms, uint8_t max_attempts);
esp_err_t engine_load_config_from_nvs(void);
esp_err_t engine_save_config_to_nvs(void);

// 事件处理接口
void engine_handle_rpm_change(uint16_t rpm);
void engine_handle_oil_alarm(bool alarm_active);
void engine_handle_manual_pull_start(void);
void engine_handle_remote_trigger(void);

// 内部控制函数声明 - 部分已实现，部分预留
static void engine_start_sequence_handler(void* arg);  // 已实现
static void engine_runtime_update_handler(void* arg);  // 已实现
static esp_err_t engine_execute_start_step(void);      // 已实现
static esp_err_t engine_execute_stop_step(void);       // 已实现

// 预留函数声明 - 后续开发使用
/*
static void engine_stop_sequence_handler(void* arg);
*/

#endif // ENGINE_CTRL_H 