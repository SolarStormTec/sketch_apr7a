# SmartJet v2.0 - 未实现功能清单

## 📋 概述

本文档列出了SmartJet v2.0项目中已声明但尚未实现的功能，按模块分类整理，便于后续开发。

## 🌐 WiFi管理模块 (main/cloud/wifi_manager.h)

### 高优先级功能
- [ ] **wifi_event_handler()** - WiFi事件处理器
  - 功能：处理WiFi连接/断开等事件
  - 优先级：高
  - 预计工作量：2-3小时

- [ ] **ip_event_handler()** - IP事件处理器
  - 功能：处理IP地址获取/释放事件
  - 优先级：高
  - 预计工作量：1-2小时

### 中等优先级功能
- [ ] **wifi_reconnect_timer_callback()** - WiFi重连定时器回调
  - 功能：自动重连机制的定时器处理
  - 优先级：中
  - 预计工作量：2小时

- [ ] **wifi_config_timeout_callback()** - 配置超时回调
  - 功能：WiFi配置过程的超时处理
  - 优先级：中
  - 预计工作量：1小时

- [ ] **wifi_manager_apply_sta_config()** - 应用STA配置
  - 功能：将配置应用到WiFi STA模式
  - 优先级：中
  - 预计工作量：1-2小时

### 已实现功能 ✅
- [x] **wifi_rssi_to_level()** - RSSI转信号强度等级
- [x] **wifi_manager_update_signal_level()** - 更新信号强度

## 🔋 电池管理模块 (main/battery/bq40z50.h)

### 中等优先级功能
- [ ] **bq40z50_i2c_init()** - I2C初始化
  - 功能：初始化BQ40Z50芯片的I2C通信
  - 优先级：中
  - 预计工作量：2-3小时

- [ ] **bq40z50_i2c_deinit()** - I2C反初始化
  - 功能：清理I2C资源
  - 优先级：中
  - 预计工作量：1小时

- [ ] **bq40z50_parse_battery_status()** - 解析电池状态
  - 功能：解析电池状态寄存器数据
  - 优先级：中
  - 预计工作量：2-3小时

- [ ] **bq40z50_parse_battery_mode()** - 解析电池模式
  - 功能：解析电池模式寄存器数据
  - 优先级：中
  - 预计工作量：2-3小时

### 已实现功能 ✅
- [x] **bq40z50_validate_data()** - 数据验证
- [x] **bq40z50_temperature_k_to_c()** - 温度转换

## 📱 显示驱动模块 (main/display/st7789_driver.h)

### 高优先级功能
- [ ] **st7789_spi_init()** - SPI初始化
  - 功能：初始化ST7789显示屏的SPI通信
  - 优先级：高
  - 预计工作量：3-4小时

- [ ] **st7789_gpio_init()** - GPIO初始化
  - 功能：初始化显示屏控制引脚
  - 优先级：高
  - 预计工作量：2小时

- [ ] **st7789_display_init_sequence()** - 显示初始化序列
  - 功能：发送显示屏初始化命令序列
  - 优先级：高
  - 预计工作量：4-5小时

### 中等优先级功能
- [ ] **st7789_backlight_init()** - 背光初始化
  - 功能：初始化PWM背光控制
  - 优先级：中
  - 预计工作量：2小时

- [ ] **st7789_backlight_set_duty()** - 设置背光占空比
  - 功能：动态调节背光亮度
  - 优先级：中
  - 预计工作量：1小时

## 📊 传感器管理模块 (main/sensors/sensor_mgr.h)

### 高优先级功能
- [ ] **sensor_sample_timer_callback()** - 传感器采样定时器
  - 功能：定时触发传感器数据采样
  - 优先级：高
  - 预计工作量：2-3小时

- [ ] **sensor_sample_analog_sensors()** - 模拟传感器采样
  - 功能：采样所有模拟传感器（ADC）
  - 优先级：高
  - 预计工作量：3-4小时

- [ ] **sensor_sample_digital_sensors()** - 数字传感器采样
  - 功能：采样所有数字传感器（GPIO）
  - 优先级：高
  - 预计工作量：2-3小时

### 中等优先级功能
- [ ] **rpm_gpio_isr_handler()** - RPM中断处理
  - 功能：处理发动机转速脉冲中断
  - 优先级：中
  - 预计工作量：3-4小时

- [ ] **freq_gpio_isr_handler()** - 频率中断处理
  - 功能：处理频率测量脉冲中断
  - 优先级：中
  - 预计工作量：2-3小时

- [ ] **sensor_update_rpm_calculation()** - 更新RPM计算
  - 功能：根据脉冲计算发动机转速
  - 优先级：中
  - 预计工作量：2-3小时

- [ ] **sensor_update_frequency_calculation()** - 更新频率计算
  - 功能：计算发电机输出频率
  - 优先级：中
  - 预计工作量：2-3小时

### 低优先级功能
- [ ] **sensor_ntc_resistance_to_temperature()** - NTC温度转换
  - 功能：将NTC电阻值转换为温度
  - 优先级：低
  - 预计工作量：2-3小时

### 已实现功能 ✅
- [x] **sensor_filter_update()** - 传感器滤波器

## ⚙️ 引擎控制模块 (main/engine/engine_ctrl.h)

### 低优先级功能
- [ ] **engine_stop_sequence_handler()** - 引擎停止序列处理
  - 功能：处理引擎停止的完整序列
  - 优先级：低
  - 预计工作量：3-4小时
  - 备注：当前已有基础停止功能

### 已实现功能 ✅
- [x] **engine_start_sequence_handler()** - 启动序列处理
- [x] **engine_runtime_update_handler()** - 运行时更新处理
- [x] **engine_execute_start_step()** - 执行启动步骤
- [x] **engine_execute_stop_step()** - 执行停止步骤

## 🔧 步进电机模块 (main/engine/stepper_motor.h)

### 高优先级功能
- [ ] **stepper_timer_callback()** - 步进电机定时器回调
  - 功能：定时控制步进电机步进
  - 优先级：高
  - 预计工作量：3-4小时

- [ ] **stepper_single_step()** - 单步控制
  - 功能：控制步进电机单步运动
  - 优先级：高
  - 预计工作量：2-3小时

- [ ] **stepper_set_coil_outputs()** - 设置线圈输出
  - 功能：控制4个线圈的输出状态
  - 优先级：高
  - 预计工作量：2小时

### 中等优先级功能
- [ ] **stepper_disable_all_coils()** - 禁用所有线圈
  - 功能：断电保护，停止电机
  - 优先级：中
  - 预计工作量：1小时

- [ ] **stepper_degrees_to_steps()** - 角度转步数
  - 功能：将角度转换为步进数
  - 优先级：中
  - 预计工作量：1小时

- [ ] **stepper_steps_to_degrees()** - 步数转角度
  - 功能：将步进数转换为角度
  - 优先级：中
  - 预计工作量：1小时

## 📈 开发优先级建议

### 第一阶段 (核心功能)
1. WiFi事件处理器 (wifi_event_handler, ip_event_handler)
2. 显示屏SPI初始化 (st7789_spi_init, st7789_gpio_init)
3. 传感器采样功能 (sensor_sample_analog/digital_sensors)
4. 步进电机基础控制 (stepper_single_step, stepper_set_coil_outputs)

### 第二阶段 (扩展功能)
1. 传感器定时采样 (sensor_sample_timer_callback)
2. 电池管理I2C通信 (bq40z50_i2c_init)
3. 显示屏初始化序列 (st7789_display_init_sequence)
4. 步进电机定时控制 (stepper_timer_callback)

### 第三阶段 (优化功能)
1. WiFi自动重连 (wifi_reconnect_timer_callback)
2. 传感器中断处理 (rpm_gpio_isr_handler, freq_gpio_isr_handler)
3. 背光控制 (st7789_backlight_init/set_duty)
4. 高级传感器处理 (RPM/频率计算)

## 📝 开发注意事项

### 通用注意事项
- 所有函数都已有声明，实现时注意参数和返回值类型匹配
- 遵循现有代码风格和错误处理模式
- 添加充分的错误检查和日志输出
- 实现前先编写测试用例

### 模块特定注意事项
- **WiFi模块**: 注意线程安全，使用事件组同步
- **显示模块**: 注意SPI时序和显示屏特定命令
- **传感器模块**: 注意ADC采样精度和滤波算法
- **电机模块**: 注意时序控制和功耗管理

## 🎯 总结

- **总计未实现功能**: 31个
- **预计总工作量**: 65-85小时
- **核心功能工作量**: 25-35小时
- **建议分3个阶段完成**

各模块功能相对独立，可以并行开发。建议先完成核心功能，再逐步添加扩展功能。 