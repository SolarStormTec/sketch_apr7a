/**
 * @file stepper_motor.h
 * @brief 步进电机控制模块 - 风门控制
 * @author SmartJet Team
 * @date 2024
 */

#ifndef STEPPER_MOTOR_H
#define STEPPER_MOTOR_H

#include "smartjet_common.h"
#include "esp_timer.h"

// 步进电机参数
#define STEPPER_STEPS_PER_REVOLUTION    200     // 每圈步数
#define STEPPER_MAX_POSITION           180     // 最大位置（度）
#define STEPPER_MIN_POSITION            0      // 最小位置（度）
#define STEPPER_DEFAULT_SPEED_RPM       30     // 默认转速(RPM)
#define STEPPER_STEP_DELAY_US          2000    // 步进间隔(微秒)

#define STEPPER_RESET_EXTRA_STEPS       50     // 复位时额外步数，确保到达极限位置

// 步进电机状态
typedef enum {
    STEPPER_STATE_IDLE = 0,         // 空闲状态
    STEPPER_STATE_MOVING,           // 运动中
    STEPPER_STATE_RESETTING,        // 复位中
    STEPPER_STATE_ERROR             // 错误状态
} stepper_state_t;

// 步进电机方向
typedef enum {
    STEPPER_DIR_CW = 0,             // 顺时针
    STEPPER_DIR_CCW                 // 逆时针
} stepper_direction_t;

// 步进模式
typedef enum {
    STEPPER_MODE_FULL_STEP = 0,     // 全步模式
    STEPPER_MODE_HALF_STEP          // 半步模式
} stepper_mode_t;

// 步进电机控制结构
typedef struct {
    stepper_state_t state;          // 当前状态
    stepper_mode_t mode;            // 步进模式
    int16_t current_position;       // 当前位置（度）
    int16_t target_position;        // 目标位置（度）
    uint32_t step_delay_us;         // 步进间隔（微秒）
    bool is_homed;                  // 是否已归位
    
    // 控制变量
    uint8_t step_index;             // 当前步进索引
    stepper_direction_t direction;  // 运动方向
    
    // 定时器
    esp_timer_handle_t step_timer;  // 步进定时器
    
    // 互斥锁
    SemaphoreHandle_t motor_mutex;  // 电机控制锁
} stepper_motor_t;

// 全步模式步进序列（4相）
extern const uint8_t stepper_full_step_sequence[4][4];

// 半步模式步进序列（8相）
extern const uint8_t stepper_half_step_sequence[8][4];

// 函数声明
esp_err_t stepper_motor_init(void);
void stepper_motor_deinit(void);

// 基本控制接口
esp_err_t stepper_motor_move_steps(int32_t steps, stepper_direction_t direction);
esp_err_t stepper_motor_move_to_position(int16_t position_degrees);
esp_err_t stepper_motor_move_relative(int16_t relative_degrees);

// 归位和复位
esp_err_t stepper_motor_home(void);
esp_err_t stepper_motor_reset_position(void);

// 控制参数设置
esp_err_t stepper_motor_set_speed(uint32_t rpm);
esp_err_t stepper_motor_set_mode(stepper_mode_t mode);
esp_err_t stepper_motor_stop(void);

// 状态查询
stepper_state_t stepper_motor_get_state(void);
int16_t stepper_motor_get_position(void);
int16_t stepper_motor_get_target_position(void);
bool stepper_motor_is_moving(void);
bool stepper_motor_is_homed(void);

// 校准功能
esp_err_t stepper_motor_calibrate(void);
esp_err_t stepper_motor_set_current_position(int16_t position);

// 内部函数声明 - 预留用于步进电机精细控制
/*
static void stepper_timer_callback(void* arg);
static esp_err_t stepper_single_step(stepper_direction_t direction);
static void stepper_set_coil_outputs(uint8_t coil_a, uint8_t coil_b, uint8_t coil_c, uint8_t coil_d);
static void stepper_disable_all_coils(void);
static int32_t stepper_degrees_to_steps(int16_t degrees);
static int16_t stepper_steps_to_degrees(int32_t steps);
*/

#endif // STEPPER_MOTOR_H 