/*
 * ui_output.c —— 文本输出框模块
 * 负责：消息列表(chat_body)、思维链可折叠显示、全屏/还原、停止生成按钮、
 *      加载动画(GIF)、流式轮询定时器。
 * 由 screen.c 的 main_screen_control() 调用 ui_output_create() 创建。
 */
#include "src/deepseek_chat_UI/ui_shared.h"
#include "src/ui_nav.h"                /* 底部上滑唤出"返回桌面"(可复用) */
#include "src/ui_quick_settings.h"     /* 顶部下拉快捷设置(可复用) */
#include "dp_chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 输出区全局状态（本模块私有） ================= */
static lv_obj_t *chat_cont;    // 输出区外层容器（用于全屏缩放）
static lv_obj_t *chat_body;    // 消息列表容器（纵向排列各消息块，外层滚动）
/* 当前这一轮回复的控件（流式追加用） */
static lv_obj_t *cur_content   = NULL;   // 正式回答 label
static lv_obj_t *cur_think_hdr = NULL;   // 思维链 展开/收起 按钮
static lv_obj_t *cur_think     = NULL;   // 思维链 label
static bool in_reasoning_phase = false;  // 是否正在输出思维链
static bool     chat_fullscreen = false; // 输出区是否处于全屏
static lv_obj_t *fs_btn;       // 全屏/还原 按钮（保持最顶层，保证可点还原）
static lv_obj_t *spinner;      // 加载动画(GIF)
static lv_obj_t *stop_btn;     // 停止生成按钮（右下角，生成时显示）
static bool need_assistant_prefix = false;   // 下一条流式内容需加 "DeepSeek: " 前缀

/* 过滤字体渲染不了的字符(表情/特殊符号→白框)，只保留 ASCII 和常用汉字/CJK 标点 */
static void chat_strip_to_renderable(const char * in, char * out, size_t out_cap)
{
    size_t o = 0;
    const unsigned char * p = (const unsigned char *)in;
    while(*p != '\0' && o + 4 < out_cap) {
        uint32_t cp;
        size_t len;
        if(*p < 0x80) { cp = *p; len = 1; }
        else if((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x1F) << 6) | (p[1] & 0x3F); len = 2;
        }
        else if((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3;
        }
        else if((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                 ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); len = 4;
        }
        else { p++; continue; }

        bool keep =
            (cp == '\n') ||                               /* 换行：消息分行必须保留 */
            (cp >= 0x20 && cp <= 0x7E) ||                 /* ASCII 可打印 */
            (cp >= 0x4E00 && cp <= 0x9FFF) ||             /* 常用汉字 */
            (cp >= 0x3000 && cp <= 0x303F) ||             /* CJK 标点 */
            (cp >= 0xFF00 && cp <= 0xFFEF) ||             /* 全角字符 */
            (cp == 0x2018 || cp == 0x2019 || cp == 0x201C || cp == 0x201D) ||
            (cp == 0x2026) ||                             /* … */
            (cp == 0x00B7) ||                             /* · */
            (cp == 0x2014 || cp == 0x2015);               /* — */
        if(keep) { memcpy(out + o, p, len); o += len; }
        p += len;
    }
    out[o] = '\0';
}

/* 追加文本到指定 label（label 不支持原生追加，重建字符串；过滤白框字符） */
static void label_append(lv_obj_t *lbl, const char *text)
{
    if(lbl == NULL || text == NULL) return;
    char clean[2048];
    chat_strip_to_renderable(text, clean, sizeof(clean));
    const char *old = lv_label_get_text(lbl);
    size_t need = strlen(old) + strlen(clean) + 1;
    char *new_txt = malloc(need);
    if(new_txt == NULL) return;
    sprintf(new_txt, "%s%s", old, clean);
    lv_label_set_text(lbl, new_txt);
    free(new_txt);
}

/* 在消息列表末尾新建一条 label（整宽、自动换行） */
static lv_obj_t *chat_body_add_label(const char *text)
{
    lv_obj_t *lbl = lv_label_create(chat_body);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_add_style(lbl, &style, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_pad_all(lbl, 6, 0);
    lv_label_set_text(lbl, text ? text : "");
    return lbl;
}

/* 键盘 READY 时调用（非阻塞发送）—— 供 ui_input.c 调用 */
void ui_output_send_message(lv_obj_t *ta)
{
    const char *txt = lv_textarea_get_text(ta);
    if (txt == NULL || txt[0] == '\0')
        return;

    /* 用户消息：单独一条 label（浅蓝区分） */
    lv_obj_t *lbl = chat_body_add_label("");
    lv_label_set_text_fmt(lbl, "我:\n%s", txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9de0ff), 0);
    lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);

    if (dp_send_message_async(txt) == 0)
    {
        lv_textarea_set_text(ta, "");                           // 清空输入框
        /* 准备新一轮回复（可含思维链 + 正式回答） */
        cur_content = NULL;
        cur_think_hdr = NULL;
        cur_think = NULL;
        in_reasoning_phase = false;
        need_assistant_prefix = true;                           // 下一条内容新建 DeepSeek 块
        if (spinner != NULL)
            lv_obj_remove_flag(spinner, LV_OBJ_FLAG_HIDDEN);    // 显示"加载中"
        if (stop_btn != NULL)
            lv_obj_remove_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);   // 显示"停止"按钮
    }
    else
    {
        chat_body_add_label("DeepSeek: [发送失败: 未初始化或队列已满]");
    }
}

/* ===================== 思维链：可折叠显示（仿 DeepSeek 官网） ===================== */
/* 点击思维链按钮：展开/收起 */
static void think_toggle_cb(lv_event_t * e)
{
    lv_obj_t *hdr = lv_event_get_current_target(e);
    lv_obj_t *tl = (lv_obj_t *)lv_event_get_user_data(e);
    if (tl == NULL) return;
    lv_obj_t *arrow = lv_obj_get_child(hdr, 1);   /* 第二个子控件是箭头 */
    if (lv_obj_has_flag(tl, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_clear_flag(tl, LV_OBJ_FLAG_HIDDEN);        /* 展开：思维链向下展开 */
        if (arrow != NULL) lv_label_set_text(arrow, LV_SYMBOL_DOWN);    /* 展开 ▾ */
        /* 滚动到思维链可见（跟随向下展开），不要跳到整个对话最底部 */
        lv_obj_update_layout(chat_cont);
        lv_obj_scroll_to_view(tl, LV_ANIM_OFF);
    }
    else
    {
        lv_obj_add_flag(tl, LV_OBJ_FLAG_HIDDEN);          /* 收起 */
        if (arrow != NULL) lv_label_set_text(arrow, LV_SYMBOL_RIGHT);   /* 收起 ▸ */
        lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

/* 开始输出思维链：创建 展开/收起 按钮 + 思维链 label（实时展开显示） */
static void start_reasoning(void)
{
    if (chat_body == NULL) return;

    cur_think_hdr = lv_button_create(chat_body);
    lv_obj_set_size(cur_think_hdr, LV_PCT(100), 34);
    lv_obj_set_style_bg_color(cur_think_hdr, color_map[4], 0);
    lv_obj_set_style_radius(cur_think_hdr, 6, 0);
    lv_obj_set_style_pad_all(cur_think_hdr, 6, 0);

    lv_obj_t *hdr_txt = lv_label_create(cur_think_hdr);          /* 中文：楷体 */
    lv_obj_set_style_text_font(hdr_txt, font_sk_24, 0);
    lv_obj_set_style_text_color(hdr_txt, color_map[1], 0);
    lv_label_set_text(hdr_txt, "深度思考...");
    lv_obj_align(hdr_txt, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t *hdr_arrow = lv_label_create(cur_think_hdr);        /* 箭头：默认字体带符号 */
    lv_obj_set_style_text_font(hdr_arrow, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(hdr_arrow, color_map[1], 0);
    lv_label_set_text(hdr_arrow, LV_SYMBOL_DOWN);
    lv_obj_align_to(hdr_arrow, hdr_txt, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    cur_think = lv_label_create(chat_body);
    lv_label_set_long_mode(cur_think, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(cur_think, LV_PCT(100));
    lv_obj_set_style_text_font(cur_think, font_sk_24, 0);
    lv_obj_set_style_text_color(cur_think, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_bg_color(cur_think, lv_color_hex(0x232323), 0);
    lv_obj_set_style_bg_opa(cur_think, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cur_think, 6, 0);
    lv_obj_set_style_pad_all(cur_think, 8, 0);
    lv_label_set_text(cur_think, "");

    lv_obj_add_event_cb(cur_think_hdr, think_toggle_cb, LV_EVENT_CLICKED, cur_think);
    lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* 思维链结束：折叠为一行"已深度思考 ▸"，点击再展开 */
static void finish_reasoning(void)
{
    if (cur_think_hdr != NULL)
    {
        lv_obj_t *arrow = lv_obj_get_child(cur_think_hdr, 1);
        if (arrow != NULL) lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    }
    if (cur_think != NULL)
        lv_obj_add_flag(cur_think, LV_OBJ_FLAG_HIDDEN);   /* 默认收起 */
    lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* LVGL 主循环定时器（由 lv_timer_handler 驱动）：轮询流式增量 */
static void chat_poll_cb(lv_timer_t *t)
{
    static bool was_busy = false;
    bool busy = dp_chat_is_busy();
    bool just_finished = was_busy && !busy;
    was_busy = busy;

    /* 1. 思维链增量：实时显示"深度思考中"，结束后折叠 */
    char *reasoning = dp_stream_poll_reasoning();
    if (reasoning != NULL && reasoning[0] != '\0')
    {
        if (!in_reasoning_phase && cur_content == NULL)
        {
            start_reasoning();
            in_reasoning_phase = true;
        }
        label_append(cur_think, reasoning);
        lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);
        free(reasoning);
    }

    /* 2. 正式回答增量（打字机效果） */
    char *delta = dp_stream_poll();
    if (delta != NULL && delta[0] != '\0')
    {
        if (in_reasoning_phase)          /* 思维链结束，正式回答开始 */
        {
            finish_reasoning();
            in_reasoning_phase = false;
        }
        if (need_assistant_prefix)       /* 首条内容前加角色前缀，只加一次 */
        {
            if (cur_content == NULL)
                cur_content = chat_body_add_label("DeepSeek:\n");
            need_assistant_prefix = false;
        }
        if (cur_content != NULL)
        {
            label_append(cur_content, delta);
            lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);  /* 边收边滚 */
        }
        free(delta);
    }

    /* 3. 本轮结束：收尾思维链、隐藏加载动画并滚到底 */
    if (just_finished)
    {
        if (in_reasoning_phase)
        {
            finish_reasoning();
            in_reasoning_phase = false;
        }
        need_assistant_prefix = false;
        if (spinner != NULL)
            lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        if (stop_btn != NULL)
            lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);      // 隐藏"停止"
        lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_ON);
    }
    LV_UNUSED(t);
}

/* 输出区 全屏/还原 切换：放大到全屏，文字随容器宽度重新换行 */
static void chat_fullscreen_cb(lv_event_t * e)
{
    if(chat_cont == NULL) return;
    lv_obj_t * btn_label = lv_obj_get_child(lv_event_get_current_target(e), 0);

    if(!chat_fullscreen)
    {
        /* 放大到全屏 */
        lv_obj_move_foreground(chat_cont);   /* 提到最顶层，盖住输入框/键盘 */
        lv_obj_move_foreground(fs_btn);      /* 按钮保持在容器之上，保证能点"还原" */
        ui_nav_raise();                      /* 底部"返回桌面"手势保持可点 */
        ui_quick_settings_raise();           /* 顶部下拉快捷设置保持可点 */
        ui_model_raise();                    /* 模型按钮/面板保持最顶层 */
        lv_obj_set_size(chat_cont, LV_HOR_RES, LV_VER_RES);
        lv_obj_align(chat_cont, LV_ALIGN_TOP_LEFT, 0, 0);
        if (chat_body != NULL) lv_obj_set_width(chat_body, LV_PCT(100));
        lv_label_set_text(btn_label, LV_SYMBOL_NEXT);   /* ⏭ = 还原 */
    }
    else
    {
        /* 还原 */
        lv_obj_move_background(chat_cont);   /* 恢复普通层级，露出输入框 */
        lv_obj_set_size(chat_cont, 600, 600);
        lv_obj_align_to(chat_cont, lv_screen_active(), LV_ALIGN_TOP_RIGHT, 0, 0);
        if (chat_body != NULL) lv_obj_set_width(chat_body, LV_PCT(100));
        lv_label_set_text(btn_label, LV_SYMBOL_PREV);   /* ⏮ = 全屏 */
    }
    chat_fullscreen = !chat_fullscreen;

    /* 文字重排后滚到底部 */
    lv_obj_scroll_to_y(chat_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* 停止生成按钮：点击立即中断当前 AI 输出 */
static void stop_btn_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    dp_chat_stop();
}

/* 创建文本输出区域（消息列表/思维链/全屏/停止/加载动画） */
void ui_output_create(void)
{
    /* 外层容器：600*600 的显示区域 */
    lv_obj_t * cont = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cont, 600, 600);
    lv_obj_align_to(cont, lv_screen_active(), LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);  /* 超出自动出滚动条 */
    lv_obj_move_background(cont);
    chat_cont = cont;                          /* 保存容器引用，供全屏切换用 */

    //背景
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x808080), 0);  /* 灰色底 #808080 */
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);              /* 不透明，盖住默认白 */
    /* 背景图片：垫在文字下面，铺满容器，且不随文字滚动 */
    lv_obj_t * bg_img = lv_image_create(cont);
    lv_image_set_src(bg_img, "./pic/jingyuan_lvgl.jpg");
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_size(bg_img, LV_PCT(100), LV_PCT(100));  /* 铺满容器 */
    lv_obj_add_flag(bg_img, LV_OBJ_FLAG_FLOATING);      /* 关键：不随文字滚动，固定背景 */
    lv_obj_set_style_image_opa(bg_img, LV_OPA_20, 0);   /* 图片透明度 */
    lv_obj_move_background(bg_img);                     /* 垫到最底层，文字在上面 */

    /* 消息列表容器：纵向排列各条消息（用户/助手/思维链），滚动交给外层 cont */
    chat_body = lv_obj_create(cont);
    lv_obj_remove_flag(chat_body, LV_OBJ_FLAG_SCROLLABLE);   /* 滚动交给外层 */
    lv_obj_set_width(chat_body, LV_PCT(100));
    lv_obj_set_height(chat_body, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(chat_body, 8, 0);
    lv_obj_set_style_pad_row(chat_body, 4, 0);
    lv_obj_set_style_border_width(chat_body, 0, 0);
    lv_obj_set_style_bg_opa(chat_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(chat_body, 0, 0);
    lv_obj_set_layout(chat_body, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chat_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(chat_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 全屏/还原 按钮：放在输出区右上角（跟随容器移动） */
    fs_btn = lv_button_create(lv_screen_active());
    lv_obj_set_size(fs_btn, 48, 48);
    lv_obj_align_to(fs_btn, cont, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_t * fs_label = lv_label_create(fs_btn);
    lv_obj_set_style_text_font(fs_label, lv_font_get_default(), 0); /* 默认字体才带 LV_SYMBOL 图标 */
    lv_obj_set_style_text_color(fs_label, lv_color_white(), 0);
    lv_label_set_text(fs_label, LV_SYMBOL_PREV);              /* ⏮ = 全屏 */
    lv_obj_center(fs_label);
    lv_obj_add_event_cb(fs_btn, chat_fullscreen_cb, LV_EVENT_CLICKED, NULL);

    /* 底部上滑唤出"返回桌面"（可复用导航模块，见 src/ui_nav.h） */
    ui_nav_add_home_gesture(lv_screen_active());

    /* 停止生成按钮：右下角圆形红色按钮（生成时才显示） */
    stop_btn = lv_button_create(cont);
    lv_obj_set_size(stop_btn, 48, 48);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_FLOATING);        /* 固定角落，不随文字滚动 */
    lv_obj_align(stop_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xd01616), 0);
    lv_obj_set_style_radius(stop_btn, 24, 0);               /* 圆形 */
    lv_obj_set_style_border_width(stop_btn, 0, 0);
    lv_obj_t * stop_label = lv_label_create(stop_btn);
    lv_obj_set_style_text_font(stop_label, lv_font_get_default(), 0);
    lv_obj_set_style_text_color(stop_label, lv_color_white(), 0);
    lv_label_set_text(stop_label, LV_SYMBOL_STOP);          /* ■ 停止图标 */
    lv_obj_center(stop_label);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_HIDDEN);          /* 默认隐藏 */
    lv_obj_add_event_cb(stop_btn, stop_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 加载动画（初始隐藏）：旋转圆环 + 圆心图片(超出自动画成圆) */
    spinner = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(spinner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(spinner, 110, 110);
    lv_obj_center(spinner);
    lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spinner, 0, 0);
    lv_obj_set_style_pad_all(spinner, 0, 0);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    /* 旋转的圆环（底环 + 转动的指示弧） */
    lv_obj_t * ring = lv_spinner_create(spinner);
    lv_obj_set_size(ring, 110, 110);
    lv_obj_center(ring);
    lv_spinner_set_anim_params(ring, 1000, 60);                 /* 转一圈 1s，弧长 60° */
    lv_obj_set_style_arc_color(ring, lv_color_hex(0x3a4150), LV_PART_MAIN);      /* 底环 */
    lv_obj_set_style_arc_width(ring, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, color_map[4], LV_PART_INDICATOR);           /* 蓝色转动 */
    lv_obj_set_style_arc_width(ring, 5, LV_PART_INDICATOR);

    /* 圆心图片：放入圆形裁剪容器，图片超出圆的部分自动裁掉 */
    lv_obj_t * logo_clip = lv_obj_create(spinner);
    lv_obj_remove_flag(logo_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(logo_clip, 98, 98);
    lv_obj_set_style_radius(logo_clip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(logo_clip, true, 0);           /* 子控件按圆形裁剪 */
    lv_obj_set_style_bg_color(logo_clip, lv_color_hex(0x1a2029), 0);
    lv_obj_set_style_bg_opa(logo_clip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(logo_clip, 0, 0);
    lv_obj_set_style_pad_all(logo_clip, 0, 0);
    lv_obj_center(logo_clip);

    lv_obj_t * logo = lv_image_create(logo_clip);
    lv_image_set_src(logo, "./pic/feiyu_head.jpg");
    lv_obj_set_size(logo, 98, 98);
    lv_obj_center(logo);

    /* 启动结果轮询定时器（由 lv_timer_handler 驱动），只创建一次 */
    static int poll_started = 0;
    if (!poll_started)
    {
        lv_timer_create(chat_poll_cb, 100, NULL);
        poll_started = 1;
    }
}
