/*
 * ui_nav.c —— 通用"底部上滑唤出 返回桌面"导航（可复用）
 * ---------------------------------------------------------
 * 用法见 ui_nav.h。每个屏幕挂一次，控件建在该屏上；内部用 user_data
 * 记录"手势条↔返回按钮↔自动隐藏定时器"的关联，互不干扰，可同时挂多屏。
 */
#include "ui_nav.h"
#include "deepseek_chat_UI/ui_shared.h"   /* font_sk_24(中文) + ui_home_show(回桌面) */

#define NAV_SWIPE_UP  -40      /* 从底部上滑超过该像素唤出 */
#define NAV_HIDE_MS    8000    /* 唤出后几秒无操作自动隐藏 */
#define UI_NAV_MAX     8       /* 最多同时挂载的屏幕数 */

/* 记录所有已挂载实例，供 ui_nav_raise() 统一提到最顶层 */
typedef struct {
    lv_obj_t * handle;   /* 底部透明手势条 */
    lv_obj_t * home;     /* 返回桌面按钮 */
} NavPair;
static NavPair g_navs[UI_NAV_MAX];
static int     g_nav_cnt = 0;

/* 自动隐藏定时器：隐藏按钮 + 删掉自己 */
static void nav_hide_timer_cb(lv_timer_t * t)
{
    lv_obj_t * home = lv_timer_get_user_data(t);
    if (home != NULL) {
        lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_user_data(home, NULL);   /* 清除计时器记录 */
    }
    lv_timer_delete(t);
}

/* 取消某按钮已挂起的自动隐藏计时 */
static void nav_cancel_hide_timer(lv_obj_t * home)
{
    lv_timer_t * t = lv_obj_get_user_data(home);
    if (t != NULL) {
        lv_timer_delete(t);
        lv_obj_set_user_data(home, NULL);
    }
}

/* 唤出"返回桌面"按钮（先不排自动隐藏，抬手后再排，避免提前消失） */
static void nav_show_home(lv_obj_t * home)
{
    if (home == NULL) return;
    nav_cancel_hide_timer(home);
    lv_obj_clear_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(home);
}

/* 排定自动隐藏（NAV_HIDE_MS 后收起） */
static void nav_schedule_hide(lv_obj_t * home)
{
    if (home == NULL) return;
    nav_cancel_hide_timer(home);
    lv_obj_set_user_data(home, lv_timer_create(nav_hide_timer_cb, NAV_HIDE_MS, home));
}

/* 手动隐藏"返回桌面"按钮 */
static void nav_hide_home(lv_obj_t * home)
{
    if (home == NULL) return;
    nav_cancel_hide_timer(home);
    lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);
}

/* 点"返回桌面"按钮 → 回到手机式桌面 */
static void nav_home_click_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    LV_LOG_USER("ui_nav: 点击返回桌面");
    ui_home_show();
}

/* 底部手势条事件：从底部上滑 或 轻点 → 唤出/收起 */
static void nav_handle_cb(lv_event_t * e)
{
    lv_obj_t * home = lv_event_get_user_data(e);   /* 事件注册时传入了该实例的按钮 */
    if (home == NULL) return;

    lv_event_code_t code = lv_event_get_code(e);
    static lv_point_t start;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_active(), &start);
    }
    else if (code == LV_EVENT_PRESSING) {
        lv_point_t cur;
        lv_indev_get_point(lv_indev_active(), &cur);
        if (cur.y - start.y < NAV_SWIPE_UP)        /* 从底部上滑：先显示，不计时 */
            nav_show_home(home);
    }
    else if (code == LV_EVENT_RELEASED) {
        lv_point_t cur;
        lv_indev_get_point(lv_indev_active(), &cur);
        int dy = cur.y - start.y;
        if (dy < NAV_SWIPE_UP)                     /* 确为一次上滑：抬手后开始自动隐藏计时 */
            nav_schedule_hide(home);
        /* 其余(轻点/误触)一律忽略，不弹也不收，避免误操作 */
    }
}

/* 给指定屏幕挂上"底部上滑唤出返回桌面" */
void ui_nav_add_home_gesture(lv_obj_t * scr)
{
    if (scr == NULL) return;

    /* 底部透明手势条 */
    lv_obj_t * handle = lv_obj_create(scr);
    lv_obj_set_size(handle, LV_HOR_RES, 14);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_radius(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);

    /* "返回桌面"按钮（默认隐藏，唤出时显示；加大并上移，远离底部手势条便于点准） */
    lv_obj_t * home = lv_button_create(scr);
    lv_obj_set_size(home, 200, 56);
    lv_obj_align(home, LV_ALIGN_BOTTOM_MID, 0, -72);
    lv_obj_set_style_bg_color(home, lv_color_hex(0x303846), 0);
    lv_obj_set_style_radius(home, 22, 0);
    lv_obj_set_style_border_width(home, 0, 0);
    lv_obj_t * icon = lv_label_create(home);
    lv_obj_set_style_text_font(icon, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(icon, lv_color_white(), 0);
    lv_label_set_text(icon, LV_SYMBOL_HOME);            /* ⌂ 默认字体带符号 */
    lv_obj_center(icon);
    // lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);

    // lv_obj_t * text = lv_label_create(home);
    // lv_obj_set_style_text_font(text, font_sk_24, 0);
    // lv_obj_set_style_text_color(text, lv_color_white(), 0);
    // lv_label_set_text(text, "返回桌面");
    // lv_obj_center(text);

    lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);

    /* 关联事件：手势条 → home 按钮；点按钮 → 回桌面 */
    lv_obj_add_event_cb(handle, nav_handle_cb, LV_EVENT_ALL, home);
    lv_obj_add_event_cb(home, nav_home_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(handle);

    /* 记入实例表（供 ui_nav_raise 统一提升层级） */
    if (g_nav_cnt < UI_NAV_MAX) {
        g_navs[g_nav_cnt].handle = handle;
        g_navs[g_nav_cnt].home   = home;
        g_nav_cnt++;
    }
}

/* 所有已挂载的导航手势条/按钮提到最顶层 */
void ui_nav_raise(void)
{
    for (int i = 0; i < g_nav_cnt; i++) {
        if (g_navs[i].handle != NULL) lv_obj_move_foreground(g_navs[i].handle);
        if (g_navs[i].home != NULL)   lv_obj_move_foreground(g_navs[i].home);
    }
}
