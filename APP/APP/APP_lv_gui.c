/**
 * @file    APP_lv_gui.c
 * @brief   车载仪表盘 UI (320x240)
 *
 * @note    三段布局: 顶部状态栏 / 中部仪表盘 / 底部信息栏
 */

#include "lvgl.h"
#include "APP_lv_gui.h"
#include <stdio.h>
#include <stdlib.h>
#include "src/extra/widgets/meter/lv_meter.h"
#include "src/core/lv_obj_draw.h"
#include "src/core/lv_event.h"
#include "src/widgets/lv_canvas.h"

#include "APP_icons_all.h" /* 车/安全带/警告/制动 */
#include "ota_http.h"      /* OTA_VERSION_STR */
#include "APP_can.h"       /* g_vehicle */

/*=== 颜色 ===*/
#define COL_BG lv_color_hex(0xFFFFFF)      /* 白色背景 */
#define COL_TOP_BAR lv_color_hex(0x000000) /* 黑色顶栏 */
#define COL_BOTTOM lv_color_hex(0x000000)  /* 黑色底栏 */
#define COL_TEXT lv_color_hex(0x000000)    /* 黑色文字 */
#define COL_DIM lv_color_hex(0x444444)     /* 深灰 */
#define COL_ACCENT lv_color_hex(0x1CC3C9)  /* 青色 */
#define COL_GREEN lv_color_hex(0x1CC3C9)   /* 青色 */
#define COL_BLACK lv_color_hex(0x000000)   /* 黑色 */
#define COL_WHITE lv_color_hex(0xFFFFFF)   /* 白色 */
#define COL_RED lv_color_hex(0xDD2222)     /* 红色 */
#define COL_YELLOW lv_color_hex(0xCC8800)  /* 黄色 */
#define COL_BORDER lv_color_hex(0xCCCCCC)  /* 浅灰边框 */

/*=== 全局对象 ===*/
static lv_obj_t *meter_left;
static lv_obj_t *meter_right;
static lv_meter_indicator_t *arc_left;
static lv_meter_indicator_t *arc_right;
static lv_obj_t *label_speed;
static lv_obj_t *label_rpm_val;
static lv_obj_t *label_fuel_val;
static lv_obj_t *label_temp_val;
static lv_obj_t *label_time;
static lv_obj_t *label_date;
static lv_obj_t *label_range;

/* 图标对象 (用于改色) */
static lv_obj_t *img_car;
static lv_obj_t *img_seatbelt;
static lv_obj_t *img_warning;
static lv_obj_t *img_brake;
static lv_obj_t *img_wifi;
static lv_obj_t *label_ota;
static lv_obj_t *btn_ota_download;
static lv_obj_t *label_ver;

/* OTA 进度文字 (替换 GO) */
static lv_obj_t *label_ota_progress; /* 百分比文字, 如 "45%" */
static lv_timer_t *ota_bar_timer;    /* 进度刷新定时器 */
static uint8_t ota_ui_downloading;   /* 1=下载中, 0=空闲 */

/* OTA 下载按钮回调 */
static void (*ota_download_cb)(void) = NULL;
static void ota_bar_timer_cb(lv_timer_t *timer); /* 前向声明 */

/**
 * @brief 下载按钮点击回调
 */
static void ota_btn_event_cb(lv_event_t *e)
{
    if (e->code == LV_EVENT_CLICKED && ota_download_cb)
    {
        /* 隐藏 GO 按钮, 显示百分比文字 */
        lv_obj_add_flag(btn_ota_download, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label_ota_progress, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_ota_progress, "0%");
        ota_ui_downloading = 1;

        /* 启动 200ms 定时器刷新百分比 */
        if (!ota_bar_timer)
        {
            ota_bar_timer = lv_timer_create(ota_bar_timer_cb, 200, NULL);
        }

        ota_download_cb(); /* -> APP_OTA_RequestDownload() */
    }
}

/**
 * @brief 刷新 OTA 进度百分比文字
 */
static void ota_bar_timer_cb(lv_timer_t *timer)
{
    extern volatile uint8_t ota_download_progress; /* ota_http.c */

    if (!ota_ui_downloading)
    {
        lv_timer_del(timer);
        ota_bar_timer = NULL;
        return;
    }

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", ota_download_progress);
    lv_label_set_text(label_ota_progress, buf);
}

/* 左侧标签回调: 除以1000显示 */
static char left_label_buf[8];
static void left_label_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->type == LV_METER_DRAW_PART_TICK && dsc->text)
    {
        int v = atoi(dsc->text);
        lv_snprintf(left_label_buf, sizeof(left_label_buf), "%d", v / 1000);
        dsc->text = left_label_buf;
    }
}

/*=== 左侧仪表盘 (转速, 开口向下) ===*/
static void create_left_meter(lv_obj_t *parent)
{
    /* 外围灰圈 */
    lv_obj_t *ring = lv_arc_create(parent);
    lv_obj_set_size(ring, 100, 100);
    lv_obj_set_pos(ring, 22, 60);
    lv_arc_set_bg_angles(ring, 135, 45);
    lv_arc_set_range(ring, 0, 100);
    lv_arc_set_value(ring, 100);
    lv_obj_set_style_arc_color(ring, lv_color_hex(0xAAAAAA), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ring, 0, LV_STATE_DEFAULT);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);

    meter_left = lv_meter_create(parent);
    lv_obj_set_size(meter_left, 100, 100);
    lv_obj_set_pos(meter_left, 22, 60);
    lv_obj_set_style_bg_opa(meter_left, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(meter_left, 0, LV_STATE_DEFAULT);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter_left);
    lv_meter_set_scale_range(meter_left, scale, 0, 8000, 270, 135);
    lv_meter_set_scale_ticks(meter_left, scale, 9, 1, 3, COL_DIM);
    lv_meter_set_scale_major_ticks(meter_left, scale, 2, 2, 8, COL_TEXT, 10);

    arc_left = lv_meter_add_arc(meter_left, scale, 8, COL_ACCENT, 0);
    lv_meter_set_indicator_start_value(meter_left, arc_left, 0);
    lv_meter_set_indicator_end_value(meter_left, arc_left, 0);

    /* 标签值除以1000 */
    lv_obj_add_event_cb(meter_left, left_label_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    /* 开口处显示转速 */
    lv_obj_t *lbl_rpm = lv_label_create(parent);
    lv_label_set_text(lbl_rpm, "RPM");
    lv_obj_set_style_text_color(lbl_rpm, COL_DIM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_rpm, &lv_font_montserrat_10, LV_STATE_DEFAULT);
    lv_obj_align_to(lbl_rpm, meter_left, LV_ALIGN_BOTTOM_MID, 0, 0);

    label_rpm_val = lv_label_create(parent);
    lv_label_set_text(label_rpm_val, "3200");
    lv_obj_set_style_text_color(label_rpm_val, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_rpm_val, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_align_to(label_rpm_val, lbl_rpm, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

/*=== 右侧仪表盘 (水温+油量, 开口向下) ===*/
static void create_right_meter(lv_obj_t *parent)
{
    /* 外围灰圈 */
    lv_obj_t *ring_r = lv_arc_create(parent);
    lv_obj_set_size(ring_r, 100, 100);
    lv_obj_set_pos(ring_r, 202, 60);
    lv_arc_set_bg_angles(ring_r, 135, 45);
    lv_arc_set_range(ring_r, 0, 100);
    lv_arc_set_value(ring_r, 100);
    lv_obj_set_style_arc_color(ring_r, lv_color_hex(0xAAAAAA), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ring_r, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(ring_r, 0, LV_STATE_DEFAULT);
    lv_obj_remove_style(ring_r, NULL, LV_PART_KNOB);

    meter_right = lv_meter_create(parent);
    lv_obj_set_size(meter_right, 100, 100);
    lv_obj_set_pos(meter_right, 202, 60);
    lv_obj_set_style_bg_opa(meter_right, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(meter_right, 0, LV_STATE_DEFAULT);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter_right);
    lv_meter_set_scale_range(meter_right, scale, 0, 100, 270, 135);
    lv_meter_set_scale_ticks(meter_right, scale, 11, 1, 3, COL_DIM);
    lv_meter_set_scale_major_ticks(meter_right, scale, 5, 2, 6, COL_TEXT, 10);

    arc_right = lv_meter_add_arc(meter_right, scale, 8, COL_ACCENT, 0);
    lv_meter_set_indicator_start_value(meter_right, arc_right, 0);
    lv_meter_set_indicator_end_value(meter_right, arc_right, 0);

    /* 开口处显示水温+油量 */
    label_temp_val = lv_label_create(parent);
    lv_label_set_text(label_temp_val, "84C");
    lv_obj_set_style_text_color(label_temp_val, COL_DIM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_temp_val, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align_to(label_temp_val, meter_right, LV_ALIGN_BOTTOM_MID, 0, 0);

    label_fuel_val = lv_label_create(parent);
    lv_label_set_text(label_fuel_val, "72%");
    lv_obj_set_style_text_color(label_fuel_val, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_fuel_val, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_align_to(label_fuel_val, label_temp_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

/*=== 中央车速 ===*/
static void create_speed_display(lv_obj_t *parent)
{
    /* 大数字 */
    label_speed = lv_label_create(parent);
    lv_label_set_text(label_speed, "58");
    lv_obj_set_style_text_color(label_speed, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_speed, &lv_font_montserrat_30, LV_STATE_DEFAULT);
    lv_obj_align(label_speed, LV_ALIGN_CENTER, 0, -12);

    /* 单位 */
    lv_obj_t *unit = lv_label_create(parent);
    lv_label_set_text(unit, "km/h");
    lv_obj_set_style_text_color(unit, COL_DIM, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_align_to(unit, label_speed, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
}

/*=== 底部信息栏 ===*/
static void create_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, 30);
    lv_obj_set_pos(bar, 0, 210);
    lv_obj_set_style_bg_color(bar, COL_BOTTOM, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_STATE_DEFAULT);

    /* 顶部分隔线 */
    lv_obj_t *sep = lv_obj_create(bar);
    lv_obj_set_size(sep, 320, 1);
    lv_obj_set_pos(sep, 0, 0);
    lv_obj_set_style_bg_color(sep, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sep, 0, LV_STATE_DEFAULT);

    /* WiFi 图标 (底部栏左侧) */
    img_wifi = lv_img_create(bar);
    lv_img_set_src(img_wifi, &icon6);
    lv_obj_set_pos(img_wifi, 5, 5);
    lv_obj_set_style_img_recolor(img_wifi, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_wifi, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_opa(img_wifi, LV_OPA_TRANSP, LV_STATE_DEFAULT); /* 初始全透明 */

    /* 版本号 (右下角) */
    label_ver = lv_label_create(bar);
    lv_label_set_text(label_ver, OTA_VERSION_STR);
    lv_obj_set_style_text_color(label_ver, COL_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_ver, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align(label_ver, LV_ALIGN_RIGHT_MID, -5, 0);
}

/*=== 定时刷新 ===*/
static void timer_cb(lv_timer_t *t)
{
    char buf[16];
    static uint8_t last_icons = 0xFF; /* 初始非法值, 强制首次刷新图标 */
    static int sec = 45;
    static int tick = 0;

    /* 从 CAN 共享数据刷新仪表盘 */
    gui_set_speed(g_vehicle.speed);
    gui_set_rpm(g_vehicle.rpm);
    gui_set_fuel(g_vehicle.fuel);
    gui_set_temp(g_vehicle.temp);

    /* 图标: 仅状态变化时刷新 */
    if (g_vehicle.icons != last_icons)
    {
        last_icons = g_vehicle.icons;
        gui_set_icon_seatbelt(g_vehicle.icons & ICON_SEATBELT);
        gui_set_icon_car(g_vehicle.icons & ICON_CAR);
        gui_set_icon_warning(g_vehicle.icons & ICON_WARNING);
        gui_set_icon_brake(g_vehicle.icons & ICON_BRAKE);
    }

    /* 模拟时间 (每10次=1秒) */
    if (++tick >= 10)
    {
        tick = 0;
        sec++;
        if (sec >= 60)
            sec = 0;
        lv_snprintf(buf, sizeof(buf), "09:12:%02d", sec);
        lv_label_set_text(label_time, buf);
    }
}

/*=== 主入口 ===*/
void lv_gui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COL_BG, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_STATE_DEFAULT);

    /* 顶部色条 */
    lv_obj_t *top_bar = lv_obj_create(scr);
    lv_obj_set_size(top_bar, 320, 20);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, COL_TOP_BAR, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top_bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(top_bar, 0, LV_STATE_DEFAULT);

    /* OTA新版本提示 (左上角) */
    label_ota = lv_label_create(top_bar);
    lv_label_set_text(label_ota, "NEW");
    lv_obj_set_style_text_color(label_ota, COL_YELLOW, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_ota, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align(label_ota, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_flag(label_ota, LV_OBJ_FLAG_HIDDEN); /* 初始隐藏 */

    /* OTA下载按钮 (紧挨 NEW 右侧) */
    btn_ota_download = lv_btn_create(top_bar);
    lv_obj_set_size(btn_ota_download, 40, 18);
    lv_obj_align_to(btn_ota_download, label_ota, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_set_style_bg_color(btn_ota_download, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_ota_download, lv_color_hex(0x159FA4), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn_ota_download, 2, LV_STATE_DEFAULT);
    lv_obj_add_flag(btn_ota_download, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_ota_label = lv_label_create(btn_ota_download);
    lv_label_set_text(btn_ota_label, "GO");
    lv_obj_set_style_text_color(btn_ota_label, COL_WHITE, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_ota_label, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_center(btn_ota_label);

    lv_obj_add_event_cb(btn_ota_download, ota_btn_event_cb, LV_EVENT_CLICKED, NULL);

    /* OTA 进度文字 (GO 按钮位置) */
    label_ota_progress = lv_label_create(top_bar);
    lv_label_set_text(label_ota_progress, "0%");
    lv_obj_set_style_text_color(label_ota_progress, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_ota_progress, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align_to(label_ota_progress, label_ota, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(label_ota_progress, LV_OBJ_FLAG_HIDDEN);

    /* 顶部分割线 */
    lv_obj_t *top_line = lv_obj_create(scr);
    lv_obj_set_size(top_line, 320, 1);
    lv_obj_set_pos(top_line, 0, 20);
    lv_obj_set_style_bg_color(top_line, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(top_line, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top_line, 0, LV_STATE_DEFAULT);

    /* 梯形分割线: lv_canvas + draw_polygon */
    static uint8_t cbuf_t[LV_CANVAS_BUF_SIZE_TRUE_COLOR(250, 24)];
    static uint8_t cbuf_b[LV_CANVAS_BUF_SIZE_TRUE_COLOR(250, 24)];

    lv_draw_rect_dsc_t poly_dsc;
    lv_draw_rect_dsc_init(&poly_dsc);
    poly_dsc.bg_color = COL_BORDER;
    poly_dsc.bg_opa = LV_OPA_COVER;

    /* 顶部梯形 (贴顶线) */
    lv_obj_t *tra_top = lv_canvas_create(scr);
    lv_obj_set_size(tra_top, 250, 24);
    lv_obj_set_pos(tra_top, 35, 20);
    lv_canvas_set_buffer(tra_top, cbuf_t, 250, 24, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(tra_top, COL_BG, LV_OPA_COVER);
    lv_point_t pt[] = {{0, 0}, {250, 0}, {230, 23}, {20, 23}};
    lv_canvas_draw_polygon(tra_top, pt, 4, &poly_dsc);

    /* 灰色底衬 (与梯形同色) */
    lv_obj_t *car_bg = lv_obj_create(scr);
    lv_obj_set_size(car_bg, 20, 20);
    lv_obj_set_style_bg_color(car_bg, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(car_bg, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(car_bg, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(car_bg, 0, LV_STATE_DEFAULT);
    lv_obj_align_to(car_bg, tra_top, LV_ALIGN_CENTER, -55, -0);

    /* 汽车图标 (青色着色) */
    img_car = lv_img_create(scr);
    lv_img_set_src(img_car, &car_icon);
    lv_obj_set_style_img_recolor(img_car, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_car, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_align_to(img_car, car_bg, LV_ALIGN_CENTER, 0, 0);

    /* 安全带图标 */
    lv_obj_t *icon2_bg = lv_obj_create(scr);
    lv_obj_set_size(icon2_bg, 20, 20);
    lv_obj_set_style_bg_color(icon2_bg, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(icon2_bg, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(icon2_bg, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(icon2_bg, 0, LV_STATE_DEFAULT);
    lv_obj_align_to(icon2_bg, tra_top, LV_ALIGN_CENTER, -88, 0);

    img_seatbelt = lv_img_create(scr);
    lv_img_set_src(img_seatbelt, &icon2);
    lv_obj_set_style_img_recolor(img_seatbelt, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_seatbelt, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_align_to(img_seatbelt, icon2_bg, LV_ALIGN_CENTER, 0, 0);

    /* 警告图标 (READY右侧) */
    lv_obj_t *warn_bg = lv_obj_create(scr);
    lv_obj_set_size(warn_bg, 20, 20);
    lv_obj_set_style_bg_color(warn_bg, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(warn_bg, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(warn_bg, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(warn_bg, 0, LV_STATE_DEFAULT);
    lv_obj_align_to(warn_bg, tra_top, LV_ALIGN_CENTER, 55, 0);

    img_warning = lv_img_create(scr);
    lv_img_set_src(img_warning, &warn_icon);
    lv_obj_set_style_img_recolor(img_warning, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_warning, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_align_to(img_warning, warn_bg, LV_ALIGN_CENTER, 0, 0);

    /* 制动图标 */
    lv_obj_t *brake_bg = lv_obj_create(scr);
    lv_obj_set_size(brake_bg, 20, 20);
    lv_obj_set_style_bg_color(brake_bg, COL_BORDER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(brake_bg, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(brake_bg, 4, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(brake_bg, 0, LV_STATE_DEFAULT);
    lv_obj_align_to(brake_bg, tra_top, LV_ALIGN_CENTER, 88, 0);

    img_brake = lv_img_create(scr);
    lv_img_set_src(img_brake, &icon5);
    lv_obj_set_style_img_recolor(img_brake, COL_ACCENT, LV_STATE_DEFAULT);
    lv_obj_set_style_img_recolor_opa(img_brake, LV_OPA_70, LV_STATE_DEFAULT);
    lv_obj_align_to(img_brake, brake_bg, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *ready = lv_label_create(scr);
    lv_label_set_text(ready, "READY");
    lv_obj_set_style_text_color(ready, COL_GREEN, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ready, &lv_font_montserrat_16, LV_STATE_DEFAULT);
    lv_obj_align_to(ready, tra_top, LV_ALIGN_CENTER, 0, 0);

    /* 底部梯形 (贴底线) */
    lv_obj_t *tra_bot = lv_canvas_create(scr);
    lv_obj_set_size(tra_bot, 250, 24);
    lv_obj_set_pos(tra_bot, 35, 186);
    lv_canvas_set_buffer(tra_bot, cbuf_b, 250, 24, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(tra_bot, COL_BG, LV_OPA_COVER);
    lv_point_t pb[] = {{20, 0}, {230, 0}, {250, 23}, {0, 23}};
    lv_canvas_draw_polygon(tra_bot, pb, 4, &poly_dsc);

    /* 底部梯形: 左时间, 中日期, 右里程 */
    label_time = lv_label_create(scr);
    lv_label_set_text(label_time, "09:12:45");
    lv_obj_set_style_text_color(label_time, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_time, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align_to(label_time, tra_bot, LV_ALIGN_LEFT_MID, 30, 0);

    label_date = lv_label_create(scr);
    lv_label_set_text(label_date, "2026-06-02");
    lv_obj_set_style_text_color(label_date, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align_to(label_date, tra_bot, LV_ALIGN_CENTER, 0, 0);

    label_range = lv_label_create(scr);
    lv_label_set_text(label_range, "480 km");
    lv_obj_set_style_text_color(label_range, COL_BLACK, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_range, &lv_font_montserrat_12, LV_STATE_DEFAULT);
    lv_obj_align_to(label_range, tra_bot, LV_ALIGN_RIGHT_MID, -30, 0);

    create_left_meter(scr);
    create_right_meter(scr);
    create_speed_display(scr);
    create_bottom_bar(scr);

    lv_timer_create(timer_cb, 100, NULL);
}

/*=== 对外设置接口 ===*/
void gui_set_speed(int val)
{
    char b[8];
    lv_snprintf(b, sizeof(b), "%d", val);
    lv_label_set_text(label_speed, b);
}

void gui_set_rpm(int val)
{
    lv_meter_set_indicator_end_value(meter_left, arc_left, val);
    char b[8];
    lv_snprintf(b, sizeof(b), "%d", val);
    lv_label_set_text(label_rpm_val, b);
}

void gui_set_fuel(int val)
{
    lv_meter_set_indicator_end_value(meter_right, arc_right, val);
    char b[8];
    lv_snprintf(b, sizeof(b), "%d%%", val);
    lv_label_set_text(label_fuel_val, b);
}

void gui_set_temp(int val)
{
    char b[8];
    lv_snprintf(b, sizeof(b), "%dC", val);
    lv_label_set_text(label_temp_val, b);
}

void gui_set_time(int h, int m, int s)
{
    char b[16];
    lv_snprintf(b, sizeof(b), "%02d:%02d:%02d", h, m, s);
    lv_label_set_text(label_time, b);
}

void gui_set_date(int y, int m, int d)
{
    char b[16];
    lv_snprintf(b, sizeof(b), "%04d-%02d-%02d", y, m, d);
    lv_label_set_text(label_date, b);
}

void gui_set_range(int val)
{
    char b[16];
    lv_snprintf(b, sizeof(b), "%d km", val);
    lv_label_set_text(label_range, b);
}

/* 图标改色: state=1 用 warn_clr, 否则青色 */
static void icon_set_color(lv_obj_t *img, int state, lv_color_t warn_clr)
{
    lv_color_t c = state ? warn_clr : COL_ACCENT;
    lv_obj_set_style_img_recolor(img, c, LV_STATE_DEFAULT);
}

void gui_set_icon_car(int state) { icon_set_color(img_car, state, COL_RED); }
void gui_set_icon_seatbelt(int state) { icon_set_color(img_seatbelt, state, COL_RED); }
void gui_set_icon_warning(int state) { icon_set_color(img_warning, state, COL_YELLOW); }
void gui_set_icon_brake(int state) { icon_set_color(img_brake, state, COL_RED); }
void gui_set_wifi(int connected)
{
    if (img_wifi == NULL)
        return;
    lv_obj_set_style_opa(img_wifi, connected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_STATE_DEFAULT);
}
void gui_set_ota_available(int available)
{
    if (label_ota == NULL)
        return;

    /* 下载中拒绝修改 UI 状态 */
    if (ota_ui_downloading)
        return;

    if (available)
    {
        lv_obj_clear_flag(label_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota_download)
            lv_obj_clear_flag(btn_ota_download, LV_OBJ_FLAG_HIDDEN);
        if (label_ota_progress)
            lv_obj_add_flag(label_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(label_ota, LV_OBJ_FLAG_HIDDEN);
        if (btn_ota_download)
            lv_obj_add_flag(btn_ota_download, LV_OBJ_FLAG_HIDDEN);
        if (label_ota_progress)
            lv_obj_add_flag(label_ota_progress, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 下载失败时调用 — 隐藏进度, 恢复 GO 按钮
 */
void gui_ota_download_failed(void)
{
    ota_ui_downloading = 0;

    if (label_ota_progress)
        lv_obj_add_flag(label_ota_progress, LV_OBJ_FLAG_HIDDEN);
    if (btn_ota_download)
        lv_obj_clear_flag(btn_ota_download, LV_OBJ_FLAG_HIDDEN);
}

void gui_set_ota_download_callback(void (*cb)(void))
{
    ota_download_cb = cb;
}
void gui_set_version(const char *ver) { lv_label_set_text(label_ver, ver); }
