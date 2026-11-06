/*
 * ui_nav.h —— 通用"底部上滑唤出 返回桌面"导航（可复用）
 * ---------------------------------------------------------
 * 给任意屏幕挂上后：从该屏底部上滑/轻点 → 底部浮现"⌂ 返回桌面"按钮 → 点击回桌面。
 *
 * ★ 以后新增一个 App/界面，只需在自己建屏时调用一行：
 *     lv_obj_t *scr = lv_obj_create(NULL);   // 你的屏幕
 *     ... 构建你的界面控件 ...
 *     ui_nav_add_home_gesture(scr);          // 挂上底部手势(自动隐现)
 * 若你的界面会把某些控件提到最顶层(例如聊天全屏)，再调 ui_nav_raise() 保持手势可点。
 */
#ifndef UI_NAV_H
#define UI_NAV_H

#include "../lvgl/lvgl.h"

/* 给指定屏幕挂上 底部上滑唤出"返回桌面" 的导航（同一屏挂一次即可） */
void ui_nav_add_home_gesture(lv_obj_t * scr);

/* 把所有已挂载的导航手势条/按钮提到最顶层（防止被全屏等控件盖住） */
void ui_nav_raise(void);

#endif /* UI_NAV_H */
