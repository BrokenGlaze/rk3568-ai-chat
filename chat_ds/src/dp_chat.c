#include "dp_chat.h"
#include "c_dp.h"

#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>   /* struct timeval */
#include <signal.h>     /* SIGPIPE */

/* 服务器地址（与原来 main() 里一致） */
#define DP_SERVER_ADDR "api.deepseek.com/chat/completions"

/* 单次请求最大重试次数（仅连接阶段失败时重试） */
#define DP_MAX_RETRY 3
/* 单次 socket 接收超时（秒）：流式时等待下一段增量 */
#define DP_RECV_TIMEOUT_SEC 20
/* 整个流式对话最长耗时（秒），防止死连接卡死 worker */
#define DP_STREAM_DEADLINE_SEC 120
/* 流式文本缓冲上限（字节） */
#define DP_STREAM_MAX (64 * 1024)

/* ===================== 待发送任务队列 ===================== */
#define DP_JOB_CAP 16

typedef struct {
    char text[MAX_MSG_LEN];
} DpJob;

static DpJob             g_jobs[DP_JOB_CAP];
static int               g_job_head = 0;
static int               g_job_tail = 0;
static pthread_mutex_t   g_job_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    g_job_cond  = PTHREAD_COND_INITIALIZER;
static volatile bool     g_busy = false;

/* 停止当前生成：由 UI 线程置位，worker 在收流循环检查并退出 */
static volatile bool     g_stop_requested = false;
/* 当前活动 socket（供停止时 shutdown 以打断阻塞的 SSL_read） */
static int               g_active_fd = -1;
static pthread_mutex_t   g_active_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ===================== 流式输出缓冲（UI 线程轮询） ===================== */
static char              g_stream_buf[DP_STREAM_MAX];
static size_t            g_stream_len = 0;        /* 已写入字节数 */
static size_t            g_stream_consumed = 0;   /* UI 已消费的位置 */
static pthread_mutex_t   g_stream_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 思维链（reasoning_content）缓冲：与正式回答分开，UI 可折叠显示 */
static char              g_reason_buf[DP_STREAM_MAX];
static size_t            g_reason_len = 0;        /* 已写入字节数 */
static size_t            g_reason_consumed = 0;   /* UI 已消费的位置 */

static pthread_t          g_worker_tid;
static volatile bool      g_running = false;

/* 连接参数（init 时解析一次，供每次请求复用） */
static char  g_dest_ip[5][20];
static char  g_web_addr[128];
static char  g_url_addr[128];

/* 当前使用的模型名（UI 可运行时切换，下一次请求生效） */
static char            g_model[64] = "deepseek-v4-flash";
static pthread_mutex_t g_model_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---------- 内部函数 ---------- */
static char *build_stream_request(const char *user_message);
static void  stream_reset(void);
static void  stream_append(const char *text, size_t len);
static void  reason_append(const char *text, size_t len);
static void  dp_chat_streaming(const char *user_message);

/* ===================================================================
 * 运行时模型切换
 * =================================================================== */
int dp_chat_set_model(const char *model)
{
    if (model == NULL) return -1;
    pthread_mutex_lock(&g_model_mutex);
    strncpy(g_model, model, sizeof(g_model) - 1);
    g_model[sizeof(g_model) - 1] = '\0';
    pthread_mutex_unlock(&g_model_mutex);
    return 0;
}

const char *dp_chat_get_model(void)
{
    static char model[64];
    pthread_mutex_lock(&g_model_mutex);
    strncpy(model, g_model, sizeof(model) - 1);
    model[sizeof(model) - 1] = '\0';
    pthread_mutex_unlock(&g_model_mutex);
    return model;
}

/* ===================================================================
 * 构造 stream:true 的 HTTP 请求报文（含 SSE 的 Accept 头）
 * =================================================================== */
static char *build_stream_request(const char *user_message)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) 
        return NULL;

    /* 取当前模型名（线程安全快照） */
    char model[64];
    pthread_mutex_lock(&g_model_mutex);
    strncpy(model, g_model, sizeof(model) - 1);
    model[sizeof(model) - 1] = '\0';
    pthread_mutex_unlock(&g_model_mutex);
    cJSON_AddStringToObject(root, "model", model);

    cJSON *messages = cJSON_CreateArray();
    if (messages == NULL) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "messages", messages);

    /* 带上历史上下文 */
    for (int i = 0; i < history_count; i++) 
    {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", history[i].role);
        cJSON_AddStringToObject(msg, "content", history[i].content);
        cJSON_AddItemToArray(messages, msg);
    }
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(messages, user_msg);

    cJSON_AddTrueToObject(root, "stream");

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) 
        return NULL;

    size_t req_len = strlen(json) + 1024;
    char *req = malloc(req_len);
    if (req == NULL)
        { free(json); return NULL; }

    snprintf(req, req_len,
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n"
             "Accept: text/event-stream\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             g_url_addr, g_web_addr, API_KEY, strlen(json), json);
    free(json);
    return req;
}

/* ---------------- SSE 解析状态（每次流式调用由 stream_reset 复位） ---------------- */
static char   s_line[2048];
static size_t s_line_len = 0;
static char   s_full[DP_STREAM_MAX];   /* 完整回复（用于写入历史） */
static size_t s_full_len = 0;
static bool   s_any_delta = false;     /* 是否收到过内容 */
static bool   s_any_content = false;   /* 是否收到过正式回答 content（不含思维链） */

/* 处理一行 SSE 数据：提取 delta 内容追加到流缓冲 */
static void sse_handle_line(const char *line, bool *done)
{
    if (strncmp(line, "data:", 5) != 0)
        return;                          /* 忽略 HTTP 头 / 空行 / event: 行 */

    const char *payload = line + 5;
    while (*payload == ' ') payload++;

    if (strcmp(payload, "[DONE]") == 0) {
        *done = true;
        return;
    }

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) return;

    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (cJSON_IsArray(choices)) 
    {
        cJSON *ch = cJSON_GetArrayItem(choices, 0);
        if (ch != NULL) 
        {
            cJSON *delta = cJSON_GetObjectItemCaseSensitive(ch, "delta");
            if (delta != NULL) 
            {
                /* 思维链 reasoning_content：独立缓冲（UI 可折叠展示，不写入历史） */
                cJSON *rc = cJSON_GetObjectItemCaseSensitive(delta, "reasoning_content");
                if (cJSON_IsString(rc) && rc->valuestring != NULL && rc->valuestring[0] != '\0')
                {
                    size_t rl = strlen(rc->valuestring);
                    reason_append(rc->valuestring, rl);
                    s_any_delta = true;
                }

                /* 只取正式回答 content */
                const char *part = NULL;
                cJSON *c = cJSON_GetObjectItemCaseSensitive(delta, "content");
                if (cJSON_IsString(c) && c->valuestring != NULL)
                    part = c->valuestring;
                if (part != NULL && part[0] != '\0') 
                {
                    size_t l = strlen(part);
                    stream_append(part, l);
                    if (s_full_len + l < sizeof(s_full)) 
                    {
                        memcpy(s_full + s_full_len, part, l);
                        s_full_len += l;
                        s_full[s_full_len] = '\0';
                    }
                    s_any_delta = true;
                    s_any_content = true;
                }
            }
            cJSON *fr = cJSON_GetObjectItemCaseSensitive(ch, "finish_reason");
            if (cJSON_IsString(fr) && fr->valuestring != NULL &&
                strcmp(fr->valuestring, "stop") == 0) {
                *done = true;
            }
        }
    }
    cJSON_Delete(root);
}

/* 喂数据给 SSE 解析器（逐行处理） */
static void sse_feed(const char *data, size_t n, bool *done)
{
    for (size_t i = 0; i < n && !*done; i++) {
        char ch = data[i];
        if (ch == '\n') {
            if (s_line_len > 0 && s_line[s_line_len - 1] == '\r')
                s_line_len--;
            s_line[s_line_len] = '\0';
            if (s_line_len > 0)
                sse_handle_line(s_line, done);
            s_line_len = 0;
        }
        else if (s_line_len < sizeof(s_line) - 1) {
            s_line[s_line_len++] = ch;
        }
    }
}

/* ===================================================================
 * 流式执行一次完整对话（只在 worker 线程内调用）
 * 连接 → TLS → 发送 stream:true 请求 → 逐段接收并解析 SSE →
 * 增量写入流缓冲（UI 实时显示）→ 完成后写入历史
 * =================================================================== */
static void dp_chat_streaming(const char *user_message)
{
    SSL       *ssl = NULL;
    SSL_CTX   *ctx = NULL;
    int        skt_fd = -1;
    bool       stream_done = false;
    time_t     deadline;

    stream_reset();
    deadline = time(NULL) + DP_STREAM_DEADLINE_SEC;

    /* 新一轮请求：复位停止状态 */
    g_stop_requested = false;
    g_active_fd = -1;

    /* 仅在还没收到任何内容时重试（避免把半截回复拼进历史） */
    for (int retry = 0; retry < DP_MAX_RETRY && !stream_done && !s_any_delta && !g_stop_requested; retry++)
    {
        /* 重试前释放上一轮连接资源 */
        if (ssl != NULL && ctx != NULL) 
        {
            destroy_SSL(ssl, ctx);
            ssl = NULL;
            ctx = NULL;
        }
        if (skt_fd != -1) 
        {
            close(skt_fd);
            skt_fd = -1;
        }

        /* 建立 TCP 连接 + TLS 握手 */
        skt_fd = connect_with_host(443, g_dest_ip);
        if (skt_fd == -1)
            break;

        /* socket 接收超时：流式时每段增量间隔可能较长 */
        struct timeval tv = { .tv_sec = DP_RECV_TIMEOUT_SEC, .tv_usec = 0 };
        setsockopt(skt_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (init_SSL(skt_fd, &ssl, &ctx) == -1) 
        {
            close(skt_fd);
            skt_fd = -1;
            break;
        }

        /* 记录活动 socket，供停止时 shutdown 打断阻塞读 */
        pthread_mutex_lock(&g_active_mutex);
        g_active_fd = skt_fd;
        pthread_mutex_unlock(&g_active_mutex);

        /* 构造并发送流式请求 */
        char *request = build_stream_request(user_message);
        if (request == NULL)
            continue;

        int total_len = (int)strlen(request);
        int total_sent = 0;
        while (total_sent < total_len) 
        {
            ssize_t wr = SSL_write(ssl, request + total_sent, total_len - total_sent);
            if (wr <= 0) 
            {
                fprintf(stderr, "发送请求报文失败: SSL_get_error=%d\n",
                        SSL_get_error(ssl, (int)wr));
                break;
            }
            total_sent += (int)wr;
        }
        free(request);
        if (total_sent < total_len)
            continue;   /* 发送失败，重试 */

        /* 逐段接收并解析 SSE，增量写入流缓冲 */
        char rbuf[4096];
        while (!stream_done) {
            if (g_stop_requested) break;          /* 用户点"停止" */
            if (time(NULL) >= deadline) break;    /* 整体超时 */

            ssize_t rd = SSL_read(ssl, rbuf, sizeof(rbuf));
            if (rd > 0) 
            {
                sse_feed(rbuf, (size_t)rd, &stream_done);
            }
            else if (rd == 0) 
            {
                break;                            /* 连接关闭 */
            }
            else 
            {
                int err = SSL_get_error(ssl, rd);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                    continue;                     /* 等待下一段 */
                if (err == SSL_ERROR_SYSCALL &&
                    (errno == EAGAIN || errno == EWOULDBLOCK))
                    continue;                     /* 接收超时但连接仍在，继续等 */
                break;                            /* 真正出错 */
            }
        }
    }

    /* 清理连接 */
    if (ssl != NULL && ctx != NULL)
        destroy_SSL(ssl, ctx);
    if (skt_fd != -1)
        close(skt_fd);

    pthread_mutex_lock(&g_active_mutex);
    g_active_fd = -1;
    pthread_mutex_unlock(&g_active_mutex);

    /* 收尾：有正式内容写历史；其余情况按原因提示 */
    if (s_any_content)
    {
        add_to_history("user", user_message);
        add_to_history("assistant", s_full);
    }
    else if (g_stop_requested)
    {
        stream_append("（已停止生成）", strlen("（已停止生成）"));
    }
    else if (stream_done)
    {
        stream_append("（无内容返回）", strlen("（无内容返回）"));
    }
    else
    {
        stream_append("（请求失败，请稍后重试）", strlen("（请求失败，请稍后重试）"));
    }
}

/* ===================================================================
 * worker 线程主循环：等待任务 → 阻塞对话 → 结果入队
 * =================================================================== */
static void *dp_worker(void *arg)
{
    (void)arg;

    while (g_running)
    {
        /* 1. 阻塞等待任务 */
        pthread_mutex_lock(&g_job_mutex);
        while (g_job_head == g_job_tail && g_running)
            pthread_cond_wait(&g_job_cond, &g_job_mutex);
        if (!g_running)
        {
            pthread_mutex_unlock(&g_job_mutex);
            break;
        }
        DpJob job = g_jobs[g_job_head];
        g_job_head = (g_job_head + 1) % DP_JOB_CAP;
        pthread_mutex_unlock(&g_job_mutex);

        /* 2. 执行流式对话（边收边写入流缓冲，UI 实时显示） */
        dp_chat_streaming(job.text);

        /* 3. 队列空则标记不忙（=本轮流式结束，UI 据此隐藏加载动画） */
        pthread_mutex_lock(&g_job_mutex);
        if (g_job_head == g_job_tail)
            g_busy = false;
        pthread_mutex_unlock(&g_job_mutex);
    }
    return NULL;
}

/* ===================================================================
 * 对外 API
 * =================================================================== */
int dp_chat_init(void)
{
    if (g_running)
        return 0;

    /* 忽略 SIGPIPE：点"停止"时 shutdown(socket) 后，
     * SSL_shutdown/SSL_write 往已关闭的 socket 写数据会触发 SIGPIPE，
     * 默认动作会直接终止整个进程(表现为整个程序退出)。忽略后写失败只返回错误码。 */
    signal(SIGPIPE, SIG_IGN);

    /* 解析域名（阻塞，仅一次） */
    if (get_hostinfo_and_dest_ip(DP_SERVER_ADDR, g_dest_ip,
                                 g_web_addr, g_url_addr) == -1)
        return -1;

    g_running = true;
    g_busy = false;

    if (pthread_create(&g_worker_tid, NULL, dp_worker, NULL) != 0)
    {
        g_running = false;
        return -1;
    }
    return 0;
}

int dp_send_message_async(const char *user_message)
{
    if (!g_running)
        return -1;
    if (user_message == NULL || user_message[0] == '\0')
        return -1;

    pthread_mutex_lock(&g_job_mutex);

    if ((g_job_tail + 1) % DP_JOB_CAP == g_job_head)
    {
        pthread_mutex_unlock(&g_job_mutex);   /* 队列满 */
        return -1;
    }

    strncpy(g_jobs[g_job_tail].text, user_message, MAX_MSG_LEN - 1);
    g_jobs[g_job_tail].text[MAX_MSG_LEN - 1] = '\0';
    g_job_tail = (g_job_tail + 1) % DP_JOB_CAP;
    g_busy = true;

    pthread_cond_signal(&g_job_cond);
    pthread_mutex_unlock(&g_job_mutex);
    return 0;
}

/* 停止当前正在进行的生成（UI 可随时调用） */
void dp_chat_stop(void)
{
    g_stop_requested = true;

    /* 打断 worker 中阻塞的 SSL_read：shutdown 当前活动 socket */
    pthread_mutex_lock(&g_active_mutex);
    int fd = g_active_fd;
    pthread_mutex_unlock(&g_active_mutex);
    if (fd != -1)
        shutdown(fd, SHUT_RDWR);
}

char *dp_stream_poll(void)
{
    pthread_mutex_lock(&g_stream_mutex);
    if (g_stream_consumed >= g_stream_len)
    {
        pthread_mutex_unlock(&g_stream_mutex);
        return NULL;
    }
    size_t n = g_stream_len - g_stream_consumed;
    char *out = malloc(n + 1);
    if (out == NULL)
    {
        pthread_mutex_unlock(&g_stream_mutex);
        return NULL;
    }
    memcpy(out, g_stream_buf + g_stream_consumed, n);
    out[n] = '\0';
    g_stream_consumed = g_stream_len;
    pthread_mutex_unlock(&g_stream_mutex);
    return out;   /* 调用者负责 free */
}

/* 取走自上次调用以来新到的思维链增量（malloc，调用者必须 free）；无返回 NULL */
char *dp_stream_poll_reasoning(void)
{
    pthread_mutex_lock(&g_stream_mutex);
    if (g_reason_consumed >= g_reason_len)
    {
        pthread_mutex_unlock(&g_stream_mutex);
        return NULL;
    }
    size_t n = g_reason_len - g_reason_consumed;
    char *out = malloc(n + 1);
    if (out == NULL)
    {
        pthread_mutex_unlock(&g_stream_mutex);
        return NULL;
    }
    memcpy(out, g_reason_buf + g_reason_consumed, n);
    out[n] = '\0';
    g_reason_consumed = g_reason_len;
    pthread_mutex_unlock(&g_stream_mutex);
    return out;   /* 调用者负责 free */
}

/* ---------- 流缓冲辅助（worker 线程内调用） ---------- */
static void stream_reset(void)
{
    pthread_mutex_lock(&g_stream_mutex);
    g_stream_len = 0;
    g_stream_consumed = 0;
    g_reason_len = 0;
    g_reason_consumed = 0;
    pthread_mutex_unlock(&g_stream_mutex);

    s_line_len = 0;
    s_full_len = 0;
    s_full[0] = '\0';
    s_any_delta = false;
    s_any_content = false;
}

static void stream_append(const char *text, size_t len)
{
    if (len == 0) return;
    pthread_mutex_lock(&g_stream_mutex);
    if (g_stream_len + len < DP_STREAM_MAX) {
        memcpy(g_stream_buf + g_stream_len, text, len);
        g_stream_len += len;
        g_stream_buf[g_stream_len] = '\0';
    }
    pthread_mutex_unlock(&g_stream_mutex);
}

static void reason_append(const char *text, size_t len)
{
    if (len == 0) return;
    pthread_mutex_lock(&g_stream_mutex);
    if (g_reason_len + len < DP_STREAM_MAX) {
        memcpy(g_reason_buf + g_reason_len, text, len);
        g_reason_len += len;
        g_reason_buf[g_reason_len] = '\0';
    }
    pthread_mutex_unlock(&g_stream_mutex);
}

bool dp_chat_is_busy(void)
{
    bool busy;
    pthread_mutex_lock(&g_job_mutex);
    busy = g_busy;
    pthread_mutex_unlock(&g_job_mutex);
    return busy;
}

void dp_chat_deinit(void)
{
    if (!g_running)
        return;

    pthread_mutex_lock(&g_job_mutex);
    g_running = false;
    pthread_cond_broadcast(&g_job_cond);
    pthread_mutex_unlock(&g_job_mutex);

    pthread_join(g_worker_tid, NULL);
}

