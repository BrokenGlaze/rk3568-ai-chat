/*
 * ui_quick_settings.h —— 通用"顶部下拉 → 快捷设置(亮度)"面板（可复用）
 * ---------------------------------------------------------
 * 给任意屏幕挂上后：从该屏顶部往下滑 → 拉出快捷设置面板（亮度滑动条，
 * 经 /sys/class/backlight 控制背光）→ 上滑/点▲/轻点手柄收起。
 *
 * ★ 以后新增一个 App/界面，只需在自己建屏时调用一行：
 *     lv_obj_t *scr = lv_obj_create(NULL);   // 你的屏幕
 *     ... 构建你的界面控件 ...
 *     ui_quick_settings_add(scr);            // 挂上顶部下拉亮度面板
 * 若你的界面会把某些控件提到最顶层(例如聊天全屏)，再调
 * ui_quick_settings_raise() 保持下拉手势可点。
 */
#ifndef UI_QUICK_SETTINGS_H
#define UI_QUICK_SETTINGS_H

#include "../lvgl/lvgl.h"

/* 给指定屏幕挂上"顶部下拉 → 快捷设置(亮度)"面板（同一屏挂一次即可） */
void ui_quick_settings_add(lv_obj_t * scr);

/* 把所有已挂载的下拉手柄/面板提到最顶层（防止被全屏等控件盖住） */
void ui_quick_settings_raise(void);

#endif /* UI_QUICK_SETTINGS_H */
