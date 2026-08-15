# OTA 固件升级系统 — 架构分析文档

> 目标平台: STM32F407ZGT6 + ESP8266 (WiFi) + W25Q128 (16MB 外部 Flash)
>
> 编译环境: ARMCC v5 (C90), Keil MDK, FreeRTOS + CMSIS-RTOS v2
>
> LVGL 版本: v8.3.11
>
> 更新日期: 2026-08-12

---

## 1. 系统概览

### 1.1 物理存储布局

```
STM32F407 内部 Flash (1MB)
├── 0x08000000 ~ 0x0800FFFF   BootLoader 区    Sector 0~3   64KB
└── 0x08010000 ~ 0x080FFFFF   APP 固件区       Sector 4~11  960KB

W25Q128 外部 Flash (16MB)
├── 0x000000 ~ 0x5FFFFF       Zone1: 固件下载区   6MB
├── 0x600000 ~ 0xBFFFFF       Zone2: 备份区       6MB
├── 0xC00000 ~ 0xC0FFFF       Zone3: OTA 标志区   64KB (仅用首 4KB 扇区)
└── 0xC10000 ~ 0xFFFFFF       保留
```

### 1.2 文件依赖树

```
APP_ota.c (应用层 — 业务流程编排)
  ├── ota_http.c   (通信层 — OneNET HTTP/AT 指令)
  │     ├── esp8266.c   (AT 指令收发, UART6 DMA)
  │     ├── md5.c       (MD5 摘要算法)
  │     └── ota_flags.c (W25Q128 标志区读写)
  │           └── w25q128.c (SPI Flash 驱动)



撒大大  ├── stm32f4xx_hal.h  (内部 Flash 擦除/编程 API)
  └── iwdg.h           (长耗时搬运期间喂狗)

APP_lv_gui.c (LVGL UI 层)
  ├── ota_http.h       (OTA_VERSION_STR, ota_download_progress)
  └── APP_ota.h        (APP_OTA_RequestDownload 回调)
```

### 1.3 FreeRTOS 任务协作

```
优先级 高 → 低
┌───────────┐ ┌────────────┐ ┌──────────┐ ┌──────────┐ ┌───────────┐
│ LVGLTask  │ │Lv_page_show│ │ espRx    │ │ OTA_Task │ │ IWDG_Task │
│ lv_timer  │ │ LVGL 初始化│ │ MQTT 收发 │ │ OTA 管理 │ │ 喂狗 2s   │
│ handler() │ │ 一次性任务 │ │ UART6 DMA │ │ 500ms 轮询│ │          │
└───────────┘ └────────────┘ └──────────┘ └──────────┘ └───────────┘
```

### 1.4 OTA 升级时序总览

```
┌─────────┐   ┌──────────┐   ┌──────────┐   ┌───────────┐   ┌─────────┐
│ OneNET  │   │ ESP8266  │   │ OTA_Task │   │BootLoader │   │ W25Q128 │
└────┬────┘   └────┬─────┘   └────┬─────┘   └─────┬─────┘   └───┬─────┘
     │             │              │                │              │
     │             │              │ ① 周期性检查   │              │
     │  返回V1.2.7 │              │ OTA_ReportAndCheck()         │
     │<────────────│<─────────────│                │              │
     │             │              │                │              │
     │             │              │ ② UI 显示 NEW  │              │
     │             │              │ 用户按 GO      │              │
     │             │              │                │              │
     │             │              │ ③ 分片下载     │              │
     │  GET 3584B  │              │ ← ota_download_progress → UI │
     │<────────────│<─────────────│ ──────────────>│──> Zone1     │
     │   ...N 片   │              │                │   写入       │
     │             │              │                │              │
     │             │              │ ④ MD5 校验     │              │
     │             │              │ protect=NONE   │              │
     │             │              │ upgrade=READY  │              │
     │             │              │                │              │
     │             │              │ ⑤ IWDG 复位    │              │
     │             │              │ ╳              │              │
     │             │              │                │ ⑥ BootLoader│
     │             │              │                │ 检测 READY   │
     │             │              │                │ Zone1→APP    │
     │             │              │                │ ────────────>│ 读
     │             │              │                │              │
     │             │              │                │ ⑦ COMMITTED  │
     │             │              │                │ protect=NONE │
     │             │              │                │ Jump APP     │
     │             │              │                │              │
     │             │              │ ⑧ APP 运行     │              │
     │             │              │ APP_OTA_Run()  │              │
     │             │              │ 检测 COMMITTED │              │
     │  上报版本   │              │ 版本上报       │              │
     │<────────────│<─────────────│                │              │
     │             │              │ Zone1→Zone2备份│              │
     │             │              │ ─────────────────────────────>│ 写
     │             │              │ protect=OK     │              │
     │             │              │ upgrade=NONE   │              │
     │             │              │ 升级完成 ✅    │              │
```

---

## 2. 标志区 (ota_flags.h / ota_flags.c)

### 2.1 字段布局

标志区位于 W25Q128 `0x00C00000` 开头的首个 4KB 扇区内：

| 偏移      | 大小  | 字段               | 读写 API                                                                      | 说明 |
|------     |------ |------             |----------                                                                     |------|
| 0x0000    | 4B    | upgrade_flag      | `Flags_ReadUpgradeFlag` / `Flags_SetUpgradeFlag`                              | OTA 状态机核心 |
| 0x0004    | 4B    | protect_flag      | `Flags_ReadProtectFlag` / `Flags_SetProtectFlag`                              | APP 运行确认 |
| 0x0010    | 24B   | current_ver       | `Flags_ReadCurrentVer` / `Flags_WriteCurrentVer`                              | 当前运行版本 |
| 0x0030    | 24B   | new_ver           | `Flags_ReadNewVer` / `Flags_WriteNewVer`                                      | OTA 目标版本 |
| 0x0050    | 24B   | backup_ver        | `Flags_ReadBackupVer` / `Flags_WriteBackupVer`                                | 备份区固件版本 |
| 0x0070    | 4B    | fw_size           | `Flags_ReadFwSize` / `Flags_WriteFwSize`                                      | 新固件文件大小 |
| 0x0080    | 4B    | boot_fail_cnt     | `Flags_ReadBootFailCnt` / `Flags_IncBootFailCnt` / `Flags_ClearBootFailCnt`   | 启动失败计数 |
| 0x0090    | 4B    | backup_fw_size    | `Flags_ReadBackupFwSize` / `Flags_WriteBackupFwSize`                          | 备份固件大小 |
| 0x0100    | 4B    | downloaded_size   | `Flags_ReadDownloadedSize` / `Flags_SaveDownloadedSize`                       | 断点续传进度 |
| 0x0200    | 64B   | token             | `Flags_ReadToken` / `Flags_SaveToken`                                         | 鉴权 token 缓存 |
| 0x0240    | 33B   | file_md5          | `Flags_ReadMd5` / `Flags_SaveMd5`                                             | 固件 MD5 (32 字符 hex) |

### 2.2 R-M-E-W（读-改-擦-写）写入机制

W25Q128 最小擦除单位 = 4KB 扇区，不支持字节级覆盖。

```
① Read   — W25Q128_Read() 将整个 4KB 扇区读到 RAM buffer sector_buf[4096]
② Modify — 在 sector_buf[] 中修改目标字节
③ Erase  — W25Q128_SectorErase() 擦除 Flash 上该扇区
④ Write  — W25Q128_Write() 将 sector_buf[] 整体写回

→ 同扇区内其他字段完整保留，不会因修改单个字段而破坏相邻数据
```

所有上层 API（`Flags_SetUpgradeFlag`、`Flags_WriteCurrentVer`、`Flags_SaveMd5` 等）都通过 `Flags_WriteBytes()` 统一执行 R-M-E-W。每次写入一个字段，都会经历完整的 4KB 读→改→擦→写周期，这是确保数据一致性的代价。

### 2.3 OTA 状态机

```
                    OTA 下载完成
  NONE (0x00000000) ────────────→ READY (0xAA55AA55)
    ↑                                  │
    │                           BootLoader 搬运固件
    │                           Zone1 → APP Flash
    │                                  │
    │                                  ↓
    │                          COMMITTED (0x55AA55AA)
    │                                  │
    │                    ┌─────────────┴──────────────┐
    │                    │ protect=OK                 │ protect=NONE
    │                    │ APP 确认新固件正常          │ APP 未确认(崩溃)
    │                    │ → ClearUpgradeComplete()   │ → boot_fail_cnt++
    │                    └─────────────┬──────────────┘
    │                                  │
    └──────────────────────────────────┘
      cnt≥3 → 回滚到 Zone2 备份
```

**魔数设计**:
- `0xAA55AA55` 和 `0x55AA55AA`：互为字节反转，降低 Flash 位翻转导致误判的概率
- `0xBB66BB66`：保护标志独立于升级标志，确保 clean separation of concerns
- `0x00000000`：Flash 擦除后的默认值，也作为 NONE 语义

**protect_flag 的关键修复**:
- 旧版 Bug：`SetCommittedFlag()` 只写 COMMITTED，不清除 protect_flag。上一次 APP 运行设置的 protect=OK 会残留，导致 BootLoader 误判"APP 已确认"→跳过回滚→死循环。
- 修复（两处）：
  1. **ota_http.c** `OTA_DownloadFirmware()`：设 READY 前强制 `Flags_SetProtectFlag(PROTECT_MAGIC_NONE)`
  2. **bootloader.c** `SetCommittedFlag()`：写 COMMITTED 同时清除 protect=NONE 和 boot_fail_cnt=0

### 2.4 辅助功能

| 函数 | 用途 |
|------|------|
| `Flags_Format()` | 擦除整个 4KB 扇区（新 W25Q128 出厂全 0xFF，自动检测后格式化） |
| `Flags_PrintAll()` | 串口打印所有字段（调试用，格式化表格输出） |
| `Flags_IncBootFailCnt()` | 读取→+1→写回（注意：这会触发一次完整的 R-M-E-W） |

---

## 3. HTTP 通信层 (ota_http.h / ota_http.c)

### 3.1 OneNET OTA API 三步流程

通信目标：`iot-api.heclouds.com:80`（HTTP 明文）

| 步骤 | 函数 | 方法 | 端点 | 请求/响应要点 |
|------|------|------|------|--------------|
| 1 | `OTA_ReportVersion()` | POST | `/fuse-ota/{pid}/{dev}/version` | Body: `{"s_version":"V1.2.6","f_version":"V1.2.6"}` |
| 2 | `OTA_CheckTask()` | GET | `/fuse-ota/{pid}/{dev}/check?type=2&version=1.2.6` | 返回 JSON: target, tid, size, md5, type |
| 3 | `OTA_DownloadFirmware()` | GET | `/fuse-ota/{pid}/{dev}/{tid}/download` | Range 分片, Keep-Alive |

**合并优化**：`OTA_ReportAndCheck()` 在单次 TCP 连接中依次执行 Step1+Step2，比分别调用少一次 TCP 握手（ESP8266 每次 `AT+CIPSTART` 约 2 秒，省下一次连接可显著提升可靠性）。

### 3.2 下载策略

| 特性 | 参数 | 说明 |
|------|------|------|
| **Keep-Alive** | `Connection: keep-alive` | 整个下载仅 1 次 TCP 连接，所有分片复用 |
| **分片大小** | 3584 字节 (3.5KB) | HTTP 头约 300B + 数据 < ESP_Buff 4096，不会溢出 |
| **Range 请求** | `Range: bytes={offset}-{offset+3583}` | 每片独立请求，服务器返回 206 Partial Content |
| **断点续传** | 每片完成后写 `FLAGS_DOWNLOADED_SIZE` | 重启后匹配 version → 从上次 offset 继续 |
| **分片重试** | 最多 3 次 | 重试前断开 TCP 重连，避免旧连接残留干扰 |
| **MD5 校验** | 下载完成后回读 Zone1 全量计算 | 与服务端下发 MD5 对比，不匹配则清除 READY |

### 3.3 数据接收路径

```
OneNET 服务器
    │  HTTP Response (TCP)
    ▼
ESP8266 (WiFi STA)
    │  UART TX
    ▼
STM32 UART6 RX (DMA 循环模式)
    │  IDLE 中断 (帧间隔检测)
    ▼
parse_ipd() — 解析 +IPD,<len>: 前缀
    │  提取 payload 到 TCPdata[]
    ▼
http_find_body() — 定位 \r\n\r\n (HTTP 头/体分界)
    │  返回 body 指针 + 长度
    ▼
W25Q128_Write(Zone1 + offset, body_ptr, body_len)
```

### 3.4 TCP 二进制数据接收的关键处理

ESP8266 透传模式下，`+IPD` 前缀中可能包含形似 HTTP 头的数据。核心函数 `find_body_in_ipd()` 通过扫描 `\r\n\r\n` 定位真正的 body 起始位置，避免将 HTTP 头写入 Flash。

### 3.5 主要 API

| 函数                      | 可见性        | 功能 |
|------                     |--------       |------|
| `OTA_ReportAndCheck()`    | public        | Step1+2 合并（推荐使用，比分别调用省一次 TCP） |
| `OTA_ReportVersion()`     | public        | Step1: 上报版本号 |
| `OTA_CheckTask()`         | public        | Step2: 查询升级任务 |
| `OTA_DownloadFirmware()`  | public        | Step3: 主下载循环（6 阶段） |
| `OTA_VerifyFirmware()`    | public        | 回读 Zone1 全量 MD5 vs 预期值 |
| `OTA_CompareVersion()`    | public        | 三段式版本号比较 |
| `OTA_ESP_EnterCmdMode()`  | public        | UART 刷新 + `+++` 退出透传 → AT 命令模式 |
| `OTA_ESP_CloseAllTCP()`   | public        | `CIPCLOSE` 所有连接 + 设 `CIPMUX=0` |
| `OTA_ESP_Cleanup()`       | public        | = EnterCmd + CloseAllTCP 合并 |
| `OTA_Disconnect()`        | public        | 断开 OTA TCP 连接 |
| `OTA_TCP_Connect()`       | static        | 预清理 + `ESP_Connect_TCP` 重试 |
| `OTA_SendRequest()`       | static        | `AT+CIPSEND` → 轮询 `tcp_rx_flag` → 返回 TCPdata |
| `OTA_ResultStr()`         | static inline | 枚举 → 可读字符串 |

---

## 4. 应用层 (APP_ota.h / APP_ota.c)

### 4.1 APP_OTA_Run() — 启动时一次性流程

```
Phase A  等待 OTA_READY 信号量（ESP8266 WiFi + MQTT 就绪）
    │
Phase B  接管 ESP8266（Suspend espRx → EnterCmd → DisconnectMQTT → CloseTCP）
    │        三步顺序至关重要：必须先进入命令模式才能释放 MQTT，
    │        必须先释放 MQTT 才能关闭 TCP（否则 MQTT 持有的 TCP 拒绝关闭）
    │
Phase C  版本同步 + 标志区自检
    │    APP_OTA_SyncVersion()：flags 为空则写入编译版本
    │    APP_OTA_CheckFlags()：非预期魔数则自动 Format
    │
Phase D  状态机分流
    ├── COMMITTED → 版本上报 + Zone1→Zone2 备份 + protect=OK + upgrade=NONE
    ├── READY    → 本地 MD5 校验 Zone1 固件完整性（不联网）
    └── NONE     → OTA_ReportAndCheck() → 有新版本则 gui_set_ota_available(1)
    │
Phase E  恢复 MQTT 连接 + Resume espRx
```

### 4.2 非阻塞下载模式

核心问题：`OTA_DownloadFirmware()` 是阻塞函数，如果在 LVGL 任务中调用，UI 会冻结数十秒直到下载完成。

解决方案：

```
LVGL 按钮点击
    │
    ▼
APP_OTA_RequestDownload()         ← 在 LVGL 任务上下文执行
    │  仅设置 ota_download_requested = 1
    │  立即返回（<1μs）
    ▼
OTA_Task 轮询 (500ms)             ← 在 OTA_Task 上下文执行
    │  检测到 ota_download_requested
    ▼
APP_OTA_StartDownload()           ← 阻塞下载在此执行
    │  ota_download_progress 每片更新
    │  LVGL 定时器独立读取并刷新 UI
    ▼
成功 → IWDG 复位 → BootLoader
失败 → 恢复 MQTT → gui_ota_download_failed()
```

关键标志位：

| 标志 | 写者 | 读者 | 用途 |
|------|------|------|------|
| `ota_download_requested` | LVGL 按钮回调 | OTA_Task 500ms 轮询 | 跨任务异步通信 |
| `ota_busy` | `APP_OTA_StartDownload()` | OTA_Task 周期性检查 | 阻止周期性检查抢占 ESP8266 |
| `ota_download_progress` (0~100) | `OTA_DownloadFirmware()` | LVGL 200ms 定时器 | 单字节原子，无需锁 |
| `ota_ui_downloading` | LVGL 事件回调 | `gui_set_ota_available()` | 阻止 GO 按钮意外复活 |
| `OTA_READY` 信号量 | `ESP_RxTask` (释放) | `OTA_Task` (获取) | ESP8266 就绪同步 |

### 4.3 固件备份机制

```
BootLoader 搬运 Zone1 → APP Flash
    │  写 COMMITTED + protect=NONE
    ▼
APP 启动 → APP_OTA_Run() 检测 COMMITTED
    │
    ▼
APP_OTA_BackupFirmware(fw_size)
    │  按扇区擦除 Zone2 目标区域
    │  4KB 分块: Zone1[0..fw_size] → Zone2[0..fw_size]
    │
    ▼
Flags_WriteBackupVer(OTA_VERSION_STR)
Flags_WriteBackupFwSize(fw_size)
Flags_SetProtectFlag(PROTECT_MAGIC_OK)   ← APP 确认运行正常
Flags_SetUpgradeFlag(UPGRADE_MAGIC_NONE)
```

> ⚠️ **已知限制**: `fw_size` 来自 OTA 下载时服务器上报的文件大小。如果固件是通过 Keil Download 直接烧录（不经过 OTA 流程），`fw_size` 不会被更新为实际大小，备份尺寸会偏小。

---

## 5. BootLoader (bootloader.c / bootloader.h)

### 5.1 项目结构

BootLoader 是**独立 Keil 工程**（`F:\theCarUser\smartCar\Boot`），不链接 APP 代码。

- 链接脚本：`Boot.sct` — `LR_IROM1 0x08000000 0x0000C000`（64KB）
- 编译产物：`Boot.hex`，通过 Keil Download 烧录
- 启动流程：`main.c` → 外设初始化 → `BootLoader_Run()`（阻塞，不返回）

### 5.2 决策流程

```
BootLoader_Run()
    │
    ├─ 决策 A: upgrade_flag == COMMITTED?
    │   ├─ protect == OK → ClearUpgradeComplete() → Jump APP
    │   │                  （APP 已确认，升级成功，清理标志）
    │   └─ protect != OK → boot_fail_cnt++
    │       ├─ cnt < 3 → Jump APP（等看门狗超时重启，重试）
    │       └─ cnt ≥ 3 → DoRollback()
    │           ├─ 验证 Zone2 固件头 → 擦除 APP 区 → Zone2 → APP Flash
    │           └─ 更新标志区（upgrade=NONE, protect=NONE, current=backup）
    │             → NVIC_SystemReset()
    │
    ├─ 决策 B: upgrade_flag == READY?（否则直接 Jump APP）
    │   └─ No → Jump APP（无待升级固件，正常启动）
    │
    └─ READY: 搬运 Zone1 → APP Flash
        ├─ 验证固件头（SP 在 SRAM 范围，大小有效）
        ├─ EraseAppArea()（逐扇区擦除，擦一个喂一次狗）
        ├─ 4KB 分块搬运（HAL_FLASH_Program WORD by WORD）
        ├─ 验证前 1KB 写入正确
        ├─ SetCommittedFlag(new_ver)（同时清除 protect=NONE, fail_cnt=0）
        └─ NVIC_SystemReset()
```

### 5.3 回滚机制

| 参数 | 值 | 说明 |
|------|-----|------|
| `BOOT_FAIL_MAX` | 3 | 连续失败 3 次触发回滚 |
| 回滚源 | W25Q128 Zone2 (`0x600000`) | 备份区 |
| 回滚目标 | STM32 `0x08010000` | APP Flash |
| 校验条件 | SP 头有效 + 大小 >0 且 ≤6MB | 防止恢复损坏的备份 |

回滚流程：
1. 读 Zone2 头部验证 SP 在 `0x20000000~0x2002FFFF` 范围内
2. 逐扇区擦除 APP Flash（`EraseAppArea`，每个扇区后喂狗）
3. 4KB 分块读取 Zone2，逐 WORD 写入 STM32 Flash
4. 更新标志区：upgrade=NONE, protect=NONE, boot_fail_cnt=0, current_ver=backup_ver
5. `NVIC_SystemReset()` 重新启动

### 5.4 Jump_to_APP()

```
① 验证 APP 区 SP 在 SRAM 范围内（0x20000000 ~ 0x2002FFFF）
② HAL_UART_DeInit(&huart2) — 释放 UART2
③ HAL_DeInit()              — 复位所有外设
④ SysTick->CTRL = 0         — 停止 SysTick（避免跳转后产生异常）
⑤ __set_PRIMASK(1)          — 关全局中断
⑥ __set_MSP(APP_SP)         — 设置主栈指针为 APP 的 SP
⑦ ((pFunction)APP_PC)()     — 跳转到 APP 复位向量
```

### 5.5 SetCommittedFlag() 修复详解

这是解决"回滚永不触发"Bug 的关键修复：

```c
// 旧版（Bug）: 只写 COMMITTED
memcpy(&flags_buf[FLAGS_UPGRADE_OFFSET], &committed, 4);
// protect_flag 保留旧值 → 上一轮 APP 写的 OK 残留

// 新版（修复）: 同时清除 protect 和 fail_cnt
memcpy(&flags_buf[FLAGS_UPGRADE_OFFSET], &committed, 4);
memcpy(&flags_buf[FLAGS_PROTECT_OFFSET], &none, 4);     // ← 新增
memcpy(&flags_buf[FLAGS_BOOT_FAIL_CNT], &zero, 4);      // ← 新增
```

**为什么需要这两条**：新固件可能崩溃，必须在新固件运行后自行确认（设置 protect=OK）。如果 BootLoader 搬运固件时不先把 protect 清零，上一次 APP 运行留下的 protect=OK 会残留，导致 BootLoader 在下次启动时误判"新固件已确认 → 正常运行"→ 跳过回滚 → 跳转到崩溃的固件 → 看门狗复位 → 无限循环。

---

## 6. LVGL UI 层 (APP_lv_gui.c)

### 6.1 OTA 相关 UI 元素

全部位于顶部黑条（`top_bar`）内，高度 20px：

| 元素 | 变量 | 位置 | 说明 |
|------|------|------|------|
| NEW 标签 | `label_ota` | 顶栏中部 | 蓝色文字 "NEW"，有新版本时显示 |
| GO 按钮 | `btn_ota_download` | NEW 右侧 | 40×18 青色按钮，文字 "GO" |
| 百分比文字 | `label_ota_progress` | GO 按钮位置 | 蓝色文字，如 "45%"，下载中显示 |
| 版本号 | `label_ver` | 顶栏右下角 | 白色小字，如 "V1.2.6" |

### 6.2 下载 UI 状态切换

```
正常状态:  [无显示]                              label_ver
有新版本:  NEW [GO]                              label_ver
下载中:    NEW 45%                               label_ver
           (GO隐藏) (百分比刷新)
下载失败:  NEW [GO]                              label_ver
           (恢复)
下载成功:  → IWDG 复位 → 整个系统重启
```

### 6.3 关键守卫逻辑

`gui_set_ota_available()` 中的 `ota_ui_downloading` 守卫：

```c
void gui_set_ota_available(int available) {
    if (ota_ui_downloading) return;  // ← 下载中拒绝修改 UI
    // ... 正常显示/隐藏 NEW 和 GO 按钮
}
```

**为什么需要**：OTA_Task 的周期性检查（每 10 秒）会调用 `gui_set_ota_available(0)` 来隐藏 OTA 通知。如果用户已经按下 GO 按钮正在下载，周期性检查可能在下载期间触发，导致 `gui_set_ota_available(0)` → `gui_set_ota_available(1)` → GO 按钮意外复活。`ota_ui_downloading` 守卫阻止了这种情况。

---

## 7. 关键设计决策

| 决策 | 原因 |
|------|------|
| **标志区放 W25Q128 而非 STM32 内部 Flash** | STM32 Flash 擦除时 CPU 暂停取指（stall），影响中断响应；W25Q128 独立 SPI 操作不干扰 CPU |
| **R-M-E-W 而非整区格式化** | 保留同扇区内版本号、token、MD5 等字段，单字段修改不误伤其他数据 |
| **protect=NONE 双重保险（ota_http + bootloader）** | 防御纵深：新 BootLoader 修复 + APP 侧也主动清除，即使旧 BootLoader 也能工作 |
| **Keep-Alive 单 TCP 连接下载** | ESP8266 `AT+CIPSTART` 每次约 2 秒；310KB 固件约 87 片，单连接节省大量时间 |
| **非阻塞下载（RequestDownload 模式）** | 下载在 OTA_Task 执行，LVGL 任务继续刷新屏幕，用户感知不到卡顿 |
| **IWDG 复位（关中断死等）替代 NVIC_SystemReset** | IWDG 由独立 LSI 时钟驱动，不受 CPU 状态/调试器/DMA 影响，更可靠 |
| **ota_ui_downloading 守卫** | 防止周期性检查触发 `gui_set_ota_available` 意外恢复 GO 按钮 |
| **BootLoader 独立工程** | BootLoader 不链接 APP 代码，各自独立编译，避免符号冲突和误链接 |
| **C90 兼容（ARMCC v5）** | 变量声明必须在块开头（`uint16_t i` 不能在 `for` 内声明），前向声明必须手动添加 |
| **备份使用 OTA 下载时的 fw_size** | 局限：Keil Download 不更新 fw_size，备份尺寸依赖最后一次 OTA 流程 |

---

## 8. 目录结构一览

```
项目根目录
├── APP/
│   ├── APP_ota.c / .h              OTA 应用层（业务流程编排）
│   ├── APP_lv_gui.c / .h           LVGL 界面（OTA UI 元素）
│   └── bootloader/
│       ├── bootloader.c / .h       BootLoader（独立工程拷贝）
│       ├── w25q128.c / .h          SPI Flash 驱动（独立工程拷贝）
│       └── iwdg.c / .h             看门狗驱动（独立工程拷贝）
│
├── MID/
│   ├── ota_http.c / .h             OneNET OTA HTTP 通信层
│   ├── ota_flags.c / .h            W25Q128 OTA 标志区管理
│   ├── esp8266.c / .h              ESP8266 AT 指令驱动
│   └── ESP_MQTT.c / .h             MQTT 客户端（OneNET 物联网平台）
│
├── Core/
│   └── Src/
│       └── freertos.c              FreeRTOS 任务创建（OTA_Task, espRx, IWDG）
│
├── Drivers/
│   └── W25Q128/
│       └── w25q128.c / .h          W25Q128 SPI Flash 驱动（APP 工程）
│
└── MDK-ARM/
    └── CSDN_LVGL/
        └── THE_CarUser.sct         链接脚本（APP 0x08010000）

BootLoader 独立工程（不在此目录树）
F:\theCarUser\smartCar\Boot\
├── APP/
│   ├── bootloader.c / .h           BootLoader 源码
│   ├── w25q128.c / .h              SPI Flash 驱动
│   └── iwdg.c / .h                 看门狗驱动
└── MDK-ARM/
    └── Boot/
        └── Boot.sct                BootLoader 链接脚本（0x08000000, 64KB）
```

---

## 9. 调试与故障排查

### 9.1 关键日志标识

| 前缀 | 来源文件 | 含义 |
|------|----------|------|
| `[OTA]` | APP_ota.c / ota_http.c | APP 侧 OTA 流程日志 |
| `[BOOT]` | bootloader.c | BootLoader 侧日志 |
| `[ESP]` | ESP_RxTask | ESP8266 网络状态 |

### 9.2 常见问题

| 现象 | 可能原因 | 排查方法 |
|------|----------|----------|
| BootLoader 循环重启 | COMMITTED + protect=NONE + boot_fail_cnt 不断累加 | 串口观察 boot_fail_cnt 是否递增到 3 |
| 回滚后版本不对 | backup_ver / backup_fw_size 未正确写入 | `Flags_PrintAll()` 查看 backup 相关字段 |
| 下载超时 | ESP8266 信号弱 / TCP 分片丢失 | 检查分片重试日志，降低 chunk 大小 |
| GO 按钮闪烁 | `gui_set_ota_available` 与下载竞态 | 确认 `ota_ui_downloading` 守卫生效 |
| 进度不刷新 | LVGL 定时器未创建或已停止 | 检查 `ota_bar_timer` 和 `ota_ui_downloading` 状态 |
