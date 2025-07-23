# SmartJet v2.0 - ESP32-S3 IoT发电机控制器
# ESP-IDF 项目配置文件

cmake_minimum_required(VERSION 3.16)

# 设置项目信息
set(PROJECT_VER "2.0.0")
set(PROJECT_DESCRIPTION "SmartJet v2.0 - ESP32-S3 IoT Generator Controller")

# 包含ESP-IDF构建系统
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 定义项目
project(smartjet_v2) 