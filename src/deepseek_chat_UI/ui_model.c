/*
 * ui_model.c —— 模型切换列表模块
 * 负责：左侧"模型"按钮、模型列表面板（默认隐藏、可上下滑动）、模型切换。
 * 由 screen.c 的 main_screen_control() 调用 ui_model_create() 创建。
 */
#include "src/deepseek_chat_UI/ui_shared.h"
#include "dp_chat.h"

#include <stdint.h>   /* intptr_t */

/* ===== 模型列表（想增加/修改可选模型，直接改这个数组即可） ===== */
static const char * const model_list[] = {
    "deepseek-v4-flash",   /* 默认 */
    "deepseek-v4-pro",       /* 通用对话 */
    "deepseek-v4-flash-vision-exp",   /* 深度推理 */
};
#define MODEL_NUM (sizeof(model_list) / sizeof(model_list[0]))

static lv_obj_t *model_btn    = NULL;     /* 左侧 展开/收起 按钮 */
static lv_obj_t *model_panel  = NULL;     /* 模型列表面板（默认隐藏） */
static lv_obj_t *model_title  = NULL;     /* 面板标题，显示当前模型 */
static lv_obj_t *model_btns[MODEL_NUM];   /* 各模型按钮，用于高亮 */
static int current_model_index = 0;

/* 点击某个模型项：切换模型、高亮、收起面板 */
static void model_item_cb(lv_event_t * e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= (int)MODEL_NUM) return;

    dp_chat_set_model(model_list[idx]);
    current_model_index = idx;

    /* 更新高亮 */
    for (int i = 0; i < (int)MODEL_NUM; i++)
    {
        if (model_btns[i] == NULL) continue;
        if (i == idx)
            lv_obj_set_style_bg_color(model_btns[i], color_map[4], 0);   /* 蓝=选中 */
        else
            lv_obj_set_style_bg_color(model_btns[i], color_map[0], 0);   /* 黑=未选 */
    }
    if (model_title != NULL)
        lv_label_set_text_fmt(model_title, "当前: %s", model_list[idx]);

    /* 选择后收起面板 */
    if (model_panel != NULL)
        lv_obj_add_flag(model_panel, LV_OBJ_FLAG_HIDDEN);
}

/* 点击左侧按钮：展开/收起面板 */
static void model_toggle_cb(lv_event_t * e)
{
    if (model_panel == NULL) return;
    if (lv_obj_has_flag(model_panel, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(model_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(model_panel);
    }
    else
    {
        lv_obj_add_flag(model_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 把模型按钮/面板提到最顶层（全屏聊天时保证可点） */
void ui_model_raise(void)
{
    if (model_btn != NULL)   lv_obj_move_foreground(model_btn);
    if (model_panel != NULL) lv_obj_move_foreground(model_panel);
}

/* 创建左侧模型选择面板（默认隐藏，点左侧"模型"按钮展开） */
void ui_model_create(void)
{
    /* 左侧 展开/收起 按钮 */
    model_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(model_btn, 48, 100);
    lv_obj_align(model_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(model_btn, color_map[4], 0);
    lv_obj_set_style_radius(model_btn, 0, 0);
    lv_obj_set_style_border_width(model_btn, 0, 0);
    lv_obj_t *btn_lbl = lv_label_create(model_btn);
    lv_obj_add_style(btn_lbl, &style, 0);
    lv_obj_set_style_text_font(btn_lbl, font_sk_24, 0);
    lv_obj_set_style_text_color(btn_lbl, color_map[1], 0);
    lv_label_set_text(btn_lbl, "模\n型");   /* 竖排，窄按钮 */
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(model_btn, model_toggle_cb, LV_EVENT_CLICKED, NULL);

    /* 面板：初始隐藏；高度≈内容高度，超过上限(220)则内部可上下滑动 */
    int panel_h = 80 + (int)MODEL_NUM * 48;           /* 预留长模型名换行空间 */
    if (panel_h > 220) panel_h = 220;                 /* 上限，超出滚动显示 */
    model_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(model_panel, 260, panel_h);
    lv_obj_align_to(model_panel, model_btn, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_set_scrollbar_mode(model_panel, LV_SCROLLBAR_MODE_AUTO);   /* 可上下滑动 */
    lv_obj_set_style_bg_color(model_panel, color_map[3], 0);
    lv_obj_set_style_bg_opa(model_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(model_panel, 6, 0);
    lv_obj_set_style_pad_all(model_panel, 8, 0);
    lv_obj_set_style_pad_row(model_panel, 4, 0);
    lv_obj_set_style_border_width(model_panel, 1, 0);
    lv_obj_set_style_border_color(model_panel, color_map[2], 0);
    lv_obj_add_flag(model_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_layout(model_panel, LV_LAYOUT_FLEX);          /* 纵向排列 */
    lv_obj_set_flex_flow(model_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(model_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 标题：显示当前模型（flex 中自然靠左，超长自动换行） */
    model_title = lv_label_create(model_panel);
    lv_obj_add_style(model_title, &style, 0);
    lv_label_set_long_mode(model_title, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(model_title, LV_PCT(100));
    lv_obj_set_style_text_font(model_title, font_sk_24, 0);
    lv_obj_set_style_text_color(model_title, lv_color_white(), 0);
    lv_label_set_text_fmt(model_title, "当前: %s", dp_chat_get_model());

    /* 模型列表按钮（flex 纵向排列，文字左对齐；超长自动换行显示完整） */
    for (int i = 0; i < (int)MODEL_NUM; i++)
    {
        lv_obj_t *b = lv_button_create(model_panel);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);        /* 高度随内容自适应(长名换行) */
        lv_obj_set_style_pad_all(b, 6, 0);            /* 上下留白，防文字裁剪 */
        lv_obj_set_style_bg_color(b, color_map[0], 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_width(b, 0, 0);

        lv_obj_t *lbl = lv_label_create(b);
        lv_obj_add_style(lbl, &style, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);   /* 超长换行 */
        lv_obj_set_width(lbl, LV_PCT(100));                     /* 限制宽度以换行 */
        lv_obj_set_style_text_font(lbl, font_sk_24, 0);
        lv_obj_set_style_text_color(lbl, color_map[1], 0);
        lv_label_set_text(lbl, model_list[i]);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);    /* 文字左对齐 */
        lv_obj_add_event_cb(b, model_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        model_btns[i] = b;
    }

    /* 默认高亮第 0 项 */
    current_model_index = 0;
    if (model_btns[0] != NULL)
        lv_obj_set_style_bg_color(model_btns[0], color_map[4], 0);

    /* 保持最顶层，盖住聊天区 */
    lv_obj_move_foreground(model_btn);
}
