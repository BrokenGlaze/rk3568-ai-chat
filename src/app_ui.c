/*
 * app_ui.c —— 应用启动入口
 * -------------------------------------------------
 * 由 main.c 的 driver_backends_run_loop() 之前调用一次 welcome_to_my_ui()：
 *   1. init_freetype_font()   初始化全局字体 + 默认样式（见 app_common.c）
 *   2. login_screen_control() 进入首屏：登录界面（见 login_UI/login.c）
 */
#include "src/app_ui.h"
#include "src/app_common.h"     /* init_freetype_font */
#include "src/login_UI/login.h" /* login_screen_control */

void welcome_to_my_ui(void)
{
    //字体初始化
    init_freetype_font();
    //登入界面控制
    login_screen_control();
}
