/**
 * @file stepper_motor.c
 * @brief 步进电机控制模块实现
 * @author SmartJet Team
 * @date 2024
 */

#include "stepper_motor.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "STEPPER_MOTOR";

// 前向声明
static void stepper_timer_callback(void* arg);
static esp_err_t stepper_single_step(stepper_direction_t direction);
static void stepper_set_coil_outputs(uint8_t coil_a, uint8_t coil_b, uint8_t coil_c, uint8_t coil_d);
static void stepper_disable_all_coils(void);
static int32_t stepper_degrees_to_steps(int16_t degrees);
static int16_t stepper_steps_to_degrees(int32_t steps);

// 全局步进电机控制器实例
static stepper_motor_t g_stepper = {0};
static bool g_stepper_initialized = false;

// 全步模式步进序列（ULN2003驱动，4相）
const uint8_t stepper_full_step_sequence[4][4] = {
    {1, 0, 0, 1},  // 步骤1: A相、D相通电
    {1, 1, 0, 0},  // 步骤2: A相、B相通电
    {0, 1, 1, 0},  // 步骤3: B相、C相通电
    {0, 0, 1, 1}   // 步骤4: C相、D相通电
};

// 半步模式步进序列（8相）
const uint8_t stepper_half_step_sequence[8][4] = {
    {1, 0, 0, 0},  // 半步1: 只有A相
    {1, 1, 0, 0},  // 半步2: A相、B相
    {0, 1, 0, 0},  // 半步3: 只有B相
    {0, 1, 1, 0},  // 半步4: B相、C相
    {0, 0, 1, 0},  // 半步5: 只有C相
    {0, 0, 1, 1},  // 半步6: C相、D相
    {0, 0, 0, 1},  // 半步7: 只有D相
    {1, 0, 0, 1}   // 半步8: D相、A相
};

/**
 * @brief 步进电机初始化
 */
esp_err_t stepper_motor_init(void)
{
    if (g_stepper_initialized) {
        ESP_LOGW(TAG, "步进电机已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化步进电机控制器...");
    
    // 创建互斥锁
    g_stepper.motor_mutex = xSemaphoreCreateMutex();
    if (!g_stepper.motor_mutex) {
        ESP_LOGE(TAG, "创建步进电机互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 配置GPIO输出
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << GPIO_STEPPER_MOTOR_A) | 
                          (1ULL << GPIO_STEPPER_MOTOR_B) |
                          (1ULL << GPIO_STEPPER_MOTOR_C) | 
                          (1ULL << GPIO_STEPPER_MOTOR_D);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "配置步进电机GPIO失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化所有线圈为关闭状态
    stepper_disable_all_coils();
    
    // 创建步进定时器
    esp_timer_create_args_t timer_args = {
        .callback = stepper_timer_callback,
        .arg = NULL,
        .name = "stepper_timer"
    };
    ret = esp_timer_create(&timer_args, &g_stepper.step_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建步进定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化参数
    g_stepper.state = STEPPER_STATE_IDLE;
    g_stepper.mode = STEPPER_MODE_FULL_STEP;
    g_stepper.current_position = 0;
    g_stepper.target_position = 0;
    g_stepper.step_delay_us = STEPPER_STEP_DELAY_US;
    g_stepper.is_homed = false;
    g_stepper.step_index = 0;
    g_stepper.direction = STEPPER_DIR_CW;
    
    g_stepper_initialized = true;
    ESP_LOGI(TAG, "步进电机控制器初始化完成");
    
    return ESP_OK;
    
cleanup:
    stepper_motor_deinit();
    return ret;
}

/**
 * @brief 步进电机反初始化
 */
void stepper_motor_deinit(void)
{
    if (!g_stepper_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化步进电机控制器...");
    
    // 停止定时器
    if (g_stepper.step_timer) {
        esp_timer_stop(g_stepper.step_timer);
        esp_timer_delete(g_stepper.step_timer);
        g_stepper.step_timer = NULL;
    }
    
    // 关闭所有线圈
    stepper_disable_all_coils();
    
    // 删除互斥锁
    if (g_stepper.motor_mutex) {
        vSemaphoreDelete(g_stepper.motor_mutex);
        g_stepper.motor_mutex = NULL;
    }
    
    g_stepper_initialized = false;
    ESP_LOGI(TAG, "步进电机控制器反初始化完成");
}

/**
 * @brief 移动指定步数
 */
esp_err_t stepper_motor_move_steps(int32_t steps, stepper_direction_t direction)
{
    if (!g_stepper_initialized) {
        ESP_LOGE(TAG, "步进电机未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (steps <= 0) {
        ESP_LOGW(TAG, "步数必须大于0");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_stepper.motor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取步进电机锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_stepper.state == STEPPER_STATE_MOVING) {
        ESP_LOGW(TAG, "步进电机正在运动中");
        xSemaphoreGive(g_stepper.motor_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "移动步进电机 %ld 步，方向: %s", steps, 
             direction == STEPPER_DIR_CW ? "顺时针" : "逆时针");
    
    // 设置目标位置
    int32_t step_change = direction == STEPPER_DIR_CW ? steps : -steps;
    int16_t target_degrees = stepper_steps_to_degrees(
        stepper_degrees_to_steps(g_stepper.current_position) + step_change);
    
    // 检查位置限制
    if (target_degrees < STEPPER_MIN_POSITION || target_degrees > STEPPER_MAX_POSITION) {
        ESP_LOGW(TAG, "目标位置 %d 超出范围 [%d, %d]", 
                target_degrees, STEPPER_MIN_POSITION, STEPPER_MAX_POSITION);
        xSemaphoreGive(g_stepper.motor_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_stepper.target_position = target_degrees;
    g_stepper.direction = direction;
    g_stepper.state = STEPPER_STATE_MOVING;
    
    // 启动步进定时器
    esp_err_t ret = esp_timer_start_periodic(g_stepper.step_timer, g_stepper.step_delay_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动步进定时器失败: %s", esp_err_to_name(ret));
        g_stepper.state = STEPPER_STATE_IDLE;
        xSemaphoreGive(g_stepper.motor_mutex);
        return ret;
    }
    
    xSemaphoreGive(g_stepper.motor_mutex);
    return ESP_OK;
}

/**
 * @brief 移动到指定位置
 */
esp_err_t stepper_motor_move_to_position(int16_t position_degrees)
{
    if (!g_stepper_initialized) {
        ESP_LOGE(TAG, "步进电机未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (position_degrees < STEPPER_MIN_POSITION || position_degrees > STEPPER_MAX_POSITION) {
        ESP_LOGE(TAG, "目标位置 %d 超出范围 [%d, %d]", 
                position_degrees, STEPPER_MIN_POSITION, STEPPER_MAX_POSITION);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_stepper.motor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取步进电机锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_stepper.state == STEPPER_STATE_MOVING) {
        ESP_LOGW(TAG, "步进电机正在运动中");
        xSemaphoreGive(g_stepper.motor_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 计算移动方向和步数
    int16_t position_diff = position_degrees - g_stepper.current_position;
    if (position_diff == 0) {
        ESP_LOGI(TAG, "已在目标位置 %d 度", position_degrees);
        xSemaphoreGive(g_stepper.motor_mutex);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "移动到位置 %d 度 (当前: %d 度)", position_degrees, g_stepper.current_position);
    
    g_stepper.target_position = position_degrees;
    g_stepper.direction = position_diff > 0 ? STEPPER_DIR_CW : STEPPER_DIR_CCW;
    g_stepper.state = STEPPER_STATE_MOVING;
    
    // 启动步进定时器
    esp_err_t ret = esp_timer_start_periodic(g_stepper.step_timer, g_stepper.step_delay_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动步进定时器失败: %s", esp_err_to_name(ret));
        g_stepper.state = STEPPER_STATE_IDLE;
        xSemaphoreGive(g_stepper.motor_mutex);
        return ret;
    }
    
    xSemaphoreGive(g_stepper.motor_mutex);
    return ESP_OK;
}

/**
 * @brief 复位到零位置
 */
esp_err_t stepper_motor_reset_position(void)
{
    if (!g_stepper_initialized) {
        ESP_LOGE(TAG, "步进电机未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "步进电机复位到零位置");
    
    if (xSemaphoreTake(g_stepper.motor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取步进电机锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    if (g_stepper.state == STEPPER_STATE_MOVING) {
        esp_timer_stop(g_stepper.step_timer);
    }
    
    g_stepper.state = STEPPER_STATE_RESETTING;
    g_stepper.target_position = 0;
    g_stepper.direction = STEPPER_DIR_CCW; // 逆时针到零位置
    
    // 启动步进定时器，执行复位序列
    esp_err_t ret = esp_timer_start_periodic(g_stepper.step_timer, g_stepper.step_delay_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动复位定时器失败: %s", esp_err_to_name(ret));
        g_stepper.state = STEPPER_STATE_IDLE;
        xSemaphoreGive(g_stepper.motor_mutex);
        return ret;
    }
    
    xSemaphoreGive(g_stepper.motor_mutex);
    return ESP_OK;
}

/**
 * @brief 停止步进电机
 */
esp_err_t stepper_motor_stop(void)
{
    if (!g_stepper_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "停止步进电机");
    
    if (xSemaphoreTake(g_stepper.motor_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取步进电机锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    // 停止定时器
    esp_timer_stop(g_stepper.step_timer);
    
    // 关闭所有线圈
    stepper_disable_all_coils();
    
    g_stepper.state = STEPPER_STATE_IDLE;
    
    xSemaphoreGive(g_stepper.motor_mutex);
    return ESP_OK;
}

/**
 * @brief 设置步进电机转速
 */
esp_err_t stepper_motor_set_speed(uint32_t rpm)
{
    if (rpm == 0 || rpm > 100) {  // 限制最大转速
        ESP_LOGE(TAG, "转速 %lu 超出范围 [1, 100]", rpm);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 计算步进间隔：60秒/分钟 * 1000000微秒/秒 / (rpm * 每圈步数)
    uint32_t steps_per_minute = rpm * STEPPER_STEPS_PER_REVOLUTION;
    if (g_stepper.mode == STEPPER_MODE_HALF_STEP) {
        steps_per_minute *= 2;  // 半步模式步数翻倍
    }
    
    g_stepper.step_delay_us = 60000000UL / steps_per_minute;
    
    ESP_LOGI(TAG, "设置步进电机转速: %lu RPM, 步进间隔: %lu us", rpm, g_stepper.step_delay_us);
    
    return ESP_OK;
}

/**
 * @brief 步进定时器回调函数
 */
static void stepper_timer_callback(void* arg)
{
    if (!g_stepper_initialized || g_stepper.state == STEPPER_STATE_IDLE) {
        return;
    }
    
    // 检查是否到达目标位置
    if (g_stepper.state == STEPPER_STATE_MOVING) {
        if (g_stepper.current_position == g_stepper.target_position) {
            esp_timer_stop(g_stepper.step_timer);
            stepper_disable_all_coils();
            g_stepper.state = STEPPER_STATE_IDLE;
            ESP_LOGI(TAG, "到达目标位置: %d 度", g_stepper.target_position);
            return;
        }
    }
    
    // 复位模式：移动到零位置并额外移动一些步数确保到位
    if (g_stepper.state == STEPPER_STATE_RESETTING) {
        static int32_t reset_extra_steps = 0;
        
        if (g_stepper.current_position <= 0) {
            reset_extra_steps++;
            if (reset_extra_steps >= STEPPER_RESET_EXTRA_STEPS) {
                // 复位完成
                esp_timer_stop(g_stepper.step_timer);
                stepper_disable_all_coils();
                g_stepper.state = STEPPER_STATE_IDLE;
                g_stepper.current_position = 0;
                g_stepper.is_homed = true;
                reset_extra_steps = 0;
                ESP_LOGI(TAG, "复位完成，当前位置: 0 度");
                return;
            }
        }
    }
    
    // 执行单步
    stepper_single_step(g_stepper.direction);
}

/**
 * @brief 执行单步移动
 */
static esp_err_t stepper_single_step(stepper_direction_t direction)
{
    const uint8_t *sequence;
    uint8_t sequence_length;
    
    // 选择步进序列
    if (g_stepper.mode == STEPPER_MODE_HALF_STEP) {
        sequence = (const uint8_t*)stepper_half_step_sequence;
        sequence_length = 8;
    } else {
        sequence = (const uint8_t*)stepper_full_step_sequence;
        sequence_length = 4;
    }
    
    // 更新步进索引
    if (direction == STEPPER_DIR_CW) {
        g_stepper.step_index = (g_stepper.step_index + 1) % sequence_length;
    } else {
        g_stepper.step_index = (g_stepper.step_index + sequence_length - 1) % sequence_length;
    }
    
    // 设置线圈输出
    const uint8_t *step = sequence + (g_stepper.step_index * 4);
    stepper_set_coil_outputs(step[0], step[1], step[2], step[3]);
    
    // 更新当前位置
    int32_t current_steps = stepper_degrees_to_steps(g_stepper.current_position);
    if (direction == STEPPER_DIR_CW) {
        current_steps++;
    } else {
        current_steps--;
    }
    g_stepper.current_position = stepper_steps_to_degrees(current_steps);
    
    return ESP_OK;
}

/**
 * @brief 设置线圈输出
 */
static void stepper_set_coil_outputs(uint8_t coil_a, uint8_t coil_b, uint8_t coil_c, uint8_t coil_d)
{
    gpio_set_level(GPIO_STEPPER_MOTOR_A, coil_a);
    gpio_set_level(GPIO_STEPPER_MOTOR_B, coil_b);
    gpio_set_level(GPIO_STEPPER_MOTOR_C, coil_c);
    gpio_set_level(GPIO_STEPPER_MOTOR_D, coil_d);
}

/**
 * @brief 关闭所有线圈
 */
static void stepper_disable_all_coils(void)
{
    gpio_set_level(GPIO_STEPPER_MOTOR_A, 0);
    gpio_set_level(GPIO_STEPPER_MOTOR_B, 0);
    gpio_set_level(GPIO_STEPPER_MOTOR_C, 0);
    gpio_set_level(GPIO_STEPPER_MOTOR_D, 0);
}

/**
 * @brief 角度转换为步数
 */
static int32_t stepper_degrees_to_steps(int16_t degrees)
{
    return (int32_t)degrees * STEPPER_STEPS_PER_REVOLUTION / 360;
}

/**
 * @brief 步数转换为角度
 */
static int16_t stepper_steps_to_degrees(int32_t steps)
{
    return (int16_t)(steps * 360 / STEPPER_STEPS_PER_REVOLUTION);
}

// 状态查询接口实现
stepper_state_t stepper_motor_get_state(void) { return g_stepper.state; }
int16_t stepper_motor_get_position(void) { return g_stepper.current_position; }
int16_t stepper_motor_get_target_position(void) { return g_stepper.target_position; }
bool stepper_motor_is_moving(void) { return g_stepper.state == STEPPER_STATE_MOVING; }
bool stepper_motor_is_homed(void) { return g_stepper.is_homed; } 