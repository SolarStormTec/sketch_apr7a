/**
 * @file engine_ctrl.c
 * @brief 发动机控制模块实现
 * @author SmartJet Team
 * @date 2024
 */

#include "engine_ctrl.h"
#include "stepper_motor.h"
#include "sys_manager.h"
#include "event_system.h"
#include "config_mgr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char* TAG = "ENGINE_CTRL";

// 全局发动机控制器实例
static engine_controller_t g_engine_ctrl = {0};
static bool g_engine_initialized = false;

/**
 * @brief 发动机控制器初始化
 */
esp_err_t engine_controller_init(void)
{
    esp_err_t ret = ESP_OK;
    
    if (g_engine_initialized) {
        ESP_LOGW(TAG, "发动机控制器已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化发动机控制器...");
    
    // 初始化互斥锁
    g_engine_ctrl.ctrl_mutex = xSemaphoreCreateMutex();
    if (!g_engine_ctrl.ctrl_mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化状态
    g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
    g_engine_ctrl.start_sequence = ENGINE_START_IDLE;
    g_engine_ctrl.stop_sequence = ENGINE_STOP_IDLE;
    g_engine_ctrl.last_trigger = ENGINE_TRIGGER_NONE;
    
    // 配置GPIO
    gpio_config_t io_conf = {0};
    
    // 启动继电器输出
    io_conf.pin_bit_mask = (1ULL << GPIO_ENGINE_STARTER_RELAY);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level(GPIO_ENGINE_STARTER_RELAY, 0);
    
    // 熄火输出
    io_conf.pin_bit_mask = (1ULL << GPIO_ENGINE_KILL_OUTPUT);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 0);
    
    // 燃油电磁阀输出
    io_conf.pin_bit_mask = (1ULL << GPIO_FUEL_SOLENOID);
    gpio_config(&io_conf);
    gpio_set_level(GPIO_FUEL_SOLENOID, 0);
    
    // 初始化步进电机
    ret = stepper_motor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "步进电机初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 创建序列控制定时器
    esp_timer_create_args_t timer_args = {
        .callback = engine_start_sequence_handler,
        .arg = NULL,
        .name = "engine_seq_timer"
    };
    ret = esp_timer_create(&timer_args, &g_engine_ctrl.sequence_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建序列定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 创建运行时间统计定时器
    timer_args.callback = engine_runtime_update_handler;
    timer_args.name = "engine_runtime_timer";
    ret = esp_timer_create(&timer_args, &g_engine_ctrl.runtime_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建运行时间定时器失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 从NVS加载配置
    engine_load_config_from_nvs();
    
    g_engine_initialized = true;
    ESP_LOGI(TAG, "发动机控制器初始化完成");
    
    return ESP_OK;
    
cleanup:
    engine_controller_deinit();
    return ret;
}

/**
 * @brief 发动机控制器反初始化
 */
void engine_controller_deinit(void)
{
    if (!g_engine_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化发动机控制器...");
    
    // 停止定时器
    if (g_engine_ctrl.sequence_timer) {
        esp_timer_stop(g_engine_ctrl.sequence_timer);
        esp_timer_delete(g_engine_ctrl.sequence_timer);
        g_engine_ctrl.sequence_timer = NULL;
    }
    
    if (g_engine_ctrl.runtime_timer) {
        esp_timer_stop(g_engine_ctrl.runtime_timer);
        esp_timer_delete(g_engine_ctrl.runtime_timer);
        g_engine_ctrl.runtime_timer = NULL;
    }
    
    // 关闭所有输出
    gpio_set_level(GPIO_ENGINE_STARTER_RELAY, 0);
    gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 0);
    gpio_set_level(GPIO_FUEL_SOLENOID, 0);
    
    // 反初始化步进电机
    stepper_motor_deinit();
    
    // 删除互斥锁
    if (g_engine_ctrl.ctrl_mutex) {
        vSemaphoreDelete(g_engine_ctrl.ctrl_mutex);
        g_engine_ctrl.ctrl_mutex = NULL;
    }
    
    g_engine_initialized = false;
    ESP_LOGI(TAG, "发动机控制器反初始化完成");
}

/**
 * @brief 启动发动机
 */
esp_err_t engine_start(engine_trigger_source_t trigger)
{
    if (!g_engine_initialized) {
        ESP_LOGE(TAG, "发动机控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_engine_ctrl.ctrl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取控制锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    // 检查当前状态
    if (g_engine_ctrl.current_state == ENGINE_STATE_RUNNING) {
        ESP_LOGW(TAG, "发动机已在运行中");
        xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
        return ESP_OK;
    }
    
    if (g_engine_ctrl.current_state == ENGINE_STATE_STARTING) {
        ESP_LOGW(TAG, "发动机正在启动中");
        xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
        return ESP_OK;
    }
    
    // 安全检查
    engine_safety_check_t safety = engine_safety_check();
    if (!safety.can_start) {
        ESP_LOGE(TAG, "安全检查失败，无法启动发动机");
        
        event_message_t event = {
            .type = EVENT_ENGINE_START_REQUEST,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
        
        xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "开始启动发动机 (触发源: %d)", trigger);
    
    // 更新状态
    g_engine_ctrl.current_state = ENGINE_STATE_STARTING;
    g_engine_ctrl.start_sequence = ENGINE_START_CHOKE_RESET;
    g_engine_ctrl.last_trigger = trigger;
    g_engine_ctrl.start_requested = true;
    g_engine_ctrl.start_attempts = 0;
    
    // 启动序列定时器
    esp_err_t ret = esp_timer_start_once(g_engine_ctrl.sequence_timer, 100 * 1000); // 100ms后开始
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动序列定时器失败: %s", esp_err_to_name(ret));
        g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
        g_engine_ctrl.start_sequence = ENGINE_START_IDLE;
        xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
        return ret;
    }
    
    // 发送启动事件
    event_message_t event = {
        .type = EVENT_ENGINE_START_REQUEST,
        .data = NULL,
        .data_len = 0,
        .timestamp = esp_timer_get_time() / 1000
    };
    event_system_post(&event);
    
    xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
    return ESP_OK;
}

/**
 * @brief 停止发动机
 */
esp_err_t engine_stop(bool emergency)
{
    if (!g_engine_initialized) {
        ESP_LOGE(TAG, "发动机控制器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_engine_ctrl.ctrl_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "获取控制锁超时");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "停止发动机 (紧急停止: %s)", emergency ? "是" : "否");
    
    // 如果已经停止，直接返回
    if (g_engine_ctrl.current_state == ENGINE_STATE_STOPPED) {
        ESP_LOGW(TAG, "发动机已停止");
        xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
        return ESP_OK;
    }
    
    // 停止任何正在进行的启动序列
    esp_timer_stop(g_engine_ctrl.sequence_timer);
    esp_timer_stop(g_engine_ctrl.runtime_timer);
    
    // 立即关闭启动继电器
    gpio_set_level(GPIO_ENGINE_STARTER_RELAY, 0);
    
    if (emergency) {
        // 紧急停止：立即熄火
        g_engine_ctrl.emergency_stop = true;
        g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
        g_engine_ctrl.stop_sequence = ENGINE_STOP_IDLE;
        
        // 立即熄火和关闭燃油
        gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 1);
        gpio_set_level(GPIO_FUEL_SOLENOID, 0);
        vTaskDelay(pdMS_TO_TICKS(ENGINE_STOP_KILL_DURATION_MS));
        gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 0);
        
        // 风门复位
        stepper_motor_reset_position();
        
        ESP_LOGI(TAG, "紧急停止完成");
    } else {
        // 正常停止序列
        g_engine_ctrl.current_state = ENGINE_STATE_STOPPING;
        g_engine_ctrl.stop_sequence = ENGINE_STOP_CHOKE_RESET;
        g_engine_ctrl.stop_requested = true;
        
        // 启动停止序列
        esp_timer_start_once(g_engine_ctrl.sequence_timer, 100 * 1000); // 100ms后开始
    }
    
    // 发送停止事件
    event_message_t event = {
        .type = EVENT_ENGINE_STOP_REQUEST,
        .data = NULL,
        .data_len = 0,
        .timestamp = esp_timer_get_time() / 1000
    };
    event_system_post(&event);
    
    xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
    return ESP_OK;
}

/**
 * @brief 紧急停止发动机
 */
esp_err_t engine_emergency_stop(void)
{
    return engine_stop(true);
}

/**
 * @brief 启动序列处理函数
 */
static void engine_start_sequence_handler(void* arg)
{
    if (xSemaphoreTake(g_engine_ctrl.ctrl_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    if (g_engine_ctrl.current_state == ENGINE_STATE_STARTING) {
        engine_execute_start_step();
    } else if (g_engine_ctrl.current_state == ENGINE_STATE_STOPPING) {
        engine_execute_stop_step();
    }
    
    xSemaphoreGive(g_engine_ctrl.ctrl_mutex);
}

/**
 * @brief 执行启动步骤
 */
static esp_err_t engine_execute_start_step(void)
{
    esp_err_t ret = ESP_OK;
    uint32_t next_delay_ms = 0;
    
    switch (g_engine_ctrl.start_sequence) {
        case ENGINE_START_CHOKE_RESET:
            ESP_LOGI(TAG, "步骤1: 风门复位");
            stepper_motor_reset_position();
            g_engine_ctrl.start_sequence = ENGINE_START_CHOKE_CLOSE;
            next_delay_ms = 2000; // 等待复位完成
            break;
            
        case ENGINE_START_CHOKE_CLOSE:
            ESP_LOGI(TAG, "步骤2: 风门关闭");
            stepper_motor_move_to_position(0); // 关闭位置
            g_engine_ctrl.start_sequence = ENGINE_START_WAKE_PULSE;
            next_delay_ms = 1000; // 等待关闭完成
            break;
            
        case ENGINE_START_WAKE_PULSE:
            ESP_LOGI(TAG, "步骤3: 启动唤醒脉冲");
            gpio_set_level(GPIO_ENGINE_STARTER_RELAY, 1);
            g_engine_ctrl.start_sequence = ENGINE_START_CRANKING;
            next_delay_ms = ENGINE_START_WAKE_PULSE_MS;
            break;
            
        case ENGINE_START_CRANKING:
            ESP_LOGI(TAG, "步骤4: 启动转动");
            // 继续保持启动继电器
            g_engine_ctrl.start_sequence = ENGINE_START_WAIT_RPM;
            next_delay_ms = ENGINE_START_CRANK_MS;
            break;
            
        case ENGINE_START_WAIT_RPM:
            ESP_LOGI(TAG, "步骤5: 等待RPM检测");
            // 关闭启动继电器
            gpio_set_level(GPIO_ENGINE_STARTER_RELAY, 0);
            
            // 检查是否已经启动
            if (g_smartjet.sensors.rpm > g_smartjet.config.engine_on_rpm_threshold) {
                ESP_LOGI(TAG, "检测到发动机启动，RPM: %u", g_smartjet.sensors.rpm);
                g_engine_ctrl.start_sequence = ENGINE_START_CHOKE_OPEN;
                next_delay_ms = 1000; // 1秒后开启风门
            } else {
                g_engine_ctrl.start_attempts++;
                if (g_engine_ctrl.start_attempts >= ENGINE_MAX_START_ATTEMPTS) {
                    ESP_LOGE(TAG, "启动失败，已达到最大尝试次数");
                    g_engine_ctrl.start_sequence = ENGINE_START_FAILED;
                    next_delay_ms = 100;
                } else {
                    ESP_LOGW(TAG, "启动尝试 %d 失败，准备重试", g_engine_ctrl.start_attempts);
                    g_engine_ctrl.start_sequence = ENGINE_START_RETRY_WAIT;
                    next_delay_ms = ENGINE_START_RETRY_DELAY_MS;
                }
            }
            break;
            
        case ENGINE_START_CHOKE_OPEN:
            ESP_LOGI(TAG, "步骤6: 风门开启");
            stepper_motor_move_to_position(ENGINE_CHOKE_OPEN_STEPS);
            gpio_set_level(GPIO_FUEL_SOLENOID, 1); // 开启燃油
            g_engine_ctrl.start_sequence = ENGINE_START_SUCCESS;
            next_delay_ms = 2000; // 等待风门开启完成
            break;
            
        case ENGINE_START_SUCCESS:
            ESP_LOGI(TAG, "发动机启动成功");
            g_engine_ctrl.current_state = ENGINE_STATE_RUNNING;
            g_engine_ctrl.start_sequence = ENGINE_START_IDLE;
            g_engine_ctrl.start_requested = false;
            g_engine_ctrl.total_start_count++;
            g_engine_ctrl.engine_start_time = esp_timer_get_time();
            
            // 启动运行时间统计定时器
            esp_timer_start_periodic(g_engine_ctrl.runtime_timer, 1000000); // 每秒更新
            
            // 发送启动成功事件
            event_message_t event = {
                .type = EVENT_ENGINE_STARTED,
                .data = NULL,
                .data_len = 0,
                .timestamp = esp_timer_get_time() / 1000
            };
            event_system_post(&event);
            return ESP_OK; // 不需要再次启动定时器
            
        case ENGINE_START_RETRY_WAIT:
            ESP_LOGI(TAG, "重试等待完成，开始下一次尝试");
            g_engine_ctrl.start_sequence = ENGINE_START_CHOKE_RESET;
            next_delay_ms = 100;
            break;
            
        case ENGINE_START_FAILED:
            ESP_LOGE(TAG, "发动机启动失败");
            g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
            g_engine_ctrl.start_sequence = ENGINE_START_IDLE;
            g_engine_ctrl.start_requested = false;
            g_engine_ctrl.failed_start_count++;
            
            // 风门复位
            stepper_motor_reset_position();
            
            // 发送启动失败事件
            event_message_t failure_event = {
                .type = EVENT_ENGINE_START_REQUEST,
                .data = NULL,
                .data_len = 0,
                .timestamp = esp_timer_get_time() / 1000
            };
            event_system_post(&failure_event);
            return ESP_OK; // 不需要再次启动定时器
            
        default:
            ESP_LOGE(TAG, "未知启动序列状态: %d", g_engine_ctrl.start_sequence);
            return ESP_FAIL;
    }
    
    // 启动下一步定时器
    if (next_delay_ms > 0) {
        ret = esp_timer_start_once(g_engine_ctrl.sequence_timer, next_delay_ms * 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "启动下一步定时器失败: %s", esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief 执行停止步骤
 */
static esp_err_t engine_execute_stop_step(void)
{
    esp_err_t ret = ESP_OK;
    uint32_t next_delay_ms = 0;
    
    switch (g_engine_ctrl.stop_sequence) {
        case ENGINE_STOP_CHOKE_RESET:
            ESP_LOGI(TAG, "停止步骤1: 风门复位");
            stepper_motor_reset_position();
            g_engine_ctrl.stop_sequence = ENGINE_STOP_KILL_IGNITION;
            next_delay_ms = 1000;
            break;
            
        case ENGINE_STOP_KILL_IGNITION:
            ESP_LOGI(TAG, "停止步骤2: 熄火");
            gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 1);
            g_engine_ctrl.stop_sequence = ENGINE_STOP_FUEL_OFF;
            next_delay_ms = ENGINE_STOP_KILL_DURATION_MS;
            break;
            
        case ENGINE_STOP_FUEL_OFF:
            ESP_LOGI(TAG, "停止步骤3: 关闭燃油");
            gpio_set_level(GPIO_ENGINE_KILL_OUTPUT, 0);
            gpio_set_level(GPIO_FUEL_SOLENOID, 0);
            g_engine_ctrl.stop_sequence = ENGINE_STOP_SUCCESS;
            next_delay_ms = 500;
            break;
            
        case ENGINE_STOP_SUCCESS:
            ESP_LOGI(TAG, "发动机停止成功");
            g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
            g_engine_ctrl.stop_sequence = ENGINE_STOP_IDLE;
            g_engine_ctrl.stop_requested = false;
            
            // 停止运行时间统计
            esp_timer_stop(g_engine_ctrl.runtime_timer);
            
            // 发送停止成功事件
            event_message_t event = {
                .type = EVENT_ENGINE_STOPPED,
                .data = NULL,
                .data_len = 0,
                .timestamp = esp_timer_get_time() / 1000
            };
            event_system_post(&event);
            return ESP_OK;
            
        default:
            ESP_LOGE(TAG, "未知停止序列状态: %d", g_engine_ctrl.stop_sequence);
            return ESP_FAIL;
    }
    
    // 启动下一步定时器
    if (next_delay_ms > 0) {
        ret = esp_timer_start_once(g_engine_ctrl.sequence_timer, next_delay_ms * 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "启动下一步定时器失败: %s", esp_err_to_name(ret));
        }
    }
    
    return ret;
}

/**
 * @brief 运行时间更新处理函数
 */
static void engine_runtime_update_handler(void* arg)
{
    if (g_engine_ctrl.current_state == ENGINE_STATE_RUNNING) {
        g_engine_ctrl.current_run_duration = 
            (esp_timer_get_time() - g_engine_ctrl.engine_start_time) / 1000000;
        
        // 每分钟更新总运行小时数
        if (g_engine_ctrl.current_run_duration % 60 == 0) {
            g_engine_ctrl.total_run_hours = 
                (g_engine_ctrl.total_run_hours * 3600 + g_engine_ctrl.current_run_duration) / 3600;
        }
    }
}

/**
 * @brief 安全检查
 */
engine_safety_check_t engine_safety_check(void)
{
    engine_safety_check_t result = {0};
    
    // 检查机油压力
    result.oil_pressure_ok = !g_smartjet.sensors.oil_pressure_alarm;
    
    // 检查电池电压
    result.battery_voltage_ok = (g_smartjet.battery.voltage >= g_smartjet.config.low_voltage_warning_threshold &&
                                g_smartjet.battery.voltage <= g_smartjet.config.high_voltage_warning_threshold);
    
    // 检查温度
    result.temperature_ok = (g_smartjet.sensors.board_temperature >= -10.0f && 
                            g_smartjet.sensors.board_temperature <= 60.0f);
    
    // 检查转速
    result.rpm_normal = (g_smartjet.sensors.rpm <= g_smartjet.config.rpm_alarm_threshold);
    
    // 综合判断
    result.can_start = result.oil_pressure_ok && result.battery_voltage_ok && result.temperature_ok;
    result.can_continue_run = result.can_start && result.rpm_normal;
    
    return result;
}

/**
 * @brief 处理RPM变化
 */
void engine_handle_rpm_change(uint16_t rpm)
{
    // 检测手拉启动
    if (g_engine_ctrl.current_state == ENGINE_STATE_STOPPED && 
        rpm > g_smartjet.config.engine_on_rpm_threshold) {
        ESP_LOGI(TAG, "检测到手拉启动，RPM: %d", rpm);
        g_engine_ctrl.manual_pull_detected = true;
        g_engine_ctrl.current_state = ENGINE_STATE_RUNNING;
        g_engine_ctrl.last_trigger = ENGINE_TRIGGER_MANUAL_PULL;
        g_engine_ctrl.engine_start_time = esp_timer_get_time();
        
        // 启动运行时间统计
        esp_timer_start_periodic(g_engine_ctrl.runtime_timer, 1000000);
        
        // 发送手拉启动事件
        event_message_t event = {
            .type = EVENT_ENGINE_STARTED,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
    }
    
    // 检测发动机停止
    if (g_engine_ctrl.current_state == ENGINE_STATE_RUNNING && 
        rpm < g_smartjet.config.engine_on_rpm_threshold) {
        ESP_LOGI(TAG, "检测到发动机停止，RPM: %d", rpm);
        g_engine_ctrl.current_state = ENGINE_STATE_STOPPED;
        
        // 停止运行时间统计
        esp_timer_stop(g_engine_ctrl.runtime_timer);
        
        // 发送停止事件
        event_message_t event = {
            .type = EVENT_ENGINE_STOPPED,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
    }
    
    // 检测转速过高
    if (rpm > g_smartjet.config.rpm_alarm_threshold) {
        ESP_LOGW(TAG, "发动机转速过高: %d", rpm);
        event_message_t event = {
            .type = EVENT_ENGINE_STARTED,
            .data = NULL,
            .data_len = 0,
            .timestamp = esp_timer_get_time() / 1000
        };
        event_system_post(&event);
    }
}

/**
 * @brief 从NVS加载配置
 */
esp_err_t engine_load_config_from_nvs(void)
{
    // 这里应该调用配置管理器来加载发动机相关配置
    // 暂时使用默认值
    return ESP_OK;
}

/**
 * @brief 保存配置到NVS
 */
esp_err_t engine_save_config_to_nvs(void)
{
    // 这里应该调用配置管理器来保存发动机相关配置
    return ESP_OK;
}

// 状态查询接口实现
engine_state_t engine_get_state(void) { return g_engine_ctrl.current_state; }
engine_start_sequence_t engine_get_start_sequence(void) { return g_engine_ctrl.start_sequence; }
bool engine_is_running(void) { return g_engine_ctrl.current_state == ENGINE_STATE_RUNNING; }
bool engine_is_starting(void) { return g_engine_ctrl.current_state == ENGINE_STATE_STARTING; }
bool engine_is_stopping(void) { return g_engine_ctrl.current_state == ENGINE_STATE_STOPPING; }

// 统计信息接口实现
uint32_t engine_get_total_start_count(void) { return g_engine_ctrl.total_start_count; }
uint32_t engine_get_failed_start_count(void) { return g_engine_ctrl.failed_start_count; }
uint32_t engine_get_total_run_hours(void) { return g_engine_ctrl.total_run_hours; }
uint32_t engine_get_current_run_duration(void) { return g_engine_ctrl.current_run_duration; } 