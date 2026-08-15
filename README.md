本项目基于 STM32 实现车载智能仪表盘，支持多机 CAN 通信、LVGL 液晶仪表界面、OneNET 云平台 MQTT 数据上报、A/B 分区 OTA 远程固件升级，完整实现一主两从分布式车载仪表方案。
⚠️ **项目说明：本项目部分代码使用 AI 进行优化，主要为代码结构重构、代码注释补全。**

## 项目简介

本项目实现一套分布式车载智能仪表盘系统：
- 🚗 **硬件架构**：`1个STM32F407主控 + 2个STM32F103从机`，通过 CAN 总线组网；从机负责车辆数据采集、按键告警信号输入；主控完成 LVGL 液晶仪表渲染。
- ☁️ **云平台**：ESP8266 接入中国移动 OneNET 平台，MQTT 协议上报车辆状态数据。
- 🔄 **OTA 升级**：基于 W25Q128 外部 SPI Flash 实现 A/B 双分区 + 标志区 BootLoader OTA 远程升级，支持分片断点续传、MD5 固件校验、升级失败自动回滚，保障升级可靠性。

完整工程分为 4 套工程：
1. `Boot`：STM32F407 BootLoader 引导程序，固件搬运、校验、回滚、跳转 APP
2. `APP`：STM32F407 主控应用工程，LVGL 界面、CAN 接收、ESP8266 联网、OTA 业务逻辑
3. `F1_1`：STM32F103‑A 从机，模拟采集车速、转速、油量、水温，发送 CAN `0x200`数据帧
4. `F1_2`：STM32F103‑B 从机，按键扫描，输出告警图标状态 CAN `0x201`帧

## 🧰 硬件清单
| 器件 | 备注 |
| --- | --- |
| STM32F407ZGT6 最小系统板 | 推荐使用某点原子开发板；若使用非正点原子板子，需要额外外接 W25Q128 SPI Flash |
| STM32F103C8T6 最小系统板 ×2 | CAN 总线两个采集从节点 |
| TJA1050 CAN 收发器 ×3 | 主控、两个从机各配 1 个，组成 CAN 总线网络 |
| ESP8266 WiFi 模块 | AT 指令固件，对接 OneNET 云平台 |
| ILI9341 2.8 寸 TFT‑LCD 屏幕 | 分辨率`320*240`，带触摸功能，LVGL 图形界面显示 |
| W25Q128 SPI Flash（16MB） | **非某点原子 F4 板子必须自备**，用于 OTA 固件存储、A/B 分区、升级标志区 |
| ST‑LINK / DAP 高速下载器 | 程序烧录调试 |
| 串口转 TTL 模块 | 日志打印、ESP8266 调试 |
| 杜邦线若干 | 硬件接线 |
> 💡提示：CAN 总线需要在总线两端增加 120Ω 终端电阻，保证 CAN 通信稳定，一般模块自带。

## 💻 软件说明

本项目全部工程基于`STM32CubeMX + HAL库`开发，操作系统采用 FreeRTOS；图形库使用 LVGL8.x，具体软件说明查看“软件流程总结.md”。
整体软件分为三层：

- `Core`：HAL 库初始化、外设初始化、中断、RTOS 内核相关代码
- `BSP`：板级外设驱动（W25Q128、ILI9341LCD、触摸、ESP8266、EEPROM 等）
- `APP/MID`：业务逻辑与中间件，CAN 协议解析、LVGL 仪表 GUI、MQTT、OTA、BootLoader 逻辑

### 核心功能
1. **多节点 CAN 通信**：500kbps 标准 CAN 帧；`0x200`车辆数据帧、`0x201`告警图标状态帧，实现分布式采集。
2. **LVGL 仪表盘 UI**：转速表、车速表、油量、水温、里程、各类告警图标，数据与界面解耦。
3. **OneNET 云接入**：ESP8266 AT 指令实现 MQTT 长连接，上报车辆运行状态，接收平台下发指令。
4. **安全 OTA 远程升级**
   - W25Q128 划分 Zone1 下载区、Zone2 备份区、Zone3 标志区三区架构
   - BootLoader 完成固件搬运、MD5 完整性校验
   - 断点续传、看门狗保护、升级失败计数，超过阈值自动回滚旧固件
   - 标志区使用 R‑M‑E‑W 读改写机制，适配 SPI Flash 擦写特性

### 工程文件说明
- `Boot/`        # F407 BootLoader引导工程
- `APP/`         # F407主控APP主工程
- `F1_1/`        # F103‑A数据采集从机
- `F1_2/`        # F103‑B按键告警从机

## ⚙️ 用户配置参数（使用前务必修改）

运行前需要根据你自己的 OneNET 平台设备信息与 WiFi 信息，修改以下头文件中的宏定义（代码中默认为 `YourXXX` 占位符）：

| 文件 | 宏定义 | 说明 |
| --- | --- | --- |
| `APP/MID/ota_http.h` | `OTA_PRODUCT_ID` | OneNET 产品 ID |
| `APP/MID/ota_http.h` | `OTA_DEVICE_NAME` | 设备名称 |
| `APP/MID/ota_http.h` | `OTA_USER_ID` | 用户 ID |
| `APP/MID/ota_http.h` | `OTA_AUTHORIZATION` | OTA 鉴权 token |
| `APP/MID/ESP_MQTT.h` | `MQTT_PRODUCT_ID` | OneNET 产品 ID |
| `APP/MID/ESP_MQTT.h` | `MQTT_DEVICE_NAME` | 设备名称 |
| `APP/MID/ESP_MQTT.h` | `MQTT_AUTH_PARAM` | MQTT 鉴权参数 |
| `APP/MID/ESP_MQTT.h` | `USER_SSID` | 连接 WiFi 名称 |
| `APP/MID/ESP_MQTT.h` | `USER_PASS` | WiFi 密码 |

## 📚 复刻本项目需要掌握的知识
- BootLoader 原理、STM32 启动流程、中断向量表重定向、片上 Flash 分区管理
- STM32 内存管理、Flash 读写、SPI Flash (W25Q128) 擦写特性
- CAN 总线协议、CAN 外设配置、多设备 CAN 组网
- LVGL 图形库移植、控件使用、UI 界面开发
- FreeRTOS 实时操作系统：任务、信号量、任务调度、多任务同步
- MQTT 协议、HTTP 分片下载、OneNET 物联网平台使用
- OTA 固件升级设计思路，A/B 分区、校验、失败回滚机制

## 📝 编译烧录顺序

1. 烧录`Boot`工程到 F407（占用 F407 Flash 前 64KB）
2. 烧录`APP`主控应用工程
3. 分别烧录`F1_1`、`F1_2`两个从机工程到两块 STM32F103
4. 硬件接线，接入 CAN 总线终端电阻，修改平台与 WiFi 配置，上电测试

## ✅ 测试方案
1. 模块自测：工程内置 CAN_TEST_FLAG 宏，开启后 CAN 静默回环模式，单机调试 CAN 收发逻辑
2. 整机联调：CAN 总线组网，从机模拟数据 / 按键输入，主控 LCD 显示仪表数据，设备连接 OneNET 上报属性
3. OTA 验证：OneNET 平台上传固件任务，测试正常升级流程；人为制造异常，验证固件自动回滚功能

## 🔭 改进方向
1. 当前从机为串口模拟传感器输入，可接入霍尔转速传感器、油位传感器、水温传感器完成实车信号采集
2. OTA 鉴权当前使用硬编码 token，可以引入 HMAC‑SHA1 动态签名提升安全性
3. UI 可以拓展导航、故障码诊断等更多界面功能
4. 可迁移 RT‑Thread 操作系统，简化组件开发

## 📖参考文档
1. OneNET 开放平台开发者文档
2. STM32F407 / STM32F103 参考手册
3. LVGL 官方文档
4. W25Q128 数据手册
5. ESP8266 AT 指令手册
