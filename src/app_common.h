/*
 * app_common.h —— 全局主题资源 + 通用控件辅助（声明）
 * 定义见 src/app_common.c。所有 UI 模块通过它拿 调色板/字体/style 和通用控件工厂。
 */
#ifndef APP_COMMON_H
#define APP_COMMON_H

#include "lvgl/lvgl.h"

/* ===== 全局主题资源 ===== */
extern lv_color_t color_map[];
extern lv_font_t *font_sk_32;   /* 32px 楷体(聊天/主文字) */
extern lv_font_t *font_sk_24;   /* 24px 楷体(面板/小字) */
extern lv_style_t style;        /* 默认样式(32px 楷体) */

/* 初始化全局字体 + 默认样式（应用启动时调用一次） */
void init_freetype_font(void);

/* ===== 通用控件辅助 ===== */
void hide_timer_cb(lv_timer_t * timer);                 /* 延时隐藏并删除 label */
lv_obj_t *create_label_out(lv_obj_t *target, lv_align_t align,
                           int x, int y,
                           const char *txt, const lv_color_t color);
lv_obj_t *create_confirm_button(lv_obj_t *parent, int size_x, int size_y,
                                int x, int y, lv_align_t align, const char *txt);
void keyborad_style_black(lv_obj_t *kbd);               /* 深色键盘样式 */

#endif /* APP_COMMON_H */
