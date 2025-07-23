/**
 * @file st7789_driver.h
 * @brief ST7789 TFT显示屏驱动
 * @author SmartJet Team
 * @date 2024
 */

#ifndef ST7789_DRIVER_H
#define ST7789_DRIVER_H

#include "smartjet_common.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

// ST7789 显示屏参数
#define ST7789_WIDTH                240         // 屏幕宽度
#define ST7789_HEIGHT               320         // 屏幕高度
#define ST7789_ROTATION             0           // 默认旋转角度

// SPI配置参数
#define ST7789_SPI_HOST             SPI2_HOST   // SPI主机
#define ST7789_SPI_CLOCK_SPEED      26000000    // SPI时钟频率 26MHz
#define ST7789_SPI_QUEUE_SIZE       7           // SPI队列大小

// GPIO引脚定义 - 根据最新硬件设计更新
#define ST7789_PIN_MOSI             GPIO_NUM_47 // SPI MOSI
#define ST7789_PIN_SCLK             GPIO_NUM_21 // SPI SCLK  
#define ST7789_PIN_CS               GPIO_NUM_14 // SPI CS
#define ST7789_PIN_DC               GPIO_NUM_48 // 数据/命令控制
#define ST7789_PIN_RST              GPIO_NUM_45 // 复位
#define ST7789_PIN_BLK              GPIO_NUM_13 // 背光控制

// ST7789 命令定义
#define ST7789_CMD_NOP              0x00        // 空操作
#define ST7789_CMD_SWRESET          0x01        // 软件复位
#define ST7789_CMD_RDDID            0x04        // 读显示ID
#define ST7789_CMD_RDDST            0x09        // 读显示状态
#define ST7789_CMD_SLPIN            0x10        // 睡眠模式
#define ST7789_CMD_SLPOUT           0x11        // 退出睡眠
#define ST7789_CMD_PTLON            0x12        // 部分模式开启
#define ST7789_CMD_NORON            0x13        // 正常显示模式
#define ST7789_CMD_INVOFF           0x20        // 显示反转关闭
#define ST7789_CMD_INVON            0x21        // 显示反转开启
#define ST7789_CMD_DISPOFF          0x28        // 显示关闭
#define ST7789_CMD_DISPON           0x29        // 显示开启
#define ST7789_CMD_CASET            0x2A        // 列地址设置
#define ST7789_CMD_RASET            0x2B        // 行地址设置
#define ST7789_CMD_RAMWR            0x2C        // 内存写入
#define ST7789_CMD_RAMRD            0x2E        // 内存读取
#define ST7789_CMD_PTLAR            0x30        // 部分区域
#define ST7789_CMD_VSCRDEF          0x33        // 垂直滚动定义
#define ST7789_CMD_MADCTL           0x36        // 内存访问控制
#define ST7789_CMD_VSCRSADD         0x37        // 垂直滚动起始地址
#define ST7789_CMD_PIXFMT           0x3A        // 像素格式设置
#define ST7789_CMD_RAMWRC           0x3C        // 内存写入继续
#define ST7789_CMD_RAMRDC           0x3E        // 内存读取继续
#define ST7789_CMD_COLMOD           0x3A        // 颜色模式

// MADCTL寄存器位定义
#define ST7789_MADCTL_MY            0x80        // 行地址顺序
#define ST7789_MADCTL_MX            0x40        // 列地址顺序
#define ST7789_MADCTL_MV            0x20        // 行/列交换
#define ST7789_MADCTL_ML            0x10        // 垂直刷新顺序
#define ST7789_MADCTL_RGB           0x00        // RGB顺序
#define ST7789_MADCTL_BGR           0x08        // BGR顺序
#define ST7789_MADCTL_MH            0x04        // 水平刷新顺序

// 颜色定义 (RGB565)
#define ST7789_COLOR_BLACK          0x0000      // 黑色
#define ST7789_COLOR_WHITE          0xFFFF      // 白色
#define ST7789_COLOR_RED            0xF800      // 红色
#define ST7789_COLOR_GREEN          0x07E0      // 绿色
#define ST7789_COLOR_BLUE           0x001F      // 蓝色
#define ST7789_COLOR_YELLOW         0xFFE0      // 黄色
#define ST7789_COLOR_CYAN           0x07FF      // 青色
#define ST7789_COLOR_MAGENTA        0xF81F      // 洋红色
#define ST7789_COLOR_ORANGE         0xFD20      // 橙色
#define ST7789_COLOR_GRAY           0x8410      // 灰色
#define ST7789_COLOR_LIGHT_GRAY     0xC618      // 浅灰色
#define ST7789_COLOR_DARK_GRAY      0x4208      // 深灰色

// 字体大小
typedef enum {
    ST7789_FONT_SIZE_8 = 0,         // 8x8字体
    ST7789_FONT_SIZE_12,            // 12x12字体
    ST7789_FONT_SIZE_16,            // 16x16字体
    ST7789_FONT_SIZE_20,            // 20x20字体
    ST7789_FONT_SIZE_24             // 24x24字体
} st7789_font_size_t;

// 显示旋转方向
typedef enum {
    ST7789_ROTATION_0 = 0,          // 0度
    ST7789_ROTATION_90,             // 90度
    ST7789_ROTATION_180,            // 180度
    ST7789_ROTATION_270             // 270度
} st7789_rotation_t;

// 背光亮度级别
typedef enum {
    ST7789_BACKLIGHT_OFF = 0,       // 关闭
    ST7789_BACKLIGHT_LOW = 25,      // 低亮度
    ST7789_BACKLIGHT_MEDIUM = 50,   // 中等亮度
    ST7789_BACKLIGHT_HIGH = 75,     // 高亮度
    ST7789_BACKLIGHT_MAX = 100      // 最大亮度
} st7789_backlight_level_t;

// 显示控制器结构
typedef struct {
    spi_device_handle_t spi_device; // SPI设备句柄
    uint16_t width;                 // 当前显示宽度
    uint16_t height;                // 当前显示高度
    st7789_rotation_t rotation;     // 当前旋转角度
    uint8_t backlight_level;        // 背光亮度
    bool initialized;               // 初始化标志
    bool display_on;                // 显示开关状态
    
    // 显示缓冲区
    uint16_t* frame_buffer;         // 帧缓冲区
    size_t buffer_size;             // 缓冲区大小
    bool use_frame_buffer;          // 是否使用帧缓冲
    
    SemaphoreHandle_t spi_mutex;    // SPI互斥锁
} st7789_controller_t;

// 矩形结构
typedef struct {
    uint16_t x;                     // X坐标
    uint16_t y;                     // Y坐标
    uint16_t width;                 // 宽度
    uint16_t height;                // 高度
} st7789_rect_t;

// 点结构
typedef struct {
    uint16_t x;                     // X坐标
    uint16_t y;                     // Y坐标
} st7789_point_t;

// 函数声明
esp_err_t st7789_init(void);
void st7789_deinit(void);

// 基本控制
esp_err_t st7789_reset(void);
esp_err_t st7789_sleep(bool sleep);
esp_err_t st7789_display_on(bool on);
esp_err_t st7789_set_rotation(st7789_rotation_t rotation);
esp_err_t st7789_set_backlight(st7789_backlight_level_t level);

// SPI通信
esp_err_t st7789_write_command(uint8_t cmd);
esp_err_t st7789_write_data(const uint8_t* data, size_t length);
esp_err_t st7789_write_data_word(uint16_t data);

// 窗口设置
esp_err_t st7789_set_address_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

// 基本绘图
esp_err_t st7789_fill_screen(uint16_t color);
esp_err_t st7789_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
esp_err_t st7789_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
esp_err_t st7789_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t st7789_draw_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color);
esp_err_t st7789_fill_circle(uint16_t x, uint16_t y, uint16_t radius, uint16_t color);

// 文本绘制
esp_err_t st7789_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, 
                          uint16_t bg_color, st7789_font_size_t font_size);
esp_err_t st7789_draw_string(uint16_t x, uint16_t y, const char* str, uint16_t color, 
                            uint16_t bg_color, st7789_font_size_t font_size);
esp_err_t st7789_draw_string_centered(uint16_t y, const char* str, uint16_t color, 
                                     uint16_t bg_color, st7789_font_size_t font_size);

// 图像绘制
esp_err_t st7789_draw_bitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, 
                            const uint16_t* bitmap);
esp_err_t st7789_draw_icon(uint16_t x, uint16_t y, const uint16_t* icon_data, 
                          uint16_t icon_width, uint16_t icon_height);

// 进度条和UI元素
esp_err_t st7789_draw_progress_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, 
                                  uint8_t progress, uint16_t fg_color, uint16_t bg_color);
esp_err_t st7789_draw_battery_icon(uint16_t x, uint16_t y, uint8_t level, bool charging);
esp_err_t st7789_draw_signal_bars(uint16_t x, uint16_t y, uint8_t level);

// 帧缓冲区操作
esp_err_t st7789_enable_frame_buffer(bool enable);
esp_err_t st7789_flush_frame_buffer(void);
esp_err_t st7789_clear_frame_buffer(uint16_t color);

// 颜色转换
uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b);
void st7789_rgb565_to_rgb(uint16_t color, uint8_t* r, uint8_t* g, uint8_t* b);

// 工具函数
uint16_t st7789_get_width(void);
uint16_t st7789_get_height(void);
uint16_t st7789_get_string_width(const char* str, st7789_font_size_t font_size);
uint16_t st7789_get_font_height(st7789_font_size_t font_size);

// 测试和诊断
esp_err_t st7789_test_pattern(void);
esp_err_t st7789_test_colors(void);
void st7789_dump_info(void);

// 内部函数声明 - 预留用于后续开发
/*
static esp_err_t st7789_spi_init(void);
static esp_err_t st7789_gpio_init(void);
static esp_err_t st7789_display_init_sequence(void);
static void st7789_backlight_init(void);
static void st7789_backlight_set_duty(uint8_t duty_percent);
*/

#endif // ST7789_DRIVER_H 