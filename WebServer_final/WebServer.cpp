/*
 * WebServer 主函数
 * author: octalzero
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>

#include "./database/sql_connection_pool.h"
#include "./http/http_conn.h"
#include "./lock/locker.h"
#include "./log/log.h"
#include "./threadpool/threadpool.h"
// #include "./timer/lst_timer.h"
#include "./timer/heap_timer.h"

#define MAX_FD 65536            // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
#define TIMESLOT 5              // 最小超时单位

#define SYNLOG  //同步写日志
//#define ASYNLOG //异步写日志

#define listenfdET  //边缘触发非阻塞
// #define listenfdLT  //水平触发阻塞

// 这三个函数在http_conn.cpp中定义，改变链接属性
// 添加需要监听的文件描述符到 epoll 中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从 epoll 中移除监听的文件描述符
extern int remove(int epollfd, int fd);
// 对文件描述符设置非阻塞
extern int setnonblocking(int fd);

// 设置定时器相关参数
static int pipefd[2];
static time_heap timer_heap;
static int epollfd = 0;

// 信号处理函数
void sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的 errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) sa.sa_flags |= SA_RESTART;  // 继续执行中断的函数
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler() {
    timer_heap.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data* user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::get_instance()->flush();
}

int main(int argc, char* argv[]) {
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 8);  // 异步日志模型
#endif

#ifdef SYNLOG
    Log::get_instance()->init("ServerLog", 2000, 800000, 0);  // 同步日志模型
#endif

    if (argc <= 1) {
        printf("usage: %s port_number\n", basename(argv[0]));
        return 1;
    }
    // 获取端口号
    int port = atoi(argv[1]);

    // 忽略 SIGPIPE 信号
    addsig(SIGPIPE, SIG_IGN);

    // 创建数据库连接池
    connection_pool* connPool = connection_pool::GetInstance();
    connPool->init("localhost", "root", "123456", "webdb", 3306, 8);

    // 创建线程池
    threadpool<http_conn>* pool = nullptr;
    try {
        pool = new threadpool<http_conn>(connPool);
    } catch (...) {
        return 1;
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[MAX_FD];
    assert(users);

    // 初始化数据库读取表
    users->initmysql_result(connPool);

    // 创建监听套接字
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // struct linger tmp={1,0};
    // SO_LINGER若有数据待发送，延迟关闭
    // setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));

    // 绑定
    int ret = 0;
    int reuse = 1;  // 设置端口复用
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    // 监听
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    // 创建 epoll 对象，和事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    // 将监听的文件描述符添加到 epoll 对象中
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;

    // 创建双向管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);  // 统一事件源

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    client_data* users_timer = new client_data[MAX_FD];

    bool timeout = false;
    alarm(TIMESLOT);

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            // epoll_wait因为设置了alarm定时触发警告，导致每次返回-1，errno为EINTR，对于这种错误返回忽略这种错误
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            // 处理新到的客户连接
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr*)&client_address,
                                    &client_addrlength);

                if (connfd < 0) {
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }

                if (http_conn::m_user_count >= MAX_FD) {  // 目前连接数满了
                    // 给客户端返回一个信息，表示服务器内部正忙
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                // 将新的客户的数据初始化，放到数组中
                users[connfd].init(connfd, client_address);

                // 初始化client_data数据
                // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到小顶堆中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                timer_node* timer = new timer_node();
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_heap.add_timer(timer);
#endif

#ifdef listenfdET
                while (1) {
                    int connfd =
                        accept(listenfd, (struct sockaddr*)&client_address,
                               &client_addrlength);
                    if (connfd < 0) {  // TODO：可以修改下捕获EAGAIN
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD) {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    // 初始化client_data数据
                    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到小顶堆中
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    timer_node* timer = new timer_node();
                    timer->user_data = &users_timer[connfd];
                    timer->cb_func = cb_func;
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_heap.add_timer(timer);
                }
                continue;
#endif
            }
            // 客户端关闭连接，移除对应的定时器
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                timer_node* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer) {
                    timer_heap.del_timer(timer);
                }
            }

            // 处理信号事件
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {  // 出错
                    continue;
                } else if (ret == 0) {  // 连接关闭
                    continue;
                } else {
                    for (int i = 0; i < ret; ++i) {  // 接收到的数据大小
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            }

            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) {
                timer_node* timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once()) {
                    LOG_INFO("deal with the client(%s)",
                             inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();
                    // 若监测到读事件，将该事件放入请求队列
                    pool->append(users + sockfd);

                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在堆上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_heap.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_heap.del_timer(timer);
                    }
                }
            } else if (events[i].events & EPOLLOUT) {
                timer_node* timer = users_timer[sockfd].timer;
                if (users[sockfd].write()) {
                    LOG_INFO("send data to the client(%s)",
                             inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::get_instance()->flush();

                    // 若有数据传输，则将定时器往后延迟3个单位
                    // 并对新的定时器在链表上的位置进行调整
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_heap.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_heap.del_timer(timer);
                    }
                }
            }
        }
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}