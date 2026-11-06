#ifndef DP_CHAT_H
#define DP_CHAT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 异步流式 DeepSeek 聊天层（LVGL 移植用）
 * ------------------------------------------------
 * 设计原则：LVGL 主线程不能阻塞。
 *  - dp_send_message_async() 立即返回，内部交给 worker 线程执行
 *    现有的 connect/SSL/发送/接收 逻辑，并开启 stream:true 流式输出；
 *  - worker 边接收边解析 SSE，把增量文本写入内部流缓冲；
 *  - LVGL 主线程用 lv_timer 定时调用 dp_stream_poll() 取出增量刷新界面，
 *    并通过 dp_chat_is_busy() 判断本轮对话是否结束。
 */

/* 初始化异步聊天层：解析域名 + 启动 worker 线程。成功返回 0 */
int dp_chat_init(void);

/* 非阻塞发送一条用户消息（内部拷贝，可安全传入 textarea 文本）。
 * 成功返回 0；未初始化 / 参数为空 / 队列满返回 -1 */
int dp_send_message_async(const char *user_message);

/* 运行时切换使用的模型名（内部拷贝保存，下一次请求生效）。成功返回 0 */
int dp_chat_set_model(const char *model);

/* 返回当前模型名（内部缓冲，勿 free/修改） */
const char *dp_chat_get_model(void);

/* 停止当前正在进行的生成（UI 可随时调用，立即中断） */
void dp_chat_stop(void);

/* 取走自上次调用以来新到的流式文本（malloc，调用者必须 free）。
 * 无新数据返回 NULL */
char *dp_stream_poll(void);

/* 取走自上次调用以来新到的思维链增量（reasoning_content，malloc，调用者必须 free）。
 * 无新数据返回 NULL */
char *dp_stream_poll_reasoning(void);

/* 当前是否正有请求在途（=流式进行中）。UI 用它判断何时结束 */
bool dp_chat_is_busy(void);

/* 销毁异步聊天层：停止并回收 worker 线程、释放残留结果 */
void dp_chat_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* DP_CHAT_H */
