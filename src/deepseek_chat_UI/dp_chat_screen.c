/*
 * deepseek_chat_UI/dp_chat_screen.c —— DeepSeek 聊天屏
 * -------------------------------------------------
 * 负责：聊天屏对象 dp_chat_screen（首次创建、之后切回，对话保留）+
 *       main_screen_control 组装同目录 4 大 UI 模块。
 * 全局主题资源/通用控件辅助已移至 src/app_common.c；
 * 应用启动入口(welcome_to_my_ui)在 src/app_ui.c。
 */
#include "ui_shared.h"             /* 同目录共享头(含 lvgl.h / 模块入口 / 导航) */
#include "src/ui_quick_settings.h" /* 顶部下拉快捷设置(可复用) */

void main_screen_control(void);    /* 本文件内定义，供 dp_chat_screen 使用 */

static lv_obj_t *chat_screen = NULL;

/* 进入 DeepSeek 聊天屏：首次创建并构建，之后直接切回（对话/状态保留） */
void dp_chat_screen(void)
{
    if (chat_screen == NULL)
    {
        chat_screen = lv_obj_create(NULL);     // 新黑板
        lv_screen_load(chat_screen);           // 切入聊天屏
        main_screen_control();                 // 在聊天屏上构建 4 大件 UI
    }
    else
    {
        lv_screen_load(chat_screen);           // 已有则直接切回
    }
}

/* 组装聊天 UI（建在当前活动屏上，即聊天屏） */
void main_screen_control(void)
{
    ui_output_create();      /* 1. 文本输出框（消息列表/思维链/全屏/停止/加载动画） */
    ui_input_create();       /* 2. 文本输入框（拼音输入法 + 键盘） */
    ui_model_create();       /* 3. 模型切换列表（左侧） */
    ui_quick_settings_add(lv_screen_active());   /* 4. 顶部下拉：亮度调节面板(可复用) */
    
}
