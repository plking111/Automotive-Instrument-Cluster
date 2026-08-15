/**
 * @file    APP_lv_gui.h
 * @brief   车载仪表盘 UI — 对外接口
 */

#ifndef __APP_LV_GUI_H
#define __APP_LV_GUI_H

void lv_gui(void); /* UI 主入口, 构建整个仪表盘 */

/*=== 数据设置接口 ===*/
void gui_set_speed(int val);            /* 车速 km/h */
void gui_set_rpm(int val);              /* 转速 0~8000 */
void gui_set_fuel(int val);             /* 油量 0~100 */
void gui_set_temp(int val);             /* 水温 ℃ */
void gui_set_time(int h, int m, int s); /* 时间 */
void gui_set_date(int y, int m, int d); /* 日期 */
void gui_set_range(int val);            /* 里程 km */

/* 图标状态: 0=默认青色, 1=警告色 */
void gui_set_icon_car(int state);      /* 车辆 */
void gui_set_icon_seatbelt(int state); /* 安全带 */
void gui_set_icon_warning(int state);  /* 警告 */
void gui_set_icon_brake(int state);    /* 制动 */

/*=== OTA 相关 ===*/
void gui_set_wifi(int connected);                     /* 0=断开, 1=连接 */
void gui_set_ota_available(int available);            /* 0=无更新, 1=有新版本 */
void gui_set_ota_download_callback(void (*cb)(void)); /* 注册下载回调 */
void gui_ota_download_failed(void);                   /* 下载失败恢复按钮 */
void gui_set_version(const char *ver);                /* 版本号 */

#endif /* __APP_LV_GUI_H */
