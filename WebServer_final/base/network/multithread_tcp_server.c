/*
 * 基于 linux socket 的多线程 tcp server 实现
 * author: octalzero
 */
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// 封装需要传递到子线程回调函数的信息
struct sockInfo {
    int fd;  // 通信的文件描述符
    struct sockaddr_in addr;
    pthread_t tid;  // 线程号
};

struct sockInfo sockinfos[128];

// 子线程和客户端通信的回调函数
void *working(void *arg) {
    // 获取客户端的信息
    struct sockInfo *pinfo = (struct sockInfo *)arg;

    // 打印客户端信息
    char clnt_ip[16];
    bzero(&clnt_ip, sizeof clnt_ip);
    inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, clnt_ip, sizeof clnt_ip);
    unsigned short clnt_port = ntohs(pinfo->addr.sin_port);
    printf("client ip: %s, port: %d\n", clnt_ip, clnt_port);

    // 5. 通信
    char recv_buf[1024];
    bzero(&recv_buf, sizeof recv_buf);
    int num = 0;
    while (1) {
        // 读取客户端数据
        int recv_len = read(pinfo->fd, recv_buf, sizeof recv_buf);
        if (recv_len == -1) {
            perror("read");
            exit(-1);
        } else if (recv_len > 0) {
            printf("recv client data: %s\n", recv_buf);
            // 给客户端发送数据
            char send_buf[1024];
            bzero(&send_buf, sizeof send_buf);
            sprintf(send_buf, "Message %d received!", ++num);
            write(pinfo->fd, send_buf, sizeof send_buf);
        } else if (recv_len == 0) {
            printf("client closed...\n");
            break;
        }
    }

    // 关闭套接字
    close(pinfo->fd);

    return NULL;
}

int main() {
    // 1.创建一个套接字
    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) {
        perror("socket");
        exit(-1);
    }

    // 2.绑定:将套接字与特定的 IP 地址和端口绑定。流经这个 IP
    // 和端口的数据交给套接字处理
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(9999);
    int res = bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof serv_addr);
    if (res == -1) {
        perror("bind");
        exit(-1);
    }

    // 3. 监听：睡眠直到客户端发起请求，第二个参数为请求队列的最大长度
    res = listen(serv_sock, 128);
    if (res == -1) {
        perror("listen");
        exit(-1);
    }

    // 初始化回调函数数据
    int max = sizeof(sockinfos) / sizeof(sockinfos[0]);
    for (int i = 0; i < max; i++) {
        bzero(&sockinfos[i], sizeof(sockinfos[i]));
        sockinfos[i].fd = -1;  // 标记-1表示未被占用
        sockinfos[i].tid = -1;
    }

    // 4. 不断循环等待客户端连接，一旦一个客户端连接，就创建一个子线程进行通信
    while (1) {
        struct sockaddr_in clnt_addr;
        socklen_t clnt_len = sizeof clnt_addr;
        int clnt_sock =
            accept(serv_sock, (struct sockaddr *)&clnt_addr, &clnt_len);
        if (clnt_sock == -1) {
            perror("accept");
            exit(-1);
        }

        struct sockInfo *pinfo;
        for (int i = 0; i < max; i++) {
            // 从这个数组中找到一个可以用的sockInfo元素
            if (sockinfos[i].fd == -1) {
                pinfo = &sockinfos[i];
                break;
            }
            if (i == max - 1) {  // 防止找不到可用的sockInfo, 又去创建了子线程
                sleep(1);
                i--;
            }
        }

        pinfo->fd = clnt_sock;
        memcpy(&pinfo->addr, &clnt_addr, sizeof clnt_addr);

        // 创建子线程
        pthread_create(&pinfo->tid, NULL, working, pinfo);

        // 分离线程，在终止的时候，会自动释放资源返回给系统
        pthread_detach(pinfo->tid);
    }

    close(serv_sock);

    return 0;
}