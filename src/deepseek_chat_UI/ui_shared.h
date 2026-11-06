/*
 * ui_shared.h —— 三个 UI 子模块共享的接口
 * 模块划分：
 *   ui_output.c  文本输出框（消息列表/思维链/全屏/停止/加载动画）
 *   ui_input.c   文本输入框（拼音输入法 + 键盘）
 *   ui_model.c   模型切换列表（左侧）
 * 共享的 color_map / 字体 / style 定义在 screen.c，这里只做 extern 声明。
 */
#ifndef UI_SHARED_H
#define UI_SHARED_H

#include "lvgl/lvgl.h"

/* ===== screen.c 定义并共享的全局 ===== */
extern lv_color_t color_map[];
extern lv_font_t *font_sk_32;
extern lv_font_t *font_sk_24;
extern lv_style_t style;

/* ===== 三个 UI 子模块入口（由 screen.c 的 main_screen_control 调用） ===== */
void ui_output_create(void);   /* 文本输出框 */
void ui_model_create(void);    /* 模型切换列表 */
void ui_input_create(void);    /* 文本输入框 */

/* 输出模块提供给输入模块：发送一条用户消息 */
void ui_output_send_message(lv_obj_t *ta);

/* 模型模块：把模型按钮/面板提到最顶层（全屏聊天时保证可点） */
void ui_model_raise(void);

/* ===== 界面导航（screen.c / ui_home.c 定义） ===== */
void dp_chat_screen(void);    /* 进入 DeepSeek 聊天屏（screen.c，首次创建、之后切回） */
void ui_home_show(void);      /* 进入/回到手机式桌面主界面（ui_home.c） */

#endif /* UI_SHARED_H */
