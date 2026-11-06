#include "c_dp.h"



struct hostent *hostinfo = NULL;    
//初始化SSL接口
int init_SSL(int tcp_fd, SSL **ssl, SSL_CTX **ctx)
{
	int n, ret;

	/* 初始化https的SSL加密协议 */
	//加载错误字符串资源
	SSL_load_error_strings();

	//初始化SSL库
	SSL_library_init();

	//新建SSL会话
	*ctx = SSL_CTX_new(SSLv23_client_method());
	if (*ctx == NULL)
	{
		fprintf(stderr, "init SSL CTX failed:%s\n",
				ERR_reason_error_string(ERR_get_error()));
	}

	//根据会话新建SSL加密
	*ssl = SSL_new(*ctx);
	if (*ssl == NULL)
	{
		fprintf(stderr, "new SSL with created CTX failed:%s\n",
				ERR_reason_error_string(ERR_get_error()));
	}

	//绑定文件描述符，tcp_fd是连接完服务器的文件描述符
	ret = SSL_set_fd(*ssl, tcp_fd);
	if (ret == 0)
	{
		fprintf(stderr, "add SSL to tcp socket failed:%s\n",
				ERR_reason_error_string(ERR_get_error()));
	}
	
	/* 利用SSL加密连接服务器 */
	ret = SSL_connect(*ssl);
	if (ret != 1)
	{
		fprintf(stderr, "SSL connection failed:%s\n",
				ERR_reason_error_string(ERR_get_error()));
	}

	return 0;
}

//销毁SSL
void destroy_SSL(SSL *ssl, SSL_CTX *ctx)
{
    int ret;

	ret = SSL_shutdown(ssl);
    if (ret == 0)
    {
        ret = SSL_shutdown(ssl);
    }
    if (ret < 0)
	{
        int ssl_error = SSL_get_error(ssl, ret);
        fprintf(stderr, "SSL shutdown failed: SSL_get_error=%d\n", ssl_error);
	}

	SSL_free(ssl);
	SSL_CTX_free(ctx);
	ERR_free_strings();
}

//建立于主机的连接
int connect_with_host(uint16_t port, char dest_ip[][20])
{
    int skt_fd;
	int retval;

	skt_fd = socket(AF_INET, SOCK_STREAM, 0);//申请一个ipv4的tcp网络套接字
	if(skt_fd == -1)
	{
		perror("申请ipv4的tcp接口失败");
        return -1;
	}

	struct sockaddr_in addrinfo;
	addrinfo.sin_family = AF_INET;                      //指定IPV4协议
	addrinfo.sin_port = htons(port);                    //指定端口号为80端口（http的默认端口）
	addrinfo.sin_addr.s_addr = inet_addr(dest_ip[0]);   //只用其一的IP
	
	retval = connect(skt_fd, (struct sockaddr *)&addrinfo, sizeof(addrinfo));//连接服务器
	if(retval == -1)
	{
		perror("接受客户端连接失败");
        return -1;
	}
	//printf("client:连接服务器成功\n");

    return skt_fd;
}

//通过网址来取得域名和URL，并且将信息导入hostinfo，
int get_hostinfo_and_dest_ip(char argv[], char dest_ip[][20], char web_addr[], char url_addr[])
{
    char *pos;

    pos = strstr(argv, "/"); //读取到"/"然后返回它所在的地址
    
    strncpy(web_addr, argv, pos-argv);
    strcpy(url_addr, pos);

    //printf("web = %s\nurl = %s\n", web_addr, url_addr);

    //gethostbyname()通过域名查询服务器信息的系统函数
    hostinfo = gethostbyname(web_addr);
    if(hostinfo == NULL)
    {
        perror("域名解析失败");
        return -1;
    }

    //获得服务器的所有IP地址并打印出来
    for(int ip_count = 0; hostinfo->h_addr_list[ip_count] != NULL; ip_count++)
    {
        strcpy(dest_ip[ip_count], inet_ntoa(*(struct in_addr *)hostinfo->h_addr_list[ip_count]));
        
        //printf("对方IP地址：%s\n", dest_ip[ip_count]);
    }

    return 1;   
}

//网络重连尝试
int reconnect_server(int retry_count, int *skt_fd, SSL **ssl, SSL_CTX **ctx, char dest_ip[][20])
{
    int retval;

    printf("网络波动，正在进行第%d次重试...\n", retry_count);

    // 重试时需要重新建立连接（SSL资源已在失败时清理）
    close(*skt_fd);

    *skt_fd = connect_with_host(443, dest_ip);
    if(*skt_fd == -1) 
        return -1;

    retval = init_SSL(*skt_fd, ssl, ctx);
    if(retval == -1)
    {
        close(*skt_fd);
        return -1;
    }

    return 0;
}