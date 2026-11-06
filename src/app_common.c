/*
 * app_common.c —— 全局主题资源 + 通用控件辅助（定义）
 * -------------------------------------------------
 * 集中存放全应用共享的“皮肤”与基础控件工厂：
 *   color_map 调色板 / font_sk_32、font_sk_24 字体 / style 默认样式 / init_freetype_font
 *   hide_timer_cb / create_label_out / create_confirm_button / keyborad_style_black
 */
#include "src/app_common.h"

/* ===== 全局主题资源 ===== */
lv_color_t color_map[] = {
    LV_COLOR_MAKE(0x20, 0x20, 0x20),  // 0. (黑色)    #202020
    LV_COLOR_MAKE(0xff, 0xff, 0xff),  // 1. (白色)    #ffffff
    LV_COLOR_MAKE(0x50, 0x50, 0x50),  // 2. (白灰色)  #505050
    LV_COLOR_MAKE(0x30, 0x30, 0x30),  // 3. (黑灰色)  #303030
    LV_COLOR_MAKE(0x00, 0x78, 0xd7),  // 4. (蓝色)    #0078d7
    LV_COLOR_MAKE(0xe0, 0x16, 0x16)   // 5. (红色)    #e01616
};
lv_font_t *font_sk_32;
lv_font_t *font_sk_24;
lv_style_t style;

/* 字体初始化 */
void init_freetype_font(void)
{
    font_sk_32 = lv_freetype_font_create("./ttf/simkai.ttf",
                                          LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                          32,
                                          LV_FREETYPE_FONT_STYLE_NORMAL);

    font_sk_24 = lv_freetype_font_create("./ttf/simkai.ttf",
                                        LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                        24,
                                        LV_FREETYPE_FONT_STYLE_NORMAL);

    if(!font_sk_32)
    {
        LV_LOG_ERROR("freetype font create failed.");
        return;
    }
    if(!font_sk_24)
    {
        LV_LOG_ERROR("freetype font create failed.");
        return;
    }
    lv_style_init(&style);
    lv_style_set_text_font(&style, font_sk_32);   /* 默认样式用 32px 楷体（登录/聊天都靠它） */
}

/* ===== 通用控件辅助 ===== */
//延时删除提示
void hide_timer_cb(lv_timer_t * timer)
{
    lv_obj_t * label = (lv_obj_t *) lv_timer_get_user_data(timer);  /* 取出闹钟里藏的那个label */
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);   /* 藏起来 */
    lv_obj_del(label);                            /* 用完就删掉这个label，免得残留 */
    lv_timer_delete(timer);                       /* 删掉这个定时器 */
}

//创建字体标签
lv_obj_t *create_label_out(lv_obj_t *target, lv_align_t align,
                           int x, int y,
                           const char *txt, const lv_color_t color)
{
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_obj_add_style(label, &style, 0);
    lv_label_set_text(label, txt);
    lv_obj_set_style_text_color(label, color, 0);    // 设置文字颜色

    lv_obj_align_to(label, target, align, x, y);
    return label;
}

//创建确认按钮
lv_obj_t *create_confirm_button(lv_obj_t *parent, int size_x, int size_y, int x, int y,
                                lv_align_t align, const char *txt)     // 对齐参照物
{
    lv_obj_t * btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(btn, size_x, size_y);
    lv_obj_align_to(btn, parent, align, x, y);

    lv_obj_t * label = lv_label_create(btn);
    lv_obj_add_style(label, &style, 0);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
    return btn;
}

//深色键盘样式
void keyborad_style_black(lv_obj_t *kbd)
{
    static bool inited = false;
    static lv_style_t style_btn;
    static lv_style_t style_bg;
    static lv_style_t style_pressed;

    if(inited == false)
    {
        lv_style_init(&style_btn);
        lv_style_init(&style_bg);
        lv_style_init(&style_pressed);
        inited = true;
    }

    lv_style_set_bg_color(&style_btn, color_map[0]);        // 按钮背景色
    lv_style_set_text_color(&style_btn, color_map[1]);      // 按钮文字颜色
    lv_style_set_border_width(&style_btn, 1);               // 边框宽度
    lv_style_set_border_color(&style_btn, color_map[2]);    // 边框颜色
    lv_style_set_radius(&style_btn, 5);                     // 圆角半径
    lv_obj_add_style(kbd, &style_btn, LV_PART_ITEMS);

    /*设置键盘背景样式*/
    lv_style_set_bg_color(&style_bg, color_map[3]);          // 背景颜色
    lv_style_set_pad_all(&style_bg, 5);                      // 内边距
    lv_obj_add_style(kbd, &style_bg, 0);

    /*设置按下状态样式*/
    lv_style_set_bg_color(&style_pressed, color_map[4]);     // 按下背景色
    lv_obj_add_style(kbd, &style_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
}
