/*
* 最小堆：
将所有定时器中超时时间最小的一个定时器的超时值作为心搏间隔。
这样，一旦心搏函数tick被调用，超时时间最小的定时器必然到期，
我们就可以在tick函数中处理该定时器。
然后，再次从剩余的定时器中找出超时时间最小的一个，
并将这段最小时间设置为下一次心搏间隔。
如此反复，就实现了较为精确的定时。
*/

/*
* 下滤操作
   将该节点不断向下移动，直到合理
* 上滤操作
   将当前节点不断向上移动，直到合理
*/

#ifndef MIN_HEAP_H
#define MIN_HEAP_H

#include <iostream>
#include <netinet/in.h>
#include <ctime>

using std::exception;

//#define BUFFER_SIZE 64

//class util_timer;
class heap_timer;  //前向声明

//struct client_data
//{
//    sockaddr_in address;
//    int sockfd;
//    util_timer* timer;
//};
struct client_data {
    sockaddr_in address;
    int sockfd;
    //char buf[BUFFER_SIZE];
    heap_timer* timer;
};

//class util_timer
//{
//public:
//    util_timer() : prev(NULL), next(NULL) {}
//
//public:
//    time_t expire;//超时时间
//    void (*cb_func)(client_data*);//定时器回调函数
//    client_data* user_data;//客户端数据
//    util_timer* prev;//上个节点
//    util_timer* next;//下个节点
//};

class heap_timer {
public:
    //heap_timer(int delay) {
    //    expire = time(NULL) + delay;
    //}
public:
    time_t expire;  //定时器生效的绝对时间
    void (*cb_func)(client_data*); // 定时器的回调函数
    client_data* user_data; //用户数据
    void setIdx(int idx) {
        this->idx = idx;
    }
    int getIdx() {
        return this->idx;
    }
private:
    int idx = -1;//标记在数组中的位置
};

//class sort_timer_lst
//{
//public:
//    sort_timer_lst();
//    ~sort_timer_lst();
//
//    void add_timer(util_timer* timer);
//    void adjust_timer(util_timer* timer);
//    void del_timer(util_timer* timer);
//    void tick();//心跳函数
//
//private:
//    void add_timer(util_timer* timer, util_timer* lst_head);
//
//    util_timer* head;
//    util_timer* tail;
//};
class time_heap {
public:
    time_heap(int cap = 128) throw(std::exception);//创建cap大小的堆，使用数组模拟最大堆
    time_heap(heap_timer** init_array, int size, int capacity) throw(std::exception);//使用数组初始化堆
    ~time_heap();

public:
    //链表有更新时间节点函数，堆该如何处理
    void add_timer(heap_timer* timer) throw(std::exception);//增加定时器
    void adjust_timer(heap_timer* timer);//更新定时器
    void del_timer(heap_timer* timer);//删除定时器
    void pop_timer();
    void tick();//心跳函数
    heap_timer* top() const {
        if (empty())
            return NULL;
        return array[0];
    }
    bool empty() const {
        return cur_size == 0;
    }

private:
    //最小堆的下虑操作，确保堆数组中以第hole个节点作为根的子树拥有最小堆性质
    void percolate_down(int hole) {
        heap_timer* tmp = array[hole];
        int child = 0;
        for (; ((hole * 2 + 1) <= (cur_size - 1)); hole = child) {
            child = hole * 2 + 1;
            if ((child < cur_size - 1) && (array[child + 1]->expire < array[child]->expire)) {
                child++;
            }
            if (array[child]->expire < tmp->expire) {  // 孩子的权值还比插入的小，只能往下走了。
                array[hole] = array[child];
                array[hole]->setIdx(hole);
            }
            else
                break;
        }
        array[hole] = tmp;
        tmp->setIdx(hole);
    }
    void resize() throw(std::exception) {

        heap_timer** tmp = new heap_timer * [capacity * 2];
        for (int j = 0; j < capacity * 2; ++j) {
            tmp[j] = NULL;
        }
        if (!tmp) {
            throw std::exception();
        }
        for (int i = 0; i < cur_size; i++) {
            tmp[i] = array[i];
        }
        delete[]array;//销毁原来的数组
        capacity = capacity * 2;
        array = tmp;
    }
private:
    heap_timer** array; //堆数组
    int capacity; //堆数组的容量
    int cur_size; //堆数组包含元素的个数
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;
    time_heap m_time_heap;
    static int u_epollfd;
    int m_TIMESLOT = -1;//超时时间
};

void cb_func(client_data* user_data);

#endif //MIN_HEAP_H