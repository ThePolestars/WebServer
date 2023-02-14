/*
 * 基于小顶堆实现的定时器
 * author: octalzero
 */
#ifndef LST_TIMER
#define LST_TIMER

#include <netinet/in.h>
#include <time.h>

#include <unordered_map>

#include "../log/log.h"

class timer_node;
struct client_data  // 客户端连接资源
{
    sockaddr_in address;  // 客户端地址
    int sockfd;           // 套接字
    timer_node *timer;    // 定时器节点
};

class timer_node  // 定时器的节点
{
   public:
    timer_node(){};

   public:
    time_t expire;  // 任务的超时时间，这里使用绝对时间

    void (*cb_func)(client_data *);
    // 任务回调函数的指针，定义了一个变量cb_func，这个变量是一个指针，指向返回值为空，参数都是client_data*
    // 的函数的指针
    client_data *user_data;  // 用户数据
};

class time_heap  // 基于最小堆实现的定时器容器
{
   public:
    time_heap();
    ~time_heap();

    void add_timer(timer_node *timer);  // 添加定时器
    void down(int hole);  // 下沉操作，定时器应用领域不需要用到上浮
    void resize();        // 模仿vector的扩容机制
    timer_node *top();
    void pop_timer();
    void del_timer(timer_node *timer);  // 删除定时器
    void tick();                        // 脉搏函数
    bool empty() const;
    void adjust_timer(timer_node *timer);
    void swap_timer(int i, int j);

   private:
    timer_node **array;
    unordered_map<timer_node *, int> hash;
    int capacity;
    int cur_size;
};

#endif