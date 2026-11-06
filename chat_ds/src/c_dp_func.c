#include "c_dp.h"
             


// HTTP 请求包构建函数
char *construct_http_request_packet(char *json_payload, size_t json_payload_len, char *url_addr, char *web_addr)
{
    //构建HTTP POST请求包
    char *request_packet;
    size_t request_packet_len = json_payload_len + 1024;
    
    request_packet = malloc(request_packet_len);
    if(request_packet == NULL)
    {
        perror("HTTP请求包内存申请失败");
        return NULL;
    }

    snprintf(request_packet, request_packet_len,
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Authorization: Bearer %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            url_addr, web_addr, API_KEY, json_payload_len, json_payload);
    return request_packet;
}

//接收响应报文 — 改用动态分配，由调用者 free
char *rcv_response_packet(SSL *ssl)
{
    char *rcv_buf = malloc(MAX_BUF);
    if (rcv_buf == NULL) 
    {
        perror("rcv_buf内存分配失败");
        return NULL;
    }

    ssize_t rd_size;
    size_t total_received = 0;          //追踪已经接收到的数据

    while(1)
    {
        rd_size = SSL_read(ssl, rcv_buf + total_received, MAX_BUF - total_received - 1);
        if(rd_size == 0)
        {
            break;
        }
        else if(rd_size < 0)
        {
            int ssl_error = SSL_get_error(ssl, rd_size);
            fprintf(stderr, "读取出错: SSL_get_error=%d\n", ssl_error);
            break;
        }

        total_received += rd_size;
    }

    rcv_buf[total_received] = '\0';  // 确保字符串结尾
    return rcv_buf;     //返还动态分配的内存，由主函数去free掉
}

/* Deepseek数据包
{
  "id": "b8cb157e-a117-48d0-85e1-e6671ed9a260",
  "object": "chat.completion",
  "created": 1787367660,
  "model": "deepseek-v4-flash",
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "是的，我可以说中文。有什么我可以帮您的吗？",
        "reasoning_content": "我们只需要用户要求说中文，直接回答即可。"
      },
      "logprobs": null,
      "finish_reason": "stop"
    }
  ],
  "usage": {
    "prompt_tokens": 87,
    "completion_tokens": 24,
    "total_tokens": 111,
    "prompt_tokens_details": {
      "cached_tokens": 0
    },
    "completion_tokens_details": {
      "reasoning_tokens": 11
    },
    "prompt_cache_hit_tokens": 0,
    "prompt_cache_miss_tokens": 87
  },
  "system_fingerprint": "a26a7955944dc5c60445bff77fac9c8e"
}
*/

// 解析 JSON 并提取 assistant 回复内容，返回动态分配的字符串
char *extract_assistant_reply(const char *json_string)
{
    cJSON *root = cJSON_Parse(json_string);
    if (root == NULL) 
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) 
        {
            fprintf(stderr, "JSON 解析错误，错误位置: %s\n", error_ptr);
        }
        return NULL;
    }

    char *reply = NULL;

    // 提取 choices 数组
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (choices && cJSON_IsArray(choices)) 
    {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice) 
        {
            cJSON *message = cJSON_GetObjectItemCaseSensitive(first_choice, "message");
            if (message) 
            {
                cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
                if (content && cJSON_IsString(content) && (content->valuestring != NULL)) 
                {
                    reply = strdup(content->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);
    return reply;  // 调用者负责 free
}


