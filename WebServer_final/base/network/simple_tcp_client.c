/*
 * 基于 linux socket 的简单的 tcp client 实现

 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

int main()
{
    // 1.创建一个套接字
    int clnt_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (clnt_sock == -1)
    {
        perror("socket");
        exit(-1);
    }

    // 2. 向服务器发起请求,服务器的 IP 地址和端口号保存在 sockaddr_in 结构体中
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr.s_addr);
    serv_addr.sin_port = htons(9999);
    int res =
        connect(clnt_sock, (struct sockaddr *)&serv_addr, sizeof serv_addr);
    if (res == -1)
    {
        perror("connect");
        exit(-1);
    }

    int num = 0;
    while (1)
    {
        // 3. 发送消息给服务器
        char send_buf[1024];
        bzero(&send_buf, sizeof send_buf);
        sprintf(send_buf, "Message %d\n", ++num);
        write(clnt_sock, send_buf, sizeof send_buf);

        // 4. 接收服务器发送的消息
        char recv_buf[1024];
        bzero(&recv_buf, sizeof recv_buf);
        int recv_len = read(clnt_sock, recv_buf, sizeof recv_buf);
        if (recv_len == -1)
        {
            perror("read");
            exit(-1);
        }
        else if (recv_len > 0)
        {
            printf("recv server data: %s\n", recv_buf);
        }
        else if (recv_len == 0)
        {
            printf("server closed...\n");
            break;
        }

        usleep(1000);
    }

    // 关闭套接字
    close(clnt_sock);

    return 0;
}