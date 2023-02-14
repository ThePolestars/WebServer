/*
 * 使用 epoll 实现 IO 多路复用
 *
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

int main()
{
    int lfd = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr;
    saddr.sin_port = htons(9999);
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));
    listen(lfd, 8);

    // 调用 epoll_create() 创建一个 epoll 实例, 参数无意义 > 0 即可
    int epfd = epoll_create(1);

    // 将监听的文件描述符相关的检测信息添加到epoll实例中
    struct epoll_event epev;
    epev.events = EPOLLIN;
    epev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &epev);
    struct epoll_event epevs[1024];

    while (1)
    {
        // 检测函数，返回发生变化的文件描述符数量
        int ret = epoll_wait(epfd, epevs, 1024, -1);
        if (ret == -1)
        {
            perror("epoll_wait");
            exit(-1);
        }

        printf("change fd nums = %d\n", ret); // 打印改变的文件描述符数量

        for (int i = 0; i < ret; i++)
        {
            int curfd = epevs[i].data.fd;

            if (curfd == lfd)
            {
                // 监听的文件描述符有数据达到，有客户端连接
                struct sockaddr_in cliaddr;
                int len = sizeof(cliaddr);
                int cfd = accept(lfd, (struct sockaddr *)&cliaddr, &len);

                epev.events = EPOLLIN;
                epev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &epev);
            }
            else
            {
                if (epevs[i].events &
                    EPOLLOUT)
                { // 排除EPOLLOUT的情况，专注处理读的情况，
                  // 如果有其余情况，需要做其他的位运算判断
                    continue;
                }
                // 有数据到达，需要通信
                char buf[1024] = {0};
                int len = read(curfd, buf, sizeof(buf));
                if (len == -1)
                {
                    perror("read");
                    exit(-1);
                }
                else if (len == 0)
                {
                    printf("client closed...\n");
                    epoll_ctl(epfd, EPOLL_CTL_DEL, curfd,
                              NULL); // 关闭前将文件描述符从红黑树中删除
                    close(curfd);
                }
                else if (len > 0)
                {
                    printf("read buf = %s\n", buf);
                    write(curfd, buf, strlen(buf) + 1);
                }
            }
        }
    }

    close(lfd);
    close(epfd);
    return 0;
}
