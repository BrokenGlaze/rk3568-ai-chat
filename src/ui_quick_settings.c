/*
 * ui_quick_settings.c —— 通用"顶部下拉 → 快捷设置(亮度)"面板（可复用）
 * ---------------------------------------------------------
 * 用法见 ui_quick_settings.h。背光(sysfs)是全局共享硬件；每个屏幕各挂一份
 * 独立的面板实例(记录在 g_qs[] 里)，互不干扰，可同时挂多屏。
 */
#include "ui_quick_settings.h"
#include "deepseek_chat_UI/ui_shared.h"   /* color_map, font_sk_24 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ================= 背光控制（sysfs，全局单例） ================= */
static char g_bl_path[160] = "";   /* /sys/class/backlight/<dev>/brightness */
static int  g_bl_max = 255;
static bool g_bl_inited = false;

/* RK3568 该板背光极性相反：sysfs 值越大屏幕越暗。
 * 1=反转（滑条调大=变亮），0=不反转。换板子若方向不对改这里即可。 */
#define BL_INVERT 1

static void backlight_init(void)
{
    g_bl_inited = true;
    DIR *dir = opendir("/sys/class/backlight");
    if (dir == NULL) {
        LV_LOG_WARN("no /sys/class/backlight");
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (ent->d_name[0] == '.') continue;

        char max_path[192];
        snprintf(max_path, sizeof(max_path),
                 "/sys/class/backlight/%s/max_brightness", ent->d_name);

        FILE *f = fopen(max_path, "r");
        if (f != NULL)
        {
            int m = 0;
            if (fscanf(f, "%d", &m) == 1 && m > 0)
                g_bl_max = m;
            fclose(f);
        }

        snprintf(g_bl_path, sizeof(g_bl_path),
                 "/sys/class/backlight/%s/brightness", ent->d_name);
        LV_LOG_USER("backlight: %s (max=%d)", g_bl_path, g_bl_max);
        break;   /* 用第一个设备 */
    }
    closedir(dir);
}

/* 读当前亮度，返回 0..100（数值越大越亮） */
static int backlight_read_percent(void)
{
    if (g_bl_path[0] == '\0') return 100;
    FILE *f = fopen(g_bl_path, "r");
    if (f == NULL) return 100;
    int v = 0;
    if (fscanf(f, "%d", &v) != 1) v = 0;
    fclose(f);
    if (g_bl_max <= 0) return 0;
    int pct = (int)(((long)v * 100 + g_bl_max / 2) / g_bl_max);
#if BL_INVERT
    return 100 - pct;          /* 极性相反：sysfs 大=暗，反转后大=亮 */
#else
    return pct;
#endif
}

/* 写亮度，入参 0..100（数值越大越亮） */
static void backlight_write_percent(int percent)
{
    if (g_bl_path[0] == '\0') return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
#if BL_INVERT
    percent = 100 - percent;   /* 极性相反：界面亮度先反转再写 sysfs */
#endif
    int v = (int)((long)percent * g_bl_max / 100);
    if (v < 0) v = 0;
    if (v > g_bl_max) v = g_bl_max;

    FILE *f = fopen(g_bl_path, "w");
    if (f == NULL) {
        LV_LOG_WARN("cannot write %s", g_bl_path);
        return;
    }
    fprintf(f, "%d\n", v);
    fclose(f);
}

/* ================= 每屏一个面板实例 ================= */
#define HANDLE_H    14       /* 顶部手柄高度 */
#define PANEL_H     190      /* 面板高度 */
#define DRAG_OPEN   40       /* 下拉超过该像素打开 */
#define DRAG_CLOSE  -40      /* 上滑超过该像素关闭 */
#define UI_QS_MAX   8        /* 最多同时挂载的屏幕数 */

typedef struct {
    lv_obj_t * handle;     /* 顶部透明下拉手柄 */
    lv_obj_t * panel;      /* 下拉面板 */
    lv_obj_t * slider;     /* 亮度滑动条 */
    lv_obj_t * val_label;  /* 亮度百分比文字 */
} QsInst;

static QsInst g_qs[UI_QS_MAX];
static int    g_qs_cnt = 0;

/* 面板滑入/滑出动画 */
static void qs_anim(lv_obj_t *obj, int from, int to)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void qs_open(QsInst * ins)
{
    if (ins->panel == NULL) return;
    lv_obj_move_foreground(ins->panel);
    lv_obj_move_foreground(ins->handle);
    if (lv_obj_get_y(ins->panel) < 0)
        qs_anim(ins->panel, lv_obj_get_y(ins->panel), 0);
}

static void qs_close(QsInst * ins)
{
    if (ins->panel == NULL) return;
    if (lv_obj_get_y(ins->panel) > -PANEL_H)
        qs_anim(ins->panel, lv_obj_get_y(ins->panel), -PANEL_H);
}

/* 收起按钮 */
static void close_cb(lv_event_t * e)
{
    QsInst * ins = lv_event_get_user_data(e);
    if (ins != NULL) qs_close(ins);
}

/* 亮度滑动条变化：写背光 + 更新百分比文字 */
static void slider_cb(lv_event_t * e)
{
    QsInst * ins = lv_event_get_user_data(e);
    if (ins == NULL || ins->slider == NULL) return;
    int32_t v = lv_slider_get_value(ins->slider);
    backlight_write_percent((int)v);
    if (ins->val_label != NULL)
        lv_label_set_text_fmt(ins->val_label, "%d%%", (int)v);
}

/* 顶部手柄：下拉打开 / 上滑或轻点收起 */
static void handle_cb(lv_event_t * e)
{
    QsInst * ins = lv_event_get_user_data(e);
    if (ins == NULL) return;

    lv_event_code_t code = lv_event_get_code(e);
    static lv_point_t start;

    if (code == LV_EVENT_PRESSED)
    {
        lv_indev_get_point(lv_indev_active(), &start);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        lv_point_t cur;
        lv_indev_get_point(lv_indev_active(), &cur);
        int dy = cur.y - start.y;
        bool is_open = (lv_obj_get_y(ins->panel) >= 0);

        if (!is_open && dy > DRAG_OPEN)
            qs_open(ins);
        else if (is_open && dy < DRAG_CLOSE)
            qs_close(ins);
    }
    else if (code == LV_EVENT_RELEASED)
    {
        lv_point_t cur;
        lv_indev_get_point(lv_indev_active(), &cur);
        int dy = cur.y - start.y;
        if (dy > -15 && dy < 15)          /* 视为轻点：切换开/关 */
        {
            if (lv_obj_get_y(ins->panel) >= 0) qs_close(ins);
            else                               qs_open(ins);
        }
    }
}

/* 在某屏上创建一个 下拉面板 + 顶部手柄 实例 */
static void qs_create(lv_obj_t * scr, QsInst * ins)
{
    /* 顶部透明下拉手柄 */
    lv_obj_t * handle = lv_obj_create(scr);
    lv_obj_set_size(handle, LV_HOR_RES, HANDLE_H);
    lv_obj_set_pos(handle, 0, 0);
    lv_obj_add_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_set_style_radius(handle, 0, 0);
    lv_obj_set_style_pad_all(handle, 0, 0);
    ins->handle = handle;

    /* 下拉面板（初始隐藏在上方） */
    lv_obj_t * panel = lv_obj_create(scr);
    lv_obj_set_size(panel, LV_HOR_RES, PANEL_H);
    lv_obj_set_pos(panel, 0, -PANEL_H);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x282828), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    ins->panel = panel;

    /* 标题 */
    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, font_sk_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "快捷设置");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 收起按钮（右上角 ▲） */
    lv_obj_t *close_btn = lv_button_create(panel);
    lv_obj_set_size(close_btn, 40, 40);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, color_map[0], 0);
    lv_obj_set_style_radius(close_btn, 4, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_obj_set_style_text_font(close_lbl, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_label_set_text(close_lbl, LV_SYMBOL_UP);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, close_cb, LV_EVENT_CLICKED, ins);

    /* 亮度文字 */
    lv_obj_t *bright_lbl = lv_label_create(panel);
    lv_obj_set_style_text_font(bright_lbl, font_sk_24, 0);
    lv_obj_set_style_text_color(bright_lbl, lv_color_white(), 0);
    lv_label_set_text(bright_lbl, "亮度");
    lv_obj_align_to(bright_lbl, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 28);

    /* 亮度滑动条 */
    lv_obj_t * slider = lv_slider_create(panel);
    lv_obj_set_size(slider, LV_PCT(72), 12);
    lv_obj_align_to(slider, bright_lbl, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 18);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, backlight_read_percent(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, color_map[0], LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, color_map[4], LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, ins);
    ins->slider = slider;

    /* 百分比 */
    lv_obj_t * val_label = lv_label_create(panel);
    lv_obj_set_style_text_font(val_label, font_sk_24, 0);
    lv_obj_set_style_text_color(val_label, lv_color_white(), 0);
    lv_label_set_text_fmt(val_label, "%d%%", backlight_read_percent());
    lv_obj_align_to(val_label, slider, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    ins->val_label = val_label;

    /* 事件关联 */
    lv_obj_add_event_cb(handle, handle_cb, LV_EVENT_ALL, ins);
    lv_obj_move_foreground(handle);
}

/* 给指定屏幕挂上"顶部下拉 → 快捷设置" */
void ui_quick_settings_add(lv_obj_t * scr)
{
    if (scr == NULL) return;
    if (!g_bl_inited) backlight_init();
    if (g_qs_cnt >= UI_QS_MAX) return;

    QsInst * ins = &g_qs[g_qs_cnt];
    memset(ins, 0, sizeof(*ins));
    qs_create(scr, ins);
    g_qs_cnt++;
}

/* 所有已挂载的下拉手柄/面板提到最顶层 */
void ui_quick_settings_raise(void)
{
    for (int i = 0; i < g_qs_cnt; i++) {
        if (g_qs[i].handle != NULL) lv_obj_move_foreground(g_qs[i].handle);
        if (g_qs[i].panel  != NULL) lv_obj_move_foreground(g_qs[i].panel);
    }
}
