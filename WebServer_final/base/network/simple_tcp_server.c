/*
 * 基于 linux socket 的简单的 tcp server 实现

 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

int main()
{
    // 1.创建一个套接字
    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1)
    {
        perror("socket");
        exit(-1);
    }

    // 端口复用
    int optval = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    // 2.绑定:将套接字与特定的 IP 地址和端口绑定。流经这个 IP
    // 和端口的数据交给套接字处理
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(9999);
    int res = bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof serv_addr);
    if (res == -1)
    {
        perror("bind");
        exit(-1);
    }

    // 3. 监听：睡眠直到客户端发起请求，第二个参数为请求队列的最大长度
    res = listen(serv_sock, 20);
    if (res == -1)
    {
        perror("listen");
        exit(-1);
    }

    // 4. 接收客户端的请求，返回客户端的套接字
    struct sockaddr_in clnt_addr;
    socklen_t clnt_len = sizeof clnt_addr;
    int clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_len);
    if (clnt_sock == -1)
    {
        perror("accept");
        exit(-1);
    }

    // 打印客户端信息
    char clnt_ip[16];
    bzero(&clnt_ip, sizeof clnt_ip);
    inet_ntop(AF_INET, &clnt_addr.sin_addr.s_addr, clnt_ip, sizeof clnt_ip);
    unsigned short clnt_port = ntohs(clnt_addr.sin_port);
    printf("client ip: %s, port: %d\n", clnt_ip, clnt_port);

    // 5. 通信
    char recv_buf[1024];
    bzero(&recv_buf, sizeof recv_buf);
    int num = 0;
    while (1)
    {
        // 读取客户端数据
        int recv_len = read(clnt_sock, recv_buf, sizeof recv_buf);
        if (recv_len == -1)
        {
            perror("read");
            exit(-1);
        }
        else if (recv_len > 0)
        {
            printf("recv client data: %s\n", recv_buf);
            // 给客户端发送数据
            char send_buf[1024];
            bzero(&send_buf, sizeof send_buf);
            sprintf(send_buf, "Message %d received!", ++num);
            write(clnt_sock, send_buf, sizeof send_buf);
        }
        else if (recv_len == 0)
        {
            printf("client closed...\n");
            break;
        }
    }

    // 关闭套接字
    close(clnt_sock);
    close(serv_sock);

    return 0;
}