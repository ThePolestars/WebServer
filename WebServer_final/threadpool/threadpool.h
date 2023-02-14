/*
 * 简易的线程池实现类
 * author: octalzero
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

#include <cstdio>
#include <exception>
#include <list>

#include "../database/sql_connection_pool.h"
#include "../lock/locker.h"

// 定义为模板类是为了代码复用，模板参数T是任务类
template <typename T>
class threadpool {
   public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(connection_pool *connPool, int thread_number = 8,
               int max_request = 10000);
    ~threadpool();
    bool append(T *request);

   private:
    // 工作线程运行的函数，它不断从工作队列中取出任务并执行
    static void *worker(void *arg);  // 声明为 static，防止传入 this
                                     // 指针，pthread 接收参数为一个 void*
    void run();

   private:
    int m_thread_number;  // 线程池中的线程数
    int m_max_requests;   // 请求队列中允许的最大请求数
    pthread_t *m_threads;  // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;   // 请求队列
    locker m_queuelocker;         // 保护请求队列的互斥锁
    sem m_queuestat;              // 是否有任务需要处理
    bool m_stop;                  // 是否结束线程
    connection_pool *m_connPool;  // 数据库
};

// 线程池的实现
template <typename T>
threadpool<T>::threadpool(connection_pool *connPool, int thread_number,
                          int max_requests)
    : m_thread_number(thread_number),
      m_max_requests(max_requests),
      m_stop(false),
      m_threads(NULL),
      m_connPool(connPool) {
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建 thread_number 个线程，并将他们设置为脱离线程。
    for (int i = 0; i < thread_number; ++i) {
        printf("create the %dth thread\n", i);
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T *request) {
    // 加锁，因为工作队列被所有线程共享。
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            // 处理虚假唤醒
            // 判断的原因：假设生产者进行了生产，所有子进程wait解除阻塞，开始抢互斥锁，假如线程A抢到了，
            // 此时，其他子线程阻塞在lock处，A线程进行下面的操作，当前信号量又恢复为0，如果线程A进行了解锁，
            // 其他抢到锁的线程共享的请求队列中其实是没有任务的了。
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) continue;

        connectionRAII mysqlcon(&request->mysql, m_connPool);

        request->process();
    }
}
#endif