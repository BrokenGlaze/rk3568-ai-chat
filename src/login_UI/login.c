/*
 * 登录界面(login_UI/login.c)
 * 由 src/dp_chat_screen.c 的 welcome_to_my_ui() 调用 login_screen_control() 启动。
 * 登录成功 → 进入手机式桌面(ui_home_show)，再点图标进入 DeepSeek 聊天屏。
 */
#include "lvgl/lvgl.h"

#include <string.h>

#include "src/login_UI/login.h"
#include "src/deepseek_chat_UI/ui_shared.h"   /* ui_home_show */
#include "src/app_common.h"                  /* 主题资源 + 通用控件辅助 */

static lv_obj_t *login_screen = NULL;

static bool check_password(const char *input)
{
    const char *correct_password = "";  // 正确密码
    return strcmp(input, correct_password) == 0;
}

/* 验证密码 → 对则进主界面,错则弹提示 */
static void verify_and_enter_main(lv_obj_t * ta)   // 入参就是输入框
{
    const char *input = lv_textarea_get_text(ta);

    if (check_password(input))
    {
        LV_LOG_USER("密码正确!");
        ui_home_show();      /* 进入手机式桌面主界面 */
    }
    else
    {
        LV_LOG_USER("密码错误!");
        lv_obj_t * err = create_label_out(lv_screen_active(), LV_ALIGN_CENTER, 0, -50,
                                          "密码错误,请重试!", color_map[5]);
        lv_timer_create(hide_timer_cb, 3000, err);   /* 3秒后消失 */
        lv_textarea_set_text(ta, "");
    }
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target_obj(e);
    lv_obj_t * kb = (lv_obj_t *) lv_event_get_user_data(e);  /* user_data = 键盘 */

    if(code == LV_EVENT_FOCUSED)
    {
        if(kb != NULL)
        {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);  // 显示键盘
        }
    }
    else if(code == LV_EVENT_DEFOCUSED)
    {
        if(kb != NULL)
        {
            lv_keyboard_set_textarea(kb, NULL);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);  // 隐藏键盘
        }
    }
    else if(code == LV_EVENT_READY)
    {
        verify_and_enter_main(ta);
    }
}

/* 密码确认事件回调函数 */
static void confirm_click_cb(lv_event_t * e)
{
    lv_obj_t * ta = (lv_obj_t *) lv_event_get_user_data(e);   /* 从按钮便利贴取输入框 */
    verify_and_enter_main(ta);
}

/* 创建密码输入的文本区域 */
static lv_obj_t *textarea_and_keyborad_login(void)
{
    lv_obj_t * pwd_ta = lv_textarea_create(lv_screen_active());
    lv_obj_align_to(pwd_ta, lv_screen_active(), LV_ALIGN_TOP_MID, 0, 0);

    //设置初始为空
    lv_textarea_set_text(pwd_ta, "");
    lv_textarea_set_password_mode(pwd_ta, true);
    lv_textarea_set_one_line(pwd_ta, true);
    //宽度为300像素
    lv_obj_set_width(pwd_ta, 300);

    //键盘创建
    lv_obj_t *kb = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(kb,  LV_HOR_RES, LV_VER_RES / 2);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(pwd_ta, ta_event_cb, LV_EVENT_ALL, kb);

    //键盘样式
    keyborad_style_black(kb);

    //将文本框与键盘绑定
    lv_keyboard_set_textarea(kb, pwd_ta);

    return pwd_ta;
}

void login_screen_control(void)
{
    /* —— 把登录界面立成一块独立的屏 —— */
    login_screen = lv_obj_create(NULL);
    lv_screen_load(login_screen);

    // 1.登入界面
    //文本输入框
    lv_obj_t *pwd_ta = textarea_and_keyborad_login();
    //添加"密码"标签
    lv_obj_t *pwd_label = create_label_out(pwd_ta, LV_ALIGN_OUT_LEFT_TOP, 0, 0, "密码:", color_map[0]);
    //“确认”按钮点击事件
    lv_obj_t *btn_label = create_confirm_button(pwd_ta, 100, 50, 0, 10, LV_ALIGN_OUT_BOTTOM_MID, "确认");
    lv_obj_add_event_cb(btn_label, confirm_click_cb, LV_EVENT_CLICKED, pwd_ta);
}
