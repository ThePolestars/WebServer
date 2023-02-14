#include "heap_timer.h"

#include "../http/http_conn.h"

time_heap::time_heap() {
    cur_size = 0;
    capacity = 10000;  // 初始容量设置为10000
    array = new timer_node *[capacity];
    for (int i = 0; i < capacity; i++) {
        array[i] = NULL;
    }
}

time_heap::~time_heap() {
    for (int i = 0; i < cur_size; i++) {
        delete array[i];
    }
    delete[] array;
}

// 添加定时器
void time_heap::add_timer(timer_node *timer) {
    if (!timer) {
        return;
    }
    if (cur_size >= capacity)  // 如果当前堆数组容量不够就进行扩容
    {
        resize();
    }
    int cur = cur_size++;
    int parent = 0;
    for (; cur > 0; cur = parent) {
        parent = (cur - 1) / 2;
        if (array[parent]->expire <= timer->expire) {
            break;
        }
        array[cur] = array[parent];
    }
    hash[timer] = cur;
    array[cur] = timer;
}

// 删除定时器
void time_heap::del_timer(timer_node *timer) {
    if (!timer) {
        return;
    }
    timer->cb_func = NULL;  // 延时删除
}

timer_node *time_heap::top() {
    if (empty()) {
        return NULL;
    }
    return array[0];
}

void time_heap::pop_timer() {
    if (empty()) {
        return;
    }
    if (array[0]) {
        hash.erase(array[0]);
        delete array[0];
        cur_size--;
        hash[array[cur_size]] = 0;
        array[0] = array[cur_size];
        down(0);
    }
}

void time_heap::adjust_timer(timer_node *timer) { down(hash[timer]); }

void time_heap::swap_timer(int i, int j) {
    std::swap(array[i], array[j]);
    hash[array[i]] = j;
    hash[array[j]] = i;
}

// 下沉操作，父节点与子节点比较
void time_heap::down(int x) {
    int lc = 2 * x + 1, rc = 2 * x + 2;
    int max = x;

    if ((lc < cur_size - 1) && array[lc]->expire < array[max]->expire) max = lc;
    if ((rc < cur_size - 1) && array[rc]->expire < array[max]->expire) max = rc;
    if (max != x) {
        swap_timer(x, max);
        down(max);
    }
}

void time_heap::resize() {
    timer_node **temp = new timer_node *[2 * capacity];
    for (int i = 0; i < 2 * capacity; i++) {
        temp[i] = NULL;
    }
    capacity = 2 * capacity;
    for (int i = 0; i < cur_size; ++i) {
        temp[i] = array[i];
    }
    delete[] array;
    array = temp;
}

// SIGALARM信号每次触发就在其信号处理函数中执行一次tick函数，以处理堆上到期的任务
void time_heap::tick() {
    timer_node *tmp = array[0];
    time_t cur = time(NULL);
    while (!empty()) {
        if (!tmp) {
            break;
        }
        if (tmp->expire > cur) {
            break;
        }
        if (array[0]->cb_func) {
            array[0]->cb_func(array[0]->user_data);
        }
        pop_timer();
        tmp = array[0];
    }
}

bool time_heap::empty() const { return cur_size == 0; }