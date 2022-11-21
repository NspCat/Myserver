#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换，0为Proactor并发模式
};
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    
    if (!m_threads)
        throw std::exception();
    
    for (int i = 0; i < thread_number; ++i)
    {
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();//先加锁
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;//设置当前客户端连接的状态，是读还是写状态
    m_workqueue.push_back(request);//放入到任务队列
    m_queuelocker.unlock();
    m_queuestat.post();//信号量++
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();//执行run函数，工作线程阻塞等待任务队列任务
    return pool;
}

template <typename T>
void threadpool<T>::run()//线程执行函数，这里会设置http_conn类中的improv和timer_flag这两个成员变量
{
    while (true)
    {
        m_queuestat.wait();//信号量P操作--
        m_queuelocker.lock();//加锁
        if (m_workqueue.empty())//检查工作队列是否有任务
        {
            m_queuelocker.unlock();//没任务则释放锁
            continue;//重新开始循环
        }
        //取出一个任务，并释放锁
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        //任务没取出来
        if (!request)
            continue;
        /*
        Reactor模式:
            要求主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生(可读、可写)，
        若有，则立即通知工作线程(逻辑单元)，将socket可读可写事件放入请求队列，交给工作线程处理。
        */
        if (1 == m_actor_model)//Reactor并发模式
        {
            if (0 == request->m_state)//客户端连接处于读状态，则调用对应的读方法
            {
                printf("工作线程%lu正在处理读事件\n", pthread_self());
                if (request->read_once())//返回是否成功完成处理读事件
                {
                    request->improv = 1;//代表被处理完成
                    connectionRAII mysqlcon(&request->mysql, m_connPool);//给当前http连接分配一个数据库连接
                    request->process();//进行http报文解析
                }
                else//未完成读事件
                {
                    request->improv = 1;
                    request->timer_flag = 1;//设置超时标志
                }
            }
            else//客户端连接处理写状态，则调用对应的写方法
            {
                printf("工作线程%lu正在处理写事件\n", pthread_self());
                if (request->write())
                {
                    request->improv = 1;
                }
                else//未完成写事件
                {
                    request->improv = 1;//标识当前处理过
                    request->timer_flag = 1;//设置超时标志，该连接出错
                }
            }
        }
        else//Proactor并发模式
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);//从数据库连接池池中取出一个连接给mysql
            request->process();//执行
        }
        printf("工作线程%lu处理事件完成\n", pthread_self());
    }
}
#endif
