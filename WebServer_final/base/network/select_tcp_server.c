/*
 * 使用 select 实现 IO 多路复用

 * fd_set set;
 * FD_ZERO(&set);      // 将set清零使集合中不含任何fd
 * FD_SET(fd, &set);   // 将fd加入set集合
 * FD_CLR(fd, &set);   // 将fd从set集合中清除
 * FD_ISSET(fd,&set);
 * //在调用select()函数后，用FD_ISSET来检测fd是否在set集合中，当检测到fd在set中则返回真，否则，返回假（0）
 */
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
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

    // 创建一个 fd_set 的集合，存放的是需要检测的文件描述符
    // tmp 是内核返回的，rdset 是用户态传入的（不要直接操作内核）
    fd_set rdset, tmp;
    FD_ZERO(&rdset);
    FD_SET(lfd, &rdset);
    int maxfd = lfd;

    while (1)
    {
        tmp = rdset;

        // 调用 select 系统函数，让内核帮忙检测哪些文件描述符有数据，
        // 可以设置阻塞时间, NULL 为永久阻塞
        int ret = select(maxfd + 1, &tmp, NULL, NULL, NULL);
        if (ret == -1)
        {
            perror("select");
            exit(-1);
        }
        else if (ret == 0)
        { // 没有任何改变，因此可以不执行什么操作
            continue;
        }
        else if (ret > 0)
        {
            // 检测到监听套接字有变化, 说明有新的连接加入
            if (FD_ISSET(lfd, &tmp))
            {
                // 表示有新的客户端连接进来了
                struct sockaddr_in cliaddr;
                int len = sizeof(cliaddr);
                int cfd = accept(lfd, (struct sockaddr *)&cliaddr, &len);

                // 将新的文件描述符加入到集合中
                FD_SET(cfd, &rdset);

                // 更新最大的文件描述符
                maxfd = maxfd > cfd ? maxfd : cfd;
            }
            // 依次检测连接的客户端套接字是否有变化
            for (int i = lfd + 1; i <= maxfd; i++)
            {
                if (FD_ISSET(i, &tmp))
                {
                    // 说明这个文件描述符对应的客户端发来了数据
                    char buf[1024] = {0};
                    int len = read(i, buf, sizeof(buf));
                    if (len == -1)
                    {
                        perror("read");
                        exit(-1);
                    }
                    else if (len == 0)
                    {
                        printf("client closed...\n");
                        close(i);
                        FD_CLR(i, &rdset);
                    }
                    else if (len > 0)
                    {
                        printf("read buf = %s\n", buf);
                        write(i, buf, strlen(buf) + 1);
                    }
                }
            }
        }
    }
    close(lfd);
    return 0;
}
