#ifndef C_DP_H
#define C_DP_H

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <netdb.h>
#include <ctype.h>

//ssl库的头文件
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#include <cJSON.h>



// API Key 从本地配置文件读取（api_key.h 已加入 .gitignore，不随仓库提交）
#if defined(__has_include)
#  if __has_include("api_key.h")
#    include "api_key.h"
#  endif
#endif
#ifndef API_KEY
#define API_KEY "YOUR_API_KEY_HERE"
#endif
#define MAX_BUF (1024*512)
#define MAX_HISTORY 100    // 最大对话历史条数
#define MAX_MSG_LEN 4096   // 单条消息最大长度



//服务器信息
extern struct hostent *hostinfo;

// 对话历史消息结构
typedef struct {
    char role[16];         // "user" 或 "assistant"区分说话对象
    char content[MAX_MSG_LEN];
} Message;

// 对话历史数组
extern Message history[MAX_HISTORY];
extern int history_count;   // 当前历史条数（定义于 chat_history.c）



int  init_SSL(int tcp_fd, SSL **ssl, SSL_CTX **ctx);
void destroy_SSL(SSL *ssl, SSL_CTX *ctx);
int  connect_with_host(uint16_t port, char dest_ip[][20]);
int  get_hostinfo_and_dest_ip(char argv[], char dest_ip[][20],
                              char web_addr[], char url_addr[]);
char *construct_http_request_packet(char *json_payload,
                                    size_t json_payload_len,
                                    char *url_addr, char *web_addr);
char *construct_deepseek_json(const char *user_message);
char *rcv_response_packet(SSL *ssl);
char *extract_assistant_reply(const char *json_string);
void add_to_history(const char *role, const char *content);
void print_history(void);
int  reconnect_server(int retry_count, int *skt_fd,
                      SSL **ssl, SSL_CTX **ctx,
                      char dest_ip[][20]);

#endif