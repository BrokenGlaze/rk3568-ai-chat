/*
 * ui_home.c —— 手机式桌面主界面（Launcher）
 * ---------------------------------------------------------
 * 登录成功后进入；桌面是一排"App 图标"，点击图标进入对应界面。
 *
 * ★ 如何添加图标：往下方 home_apps[] 数组加一行即可，例如：
 *     { "相册", LV_SYMBOL_IMAGE, NULL, my_gallery_screen },
 *   - name      : 图标下方显示的名字
 *   - symbol    : 图标，用 LVGL 内置符号(LV_SYMBOL_*)作图形（无需图片文件）
 *   - img_path  : 或给一张 PNG/JPG 图标路径(./pic/xxx.png)，此时 symbol 填 NULL
 *   - on_click  : 点击后进入哪个界面（写一个函数调用对应屏的入口即可）
 */
#include "src/deepseek_chat_UI/ui_shared.h"   /* color_map/font/style + dp_chat_screen/ui_home_show */

/* ---------- 桌面 App 定义 ---------- */
typedef struct {
    const char * name;         /* 图标下方名字 */
    const char * symbol;       /* 默认字体符号图标(LV_SYMBOL_*)，用图片时填 NULL */
    const char * img_path;     /* PNG/JPG 图标路径，用图片时填并置 symbol=NULL */
    void (*on_click)(void);    /* 点击进入的界面 */
} HomeApp;

static void app_enter_settings(void);   /* 示例第 2 个 App（占位） */

static const HomeApp home_apps[] = {

    { "DeepSeek", LV_SYMBOL_IMAGE,    "./pic/dp_ui.jpg", dp_chat_screen        },
    { "设置",      LV_SYMBOL_SETTINGS, NULL, app_enter_settings },

};

#define HOME_APP_NUM (sizeof(home_apps) / sizeof(home_apps[0]))

static lv_obj_t *home_screen = NULL;

/* 示例第 2 个 App：仅演示（可改成进入你自己的界面） */
static void app_enter_settings(void)
{
    LV_LOG_USER("设置 App：示例占位，可在 home_apps[] 里换成你自己的入口函数");
}

/* 图标点击：进入对应 App */
static void app_icon_click_cb(lv_event_t * e)
{
    const HomeApp *app = (const HomeApp *)lv_event_get_user_data(e);
    if (app != NULL && app->on_click != NULL)
        app->on_click();
}

/* 显示桌面（首次调用会创建，之后直接切回） */
void ui_home_show(void)
{
    if (home_screen != NULL)
    {
        lv_screen_load(home_screen);
        return;
    }

    home_screen = lv_obj_create(NULL);
    lv_screen_load(home_screen);

    /* 桌面背景 */
    lv_obj_set_style_bg_color(home_screen, lv_color_hex(0x10151c), 0);
    lv_obj_set_style_bg_opa(home_screen, LV_OPA_COVER, 0);

    /* 顶部状态栏文字 */
    lv_obj_t *status = lv_label_create(home_screen);
    lv_obj_set_style_text_font(status, font_sk_24, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x9aa5b1), 0);
    lv_label_set_text(status, "桌面");
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 10);

    /* 图标网格容器：flex 自动换行排列 */
    lv_obj_t * grid = lv_obj_create(home_screen);
    lv_obj_set_size(grid, LV_HOR_RES, LV_VER_RES - 40);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 16, 0);
    lv_obj_set_style_pad_column(grid, 24, 0);
    lv_obj_set_style_pad_row(grid, 24, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 逐个创建 App 图标 */
    for (int i = 0; i < (int)HOME_APP_NUM; i++)
    {
        const HomeApp *app = &home_apps[i];

        /* 一个 App = 一个"格子"（图标按钮 + 名字，纵向排列） */
        lv_obj_t * cell = lv_obj_create(grid);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(cell, 110, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_pad_row(cell, 8, 0);
        lv_obj_set_layout(cell, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        /* 图标按钮（圆形底色 + 图形） */
        lv_obj_t * icon = lv_button_create(cell);
        lv_obj_set_size(icon, 84, 84);
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x223041), 0);
        lv_obj_set_style_radius(icon, 42, 0);            /* 圆形 */
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_add_event_cb(icon, app_icon_click_cb, LV_EVENT_CLICKED, (void *)app);

        if (app->img_path != NULL)
        {
            /* 用图片当图标：裁剪到圆形按钮内，超出部分自动消除 */
            lv_obj_set_style_clip_corner(icon, true, 0);        /* 按圆角轮廓裁剪子控件 */
            lv_obj_remove_flag(icon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

            lv_obj_t * img = lv_image_create(icon);
            lv_image_set_src(img, app->img_path);
            lv_obj_center(img);
        }
        else
        {
            /* 用默认字体符号当图标（无图片依赖，方便） */
            lv_obj_t * sym = lv_label_create(icon);
            lv_obj_set_style_text_font(sym, lv_font_get_default(), 0);
            lv_obj_set_style_text_color(sym, lv_color_white(), 0);
            lv_label_set_text(sym, app->symbol ? app->symbol : LV_SYMBOL_IMAGE);
            lv_obj_center(sym);
        }

        /* 图标名字 */
        lv_obj_t * name = lv_label_create(cell);
        lv_obj_set_style_text_font(name, font_sk_24, 0);
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_label_set_text(name, app->name);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    }
}
