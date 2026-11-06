#include "c_dp.h"
#include "dp_chat.h"


Message  history[MAX_HISTORY];                   
int      history_count = 0; 

// 将消息加入历史记录
void add_to_history(const char *role, const char *content)
{
    if (history_count >= MAX_HISTORY)
    {
        // 历史已满，移除最旧的一条（滑动窗口）
        for (int i = 1; i < history_count; i++)
        {
            history[i - 1] = history[i];
        }
        history_count--;
    }

    strncpy(history[history_count].role, role, sizeof(history[history_count].role) - 1);
    history[history_count].role[sizeof(history[history_count].role) - 1] = '\0';
    strncpy(history[history_count].content, content, sizeof(history[history_count].content) - 1);
    history[history_count].content[sizeof(history[history_count].content) - 1] = '\0';
    history_count++;
}

// 打印当前对话历史（调试用）
void print_history(void)
{
    printf("\n========== 当前对话历史 ==========\n");
    for (int i = 0; i < history_count; i++)
    {
        printf("[%s]: %s\n", history[i].role, history[i].content);
    }
    printf("==================================\n\n");
}

// 构造 DeepSeek 格式的 JSON 请求体（带历史上下文）
// 使用 cJSON API 构建，自动处理转义，无需手动拼接字符串
//  cJSON_CreateObject(); cJSON_AddStringToObject();
char *construct_deepseek_json(const char *user_message)
{
    if (user_message == NULL) 
        return NULL;

    // 创建根对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    // 添加 model 字段（运行时模型名）
    cJSON_AddStringToObject(root, "model", dp_chat_get_model());

    // 创建 messages 数组
    cJSON *messages = cJSON_CreateArray();
    if (messages == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddItemToObject(root, "messages", messages);


    // 添加历史消息（cJSON_AddStringToObject 会自动转义特殊字符）
    for (int i = 0; i < history_count; i++)
    {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", history[i].role);
        cJSON_AddStringToObject(msg, "content", history[i].content);
        cJSON_AddItemToArray(messages, msg);
    }

    // 添加当前用户消息
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(messages, user_msg);

    // 添加 stream: false
    cJSON_AddFalseToObject(root, "stream");

    // 输出为无格式 JSON 字符串（cJSON 自动处理所有转义）
    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string == NULL)
    {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_Delete(root);
    return json_string;  // 调用者负责 free

}