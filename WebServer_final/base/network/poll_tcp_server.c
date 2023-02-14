/*
 * 使用 poll 实现 IO 多路复用

 */
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    // 初始化检测的文件描述符数组
    struct pollfd fds[1024];
    for (int i = 0; i < 1024; i++)
    {
        fds[i].fd = -1;
        fds[i].events = POLLIN;
    }
    fds[0].fd = lfd;
    int nfds = 0;

    while (1)
    {
        // 调用 poll 系统函数，让内核帮忙检测哪些文件描述符有数据
        int ret = poll(fds, nfds + 1, -1);
        if (ret == -1)
        {
            perror("poll");
            exit(-1);
        }
        else if (ret == 0)
        { // 没有任何改变，因此可以不执行什么操作
            continue;
        }
        else if (ret > 0)
        {
            // 说明检测到了有文件描述符的对应的缓冲区的数据发生了改变
            if (fds[0].revents & POLLIN)
            { // 可能同时有其他事件&操作检测
                // 表示有新的客户端连接进来了
                struct sockaddr_in cliaddr;
                int len = sizeof(cliaddr);
                int cfd = accept(lfd, (struct sockaddr *)&cliaddr, &len);

                // 将新的文件描述符加入到集合中
                for (int i = 1; i < 1024; i++)
                {
                    if (fds[i].fd == -1)
                    {
                        fds[i].fd = cfd;
                        fds[i].events = POLLIN;
                        break;
                    }
                }

                // 更新最大的文件描述符的索引
                nfds = nfds > cfd ? nfds : cfd;
            }

            for (int i = 1; i <= nfds; i++)
            {
                if (fds[i].revents & POLLIN)
                {
                    // 说明这个文件描述符对应的客户端发来了数据
                    char buf[1024] = {0};
                    int len = read(fds[i].fd, buf, sizeof(buf));
                    if (len == -1)
                    {
                        perror("read");
                        exit(-1);
                    }
                    else if (len == 0)
                    {
                        printf("client closed...\n");
                        close(fds[i].fd);
                        fds[i].fd = -1;
                    }
                    else if (len > 0)
                    {
                        printf("read buf = %s\n", buf);
                        write(fds[i].fd, buf, strlen(buf) + 1);
                    }
                }
            }
        }
    }
    close(lfd);
    return 0;
}
