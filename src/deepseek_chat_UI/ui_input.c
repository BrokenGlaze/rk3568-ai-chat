/*
 * ui_input.c —— 文本输入框模块
 * 负责：拼音输入法(lv_ime_pinyin)、输入框(ta1)、键盘(kb)、候选面板。
 * 键盘 READY(发送) 时调用 ui_output_send_message() 交给输出模块。
 * 由 screen.c 的 main_screen_control() 调用 ui_input_create() 创建。
 */
#include "src/deepseek_chat_UI/ui_shared.h"
#include "src/app_common.h"     /* keyborad_style_black */
#include "src/pinyin/pinyin_dict.h"    /* ds_pinyin_dict */

/* ================= 输入区全局状态（本模块私有） ================= */
static lv_obj_t *ime_cand_panel = NULL;  /* 拼音候选面板（随键盘一起显隐） */

/* 隐藏拼音候选面板（键盘隐藏时一起隐藏） */
static void hide_ime_panel(void)
{
    if (ime_cand_panel != NULL)
        lv_obj_add_flag(ime_cand_panel, LV_OBJ_FLAG_HIDDEN);
}

static void ta_event_pinyin_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target_obj(e);
    lv_obj_t * kb = (lv_obj_t *) lv_event_get_user_data(e);

    if(code == LV_EVENT_FOCUSED)
    {
        lv_textarea_set_placeholder_text(ta, "");        /* 点击输入框 → 占位符消失 */
        if(kb != NULL)
        {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);  // 显示键盘
            lv_obj_move_foreground(kb);                 /* 键盘永远最顶(不被模型面板等盖住) */
            if(ime_cand_panel != NULL)
                lv_obj_move_foreground(ime_cand_panel); /* 候选面板随键盘一起最顶(先提键盘再提它, 候选条在最上) */
        }
    }
    else if(code == LV_EVENT_DEFOCUSED)
    {
        lv_textarea_set_placeholder_text(ta, "请在此输入文本");  /* 失焦 → 恢复占位符 */
        lv_obj_set_style_text_opa(ta, LV_OPA_60, LV_PART_TEXTAREA_PLACEHOLDER);   //占位符透明度 60%

        if(kb != NULL)
        {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);  // 隐藏键盘
        }
        hide_ime_panel();                              // 候选面板一起隐藏
    }
    else if(code == LV_EVENT_CANCEL)
    {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        hide_ime_panel();                              // 候选面板一起隐藏
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);     //取消聚焦状态
        lv_indev_reset(NULL, ta);                      //释放对文本框的持有状态
    }
    else if(code == LV_EVENT_READY)
    {
        ui_output_send_message(ta);                    /* 交给输出模块发送 */

        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        hide_ime_panel();                              // 候选面板一起隐藏
        lv_indev_reset(NULL, ta);
        lv_textarea_set_placeholder_text(ta, "请在此输入文本");
    }
}

/* 创建拼音输入法 + 输入框 + 键盘 */
void ui_input_create(void)
{
    lv_obj_t * pinyin_ime = lv_ime_pinyin_create(lv_screen_active());
    lv_obj_set_style_text_font(pinyin_ime, font_sk_24, 0);
    lv_ime_pinyin_set_dict(pinyin_ime, ds_pinyin_dict);   // 使用自定义大字典(425音节)

    //创建文本输入框
    lv_obj_t * ta1 = lv_textarea_create(lv_screen_active());
    //样式
    lv_textarea_set_one_line(ta1, true);    //单行显示
    lv_textarea_set_placeholder_text(ta1, "请在此输入文本");   //占位提示
    lv_obj_set_style_text_opa(ta1, LV_OPA_60, LV_PART_TEXTAREA_PLACEHOLDER);   //占位符透明度 60%
    lv_obj_set_style_text_font(ta1, font_sk_32, 0);   //字体

    //输入框做成与 chat_bubble.jpg 适配的聊天框（尺寸从图片读取）
    lv_image_header_t img_hdr;
    if(lv_image_decoder_get_info("./pic/chat_bubble.jpg", &img_hdr) == LV_RESULT_OK)
    {
        lv_obj_set_size(ta1, img_hdr.w, img_hdr.h);                 /* 与图片同尺寸 */
    }
    lv_obj_set_style_bg_image_src(ta1, "./pic/chat_bubble.jpg", 0); /* 图片作背景 */
    lv_obj_set_style_bg_image_opa(ta1, LV_OPA_COVER, 0);            /* 图片不透明 */
    lv_obj_set_style_bg_opa(ta1, LV_OPA_TRANSP, 0);                 /* 去掉纯色底，露出图片 */
    lv_obj_set_style_border_width(ta1, 0, 0);                       /* 去掉边框(图片自带) */
    lv_obj_set_style_radius(ta1, 0, 0);                             /* 去掉圆角(图片自带) */
    lv_obj_set_style_text_color(ta1, lv_color_white(), 0);          /* 白字 */
    lv_obj_set_style_pad_all(ta1, 12, 0);                           /* 内边距 */
    //位置
    lv_obj_align(ta1, LV_ALIGN_TOP_LEFT, 0, 0);

    //创造拼音输入键盘
    lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
    lv_ime_pinyin_set_keyboard(pinyin_ime, kb);
    lv_keyboard_set_textarea(kb, ta1);
    keyborad_style_black(kb);

    lv_obj_add_event_cb(ta1, ta_event_pinyin_cb, LV_EVENT_ALL, kb);

    //设置键盘候选面板样式
    ime_cand_panel = lv_ime_pinyin_get_cand_panel(pinyin_ime);
    lv_obj_t *cand_panel = ime_cand_panel;
    lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(10));
    lv_obj_set_style_bg_color(cand_panel, color_map[3], 0);   /* 面板底色 #303030 */
    lv_obj_set_style_bg_opa(cand_panel, LV_OPA_COVER, 0);     /* 不透明 */
    lv_obj_set_style_border_width(cand_panel, 0, 0);          /* 去掉边框 */
    lv_obj_set_style_bg_color(cand_panel, color_map[0], LV_PART_ITEMS);
    lv_obj_set_style_text_color(cand_panel, color_map[1], LV_PART_ITEMS);
    //按下反馈
    lv_obj_set_style_bg_color(cand_panel, color_map[4], LV_PART_ITEMS | LV_STATE_PRESSED);
    //位置
    lv_obj_align_to(cand_panel, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);

    /* 进入界面时不自动弹键盘：取消聚焦 + 隐藏键盘 */
    lv_obj_remove_state(ta1, LV_STATE_FOCUSED);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_indev_reset(NULL, ta1);
}
