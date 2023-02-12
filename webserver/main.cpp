#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65536           // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000 // 监听的最大的事件数量

// 添加epoll文件描述符函数
extern void addfd(int epollfd, int fd, bool one_shot);
// 删除epoll文件描述符
extern void removefd(int epollfd, int fd);

void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa)); // 将sa中所有数据都置为0
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask); // 设置临时阻塞信号集
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[])
{

    if (argc <= 1)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    // 获取端口号
    int port = atoi(argv[1]); // 转换成整数
    // 对SIGPIPE做信号处理
    addsig(SIGPIPE, SIG_IGN);
    // 创建线程池，初始化线程池，http_con为任务类
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }
    catch (...)
    {
        return 1;
    }
    // 创建一个数组用于保存所有的客户信息
    http_conn *users = new http_conn[MAX_FD];
    // 创建监听的套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    // 端口复用，在绑定之前设置
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 绑定
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    // 监听
    ret = listen(listenfd, 5);

    // 创建epoll对象，和事件数组，添加监听的文件描述符
    epoll_event events[MAX_EVENT_NUMBER]; // 最大监听的最大事件数量
    // 创建epoll对象
    int epollfd = epoll_create(5);
    // 添加到epoll对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    while (true)
    {
        // 主线程循环监测有无事件发生
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);

        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        // 循环遍历事件数
        for (int i = 0; i < number; i++)
        {
            // 监听到的文件描述符
            int sockfd = events[i].data.fd;
            // 有客户端连接
            if (sockfd == listenfd)
            {

                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);

                if (connfd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }

                if (http_conn::m_user_count >= MAX_FD) // 目前连接数满
                {
                    close(connfd); // 关闭连接
                    // 目前连接满
                    // 给客户端写一个信息：服务器正满
                    continue;
                }
                // 将新的客户数据初始化，放到数组
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN) // 是否有读的事件发生
            {

                if (users[sockfd].read())
                {                                 // 一次性把所有数据读完
                    pool->append(users + sockfd); // 交给工作线程处理
                }
                else
                { // 关闭连接
                    users[sockfd].close_conn();
                }
            }
            else if (events[i].events & EPOLLOUT)
            {

                if (!users[sockfd].write()) // 写事件，一次性写完所有数据
                {
                    users[sockfd].close_conn(); // 关闭连接
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;
    return 0;
}