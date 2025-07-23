/**
 * @file st7789_driver.c
 * @brief ST7789 TFT显示屏驱动实现
 * @author SmartJet Team
 * @date 2024
 */

#include "st7789_driver.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char* TAG = "ST7789";

// 前向声明
static esp_err_t st7789_gpio_init(void);
static esp_err_t st7789_spi_init(void);
static esp_err_t st7789_display_init_sequence(void);
static void st7789_backlight_init(void);
static void st7789_backlight_set_duty(uint8_t duty_percent);

// 全局显示控制器实例
static st7789_controller_t g_st7789_ctrl = {0};
static bool g_st7789_initialized = false;

// 简单8x8字体数据 (ASCII 32-126)
static const uint8_t font_8x8[][8] = {
    // 空格 (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 更多字符... (这里简化，实际应包含完整字符集)
    // A (65)
    {0x7E, 0x81, 0x81, 0x81, 0xFF, 0x81, 0x81, 0x00},
};

/**
 * @brief ST7789初始化
 */
esp_err_t st7789_init(void)
{
    if (g_st7789_initialized) {
        ESP_LOGW(TAG, "ST7789已初始化");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "初始化ST7789显示屏...");
    
    // 创建SPI互斥锁
    g_st7789_ctrl.spi_mutex = xSemaphoreCreateMutex();
    if (!g_st7789_ctrl.spi_mutex) {
        ESP_LOGE(TAG, "创建SPI互斥锁失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化GPIO
    esp_err_t ret = st7789_gpio_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化SPI
    ret = st7789_spi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI初始化失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化背光PWM
    st7789_backlight_init();
    
    // 硬件复位
    ret = st7789_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "显示屏复位失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 显示初始化序列
    ret = st7789_display_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "显示初始化序列失败: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // 初始化控制器参数
    g_st7789_ctrl.width = ST7789_WIDTH;
    g_st7789_ctrl.height = ST7789_HEIGHT;
    g_st7789_ctrl.rotation = ST7789_ROTATION_0;
    g_st7789_ctrl.backlight_level = ST7789_BACKLIGHT_HIGH;
    g_st7789_ctrl.initialized = true;
    g_st7789_ctrl.display_on = true;
    g_st7789_ctrl.use_frame_buffer = false;
    g_st7789_ctrl.frame_buffer = NULL;
    
    // 设置背光
    st7789_set_backlight(ST7789_BACKLIGHT_HIGH);
    
    // 清屏
    st7789_fill_screen(ST7789_COLOR_BLACK);
    
    g_st7789_initialized = true;
    ESP_LOGI(TAG, "ST7789初始化完成 (%dx%d)", g_st7789_ctrl.width, g_st7789_ctrl.height);
    
    return ESP_OK;
    
cleanup:
    st7789_deinit();
    return ret;
}

/**
 * @brief ST7789反初始化
 */
void st7789_deinit(void)
{
    if (!g_st7789_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "反初始化ST7789...");
    
    // 关闭显示
    st7789_display_on(false);
    st7789_set_backlight(ST7789_BACKLIGHT_OFF);
    
    // 删除SPI设备
    if (g_st7789_ctrl.spi_device) {
        spi_bus_remove_device(g_st7789_ctrl.spi_device);
        g_st7789_ctrl.spi_device = NULL;
    }
    
    // 删除帧缓冲区
    if (g_st7789_ctrl.frame_buffer) {
        free(g_st7789_ctrl.frame_buffer);
        g_st7789_ctrl.frame_buffer = NULL;
    }
    
    // 删除互斥锁
    if (g_st7789_ctrl.spi_mutex) {
        vSemaphoreDelete(g_st7789_ctrl.spi_mutex);
        g_st7789_ctrl.spi_mutex = NULL;
    }
    
    g_st7789_ctrl.initialized = false;
    g_st7789_initialized = false;
    
    ESP_LOGI(TAG, "ST7789反初始化完成");
}

/**
 * @brief 硬件复位
 */
esp_err_t st7789_reset(void)
{
    gpio_set_level(ST7789_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(ST7789_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    return ESP_OK;
}

/**
 * @brief 写命令
 */
esp_err_t st7789_write_command(uint8_t cmd)
{
    if (!g_st7789_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    spi_transaction_t trans = {0};
    trans.length = 8;
    trans.tx_buffer = &cmd;
    
    gpio_set_level(ST7789_PIN_DC, 0); // 命令模式
    
    esp_err_t ret = spi_device_transmit(g_st7789_ctrl.spi_device, &trans);
    return ret;
}

/**
 * @brief 写数据
 */
esp_err_t st7789_write_data(const uint8_t* data, size_t length)
{
    if (!g_st7789_initialized || !data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    spi_transaction_t trans = {0};
    trans.length = length * 8;
    trans.tx_buffer = data;
    
    gpio_set_level(ST7789_PIN_DC, 1); // 数据模式
    
    esp_err_t ret = spi_device_transmit(g_st7789_ctrl.spi_device, &trans);
    return ret;
}

/**
 * @brief 写16位数据
 */
esp_err_t st7789_write_data_word(uint16_t data)
{
    uint8_t buffer[2];
    buffer[0] = (data >> 8) & 0xFF;
    buffer[1] = data & 0xFF;
    
    return st7789_write_data(buffer, 2);
}

/**
 * @brief 设置地址窗口
 */
esp_err_t st7789_set_address_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    esp_err_t ret = ESP_OK;
    
    if (xSemaphoreTake(g_st7789_ctrl.spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 设置列地址
    uint8_t col_data[4] = {
        (x >> 8) & 0xFF, x & 0xFF,
        ((x + w - 1) >> 8) & 0xFF, (x + w - 1) & 0xFF
    };
    ret |= st7789_write_command(ST7789_CMD_CASET);
    ret |= st7789_write_data(col_data, 4);
    
    // 设置行地址
    uint8_t row_data[4] = {
        (y >> 8) & 0xFF, y & 0xFF,
        ((y + h - 1) >> 8) & 0xFF, (y + h - 1) & 0xFF
    };
    ret |= st7789_write_command(ST7789_CMD_RASET);
    ret |= st7789_write_data(row_data, 4);
    
    // 准备写入显存
    ret |= st7789_write_command(ST7789_CMD_RAMWR);
    
    xSemaphoreGive(g_st7789_ctrl.spi_mutex);
    return ret;
}

/**
 * @brief 填充屏幕
 */
esp_err_t st7789_fill_screen(uint16_t color)
{
    return st7789_fill_rect(0, 0, g_st7789_ctrl.width, g_st7789_ctrl.height, color);
}

/**
 * @brief 填充矩形
 */
esp_err_t st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!g_st7789_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (x >= g_st7789_ctrl.width || y >= g_st7789_ctrl.height) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 裁剪到屏幕边界
    if (x + w > g_st7789_ctrl.width) w = g_st7789_ctrl.width - x;
    if (y + h > g_st7789_ctrl.height) h = g_st7789_ctrl.height - y;
    
    esp_err_t ret = st7789_set_address_window(x, y, w, h);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 准备颜色数据
    uint8_t color_data[2] = {(color >> 8) & 0xFF, color & 0xFF};
    uint32_t pixel_count = w * h;
    
    // 分批发送数据以避免内存分配过大
    const uint32_t batch_size = 1024;
    
    gpio_set_level(ST7789_PIN_DC, 1); // 数据模式
    
    while (pixel_count > 0) {
        uint32_t current_batch = (pixel_count > batch_size) ? batch_size : pixel_count;
        
        for (uint32_t i = 0; i < current_batch; i++) {
            spi_transaction_t trans = {0};
            trans.length = 16;
            trans.tx_buffer = color_data;
            spi_device_transmit(g_st7789_ctrl.spi_device, &trans);
        }
        
        pixel_count -= current_batch;
    }
    
    return ESP_OK;
}

/**
 * @brief 绘制像素点
 */
esp_err_t st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= g_st7789_ctrl.width || y >= g_st7789_ctrl.height) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return st7789_fill_rect(x, y, 1, 1, color);
}

/**
 * @brief 绘制线条
 */
esp_err_t st7789_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    // Bresenham线条算法
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (true) {
        st7789_draw_pixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 绘制矩形边框
 */
esp_err_t st7789_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    st7789_draw_line(x, y, x + w - 1, y, color);           // 上边
    st7789_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color); // 下边
    st7789_draw_line(x, y, x, y + h - 1, color);           // 左边
    st7789_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color); // 右边
    
    return ESP_OK;
}

/**
 * @brief 绘制圆形
 */
esp_err_t st7789_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color)
{
    int f = 1 - radius;
    int ddF_x = 1;
    int ddF_y = -2 * radius;
    int dx = 0;
    int dy = radius;
    
    st7789_draw_pixel(x, y + radius, color);
    st7789_draw_pixel(x, y - radius, color);
    st7789_draw_pixel(x + radius, y, color);
    st7789_draw_pixel(x - radius, y, color);
    
    while (dx < dy) {
        if (f >= 0) {
            dy--;
            ddF_y += 2;
            f += ddF_y;
        }
        dx++;
        ddF_x += 2;
        f += ddF_x;
        
        st7789_draw_pixel(x + dx, y + dy, color);
        st7789_draw_pixel(x - dx, y + dy, color);
        st7789_draw_pixel(x + dx, y - dy, color);
        st7789_draw_pixel(x - dx, y - dy, color);
        st7789_draw_pixel(x + dy, y + dx, color);
        st7789_draw_pixel(x - dy, y + dx, color);
        st7789_draw_pixel(x + dy, y - dx, color);
        st7789_draw_pixel(x - dy, y - dx, color);
    }
    
    return ESP_OK;
}

/**
 * @brief 绘制字符
 */
esp_err_t st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, 
                          uint16_t bg_color, st7789_font_size_t font_size)
{
    if (c < 32 || c > 126) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t font_width = 8;
    uint8_t font_height = 8;
    
    // 简化实现，只支持8x8字体
    int char_index = c - 32;
    if (char_index >= sizeof(font_8x8) / sizeof(font_8x8[0])) {
        char_index = 0; // 默认为空格
    }
    
    for (int row = 0; row < font_height; row++) {
        uint8_t line = font_8x8[char_index][row];
        for (int col = 0; col < font_width; col++) {
            if (line & (0x80 >> col)) {
                st7789_draw_pixel(x + col, y + row, color);
            } else if (bg_color != color) {
                st7789_draw_pixel(x + col, y + row, bg_color);
            }
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 绘制字符串
 */
esp_err_t st7789_draw_string(uint16_t x, uint16_t y, const char* str, uint16_t color, 
                            uint16_t bg_color, st7789_font_size_t font_size)
{
    if (!str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t current_x = x;
    uint8_t font_width = 8; // 简化实现
    
    while (*str) {
        st7789_draw_char(current_x, y, *str, color, bg_color, font_size);
        current_x += font_width;
        str++;
        
        // 换行处理
        if (current_x >= g_st7789_ctrl.width) {
            break;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 绘制居中字符串
 */
esp_err_t st7789_draw_string_centered(uint16_t y, const char* str, uint16_t color, 
                                     uint16_t bg_color, st7789_font_size_t font_size)
{
    if (!str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t str_width = st7789_get_string_width(str, font_size);
    uint16_t x = (g_st7789_ctrl.width - str_width) / 2;
    
    return st7789_draw_string(x, y, str, color, bg_color, font_size);
}

/**
 * @brief 绘制进度条
 */
esp_err_t st7789_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, 
                                  uint8_t progress, uint16_t fg_color, uint16_t bg_color)
{
    if (progress > 100) {
        progress = 100;
    }
    
    // 绘制背景
    st7789_fill_rect(x, y, w, h, bg_color);
    
    // 绘制进度
    uint16_t progress_width = (w * progress) / 100;
    if (progress_width > 0) {
        st7789_fill_rect(x, y, progress_width, h, fg_color);
    }
    
    // 绘制边框
    st7789_draw_rect(x, y, w, h, ST7789_COLOR_WHITE);
    
    return ESP_OK;
}

/**
 * @brief 设置背光亮度
 */
esp_err_t st7789_set_backlight(st7789_backlight_level_t level)
{
    g_st7789_ctrl.backlight_level = level;
    st7789_backlight_set_duty(level);
    
    return ESP_OK;
}

/**
 * @brief 开启/关闭显示
 */
esp_err_t st7789_display_on(bool on)
{
    if (!g_st7789_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (on) {
        // 开启显示
        ret = st7789_write_command(ST7789_CMD_DISPON);
        ESP_LOGI(TAG, "显示已开启");
    } else {
        // 关闭显示
        ret = st7789_write_command(ST7789_CMD_DISPOFF);
        ESP_LOGI(TAG, "显示已关闭");
    }
    
    return ret;
}

/**
 * @brief 初始化GPIO
 */
static esp_err_t st7789_gpio_init(void)
{
    gpio_config_t io_conf = {0};
    
    // 配置DC和RST引脚
    io_conf.pin_bit_mask = (1ULL << ST7789_PIN_DC) | (1ULL << ST7789_PIN_RST);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 设置初始电平
    gpio_set_level(ST7789_PIN_DC, 0);
    gpio_set_level(ST7789_PIN_RST, 1);
    
    return ESP_OK;
}

/**
 * @brief 初始化SPI
 */
static esp_err_t st7789_spi_init(void)
{
    spi_bus_config_t bus_cfg = {
        .miso_io_num = -1,                    // 不使用MISO
        .mosi_io_num = ST7789_PIN_MOSI,
        .sclk_io_num = ST7789_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = ST7789_WIDTH * ST7789_HEIGHT * 2
    };
    
    esp_err_t ret = spi_bus_initialize(ST7789_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        return ret;
    }
    
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = ST7789_SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = ST7789_PIN_CS,
        .queue_size = ST7789_SPI_QUEUE_SIZE,
    };
    
    ret = spi_bus_add_device(ST7789_SPI_HOST, &dev_cfg, &g_st7789_ctrl.spi_device);
    return ret;
}

/**
 * @brief 显示初始化序列
 */
static esp_err_t st7789_display_init_sequence(void)
{
    esp_err_t ret = ESP_OK;
    
    // 退出睡眠模式
    ret |= st7789_write_command(ST7789_CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // 设置颜色模式为RGB565
    ret |= st7789_write_command(ST7789_CMD_COLMOD);
    uint8_t color_mode = 0x55; // 16位/像素
    ret |= st7789_write_data(&color_mode, 1);
    
    // 设置内存访问控制
    ret |= st7789_write_command(ST7789_CMD_MADCTL);
    uint8_t madctl = ST7789_MADCTL_RGB;
    ret |= st7789_write_data(&madctl, 1);
    
    // 正常显示模式
    ret |= st7789_write_command(ST7789_CMD_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 开启显示
    ret |= st7789_write_command(ST7789_CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(120));
    
    return ret;
}

/**
 * @brief 初始化背光PWM
 */
static void st7789_backlight_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);
    
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = ST7789_PIN_BLK,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);
}

/**
 * @brief 设置背光PWM占空比
 */
static void st7789_backlight_set_duty(uint8_t duty_percent)
{
    uint32_t duty = (duty_percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/**
 * @brief RGB转RGB565
 */
uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief 获取字符串宽度
 */
uint16_t st7789_get_string_width(const char* str, st7789_font_size_t font_size)
{
    if (!str) return 0;
    
    uint8_t font_width = 8; // 简化实现
    return strlen(str) * font_width;
}

/**
 * @brief 获取字体高度
 */
uint16_t st7789_get_font_height(st7789_font_size_t font_size)
{
    return 8; // 简化实现
}

/**
 * @brief 测试显示模式
 */
esp_err_t st7789_test_pattern(void)
{
    ESP_LOGI(TAG, "显示测试图案");
    
    // 填充不同颜色的区域
    st7789_fill_rect(0, 0, 80, 80, ST7789_COLOR_RED);
    st7789_fill_rect(80, 0, 80, 80, ST7789_COLOR_GREEN);
    st7789_fill_rect(160, 0, 80, 80, ST7789_COLOR_BLUE);
    
    st7789_fill_rect(0, 80, 80, 80, ST7789_COLOR_YELLOW);
    st7789_fill_rect(80, 80, 80, 80, ST7789_COLOR_CYAN);
    st7789_fill_rect(160, 80, 80, 80, ST7789_COLOR_MAGENTA);
    
    st7789_fill_rect(0, 160, 240, 160, ST7789_COLOR_WHITE);
    
    // 绘制测试文本
    st7789_draw_string_centered(200, "SmartJet v2.0", ST7789_COLOR_BLACK, 
                               ST7789_COLOR_WHITE, ST7789_FONT_SIZE_16);
    st7789_draw_string_centered(220, "Display Test", ST7789_COLOR_BLACK, 
                               ST7789_COLOR_WHITE, ST7789_FONT_SIZE_16);
    
    return ESP_OK;
}

// 状态查询接口实现
uint16_t st7789_get_width(void) { return g_st7789_ctrl.width; }
uint16_t st7789_get_height(void) { return g_st7789_ctrl.height; } 