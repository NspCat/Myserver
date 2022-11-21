#include "heaptimer.h"
#include "../http/http_conn.h"

//创建堆并初始化所有元素
time_heap::time_heap(int cap) throw(std::exception) : capacity(cap), cur_size(0) {
    array = new heap_timer * [capacity];  //创建堆数组
    if (!array)
        throw std::exception();
    for (int i = 0; i < capacity; ++i) {
        array[i] = NULL;
    }
}

time_heap::time_heap(heap_timer** init_array, int size, int capacity) throw(std::exception) : cur_size(size), capacity(capacity) {
    if (capacity < size) {
        throw std::exception();
    }
    array = new heap_timer * [capacity];
    if (array)
        throw std::exception();
    for (int i = 0; i < capacity; ++i) {
        array[i] = NULL;
    }
    if (size > 0) {
        for (int i = 0; i < size; ++i) {
            array[i] = init_array[i];
            percolate_down(i);
        }
    }
}

time_heap::~time_heap() {
    for (int i = 0; i < cur_size; ++i) {
        delete array[i];
    }
    delete[]array;
}

void time_heap::add_timer(heap_timer* timer) throw(std::exception) {
    if (!timer)
        return;
    if (cur_size > capacity)
        resize();
    int hole = cur_size++;//当前待插入位置
    int parent = 0;

    for (; hole > 0; hole = parent) {//进行上滤操作
        parent = (hole - 1) / 2;
        if (array[parent]->expire <= timer->expire) {
            break;
        }
        else
        {
            array[hole] = array[parent];
            array[hole]->setIdx(hole);  //更新位置
        }
        
    }
    array[hole] = timer;
    timer->setIdx(hole);
}

void time_heap::del_timer(heap_timer* timer) {
    if (!timer)
        return;
    /**
     * 仅仅将回调函数设置为空，即延迟销毁，这将节省真正删除该定时器造成的开销，但这样做使得堆数组容易膨胀。
     */
    timer->cb_func = NULL;
}

void time_heap::pop_timer() {
    if (empty())
        return;
    if (array[0]) {
        delete array[0];
        array[0] = array[--cur_size];
        percolate_down(0);// 对新的堆顶元素执行下虑操作
    }
}

//void sort_timer_lst::adjust_timer(util_timer* timer)
//{
//    if (!timer)
//    {
//        return;
//    }
//    util_timer* tmp = timer->next;
//    if (!tmp || (timer->expire < tmp->expire))
//    {
//        return;
//    }
//    if (timer == head)
//    {
//        head = head->next;
//        head->prev = NULL;
//        timer->next = NULL;
//        add_timer(timer, head);
//    }
//    else
//    {
//        timer->prev->next = timer->next;
//        timer->next->prev = timer->prev;
//        add_timer(timer, timer->next);
//    }
//}


//更新时间放入节点，执行下虑操作,确保放入合适位置
void time_heap::adjust_timer(heap_timer* timer) {
    //找到定时器位置，最好重构定时器类记录位置
    int idx = timer->getIdx();
    //代表定时器不存在
    if (idx == -1) {
        return;
    }
    //执行下滤操作
    percolate_down(idx);
}
/**
 * 心搏函数
 */
void time_heap::tick() {
    heap_timer* tmp = array[0];
    time_t cur = time(NULL);
    //确保数组不为空
    while (!empty()) {
        if (!tmp)
            break;
        if (tmp->expire > cur) {
            break;
        }
        if (array[0]->cb_func) {//确认回调函数不为NULL
            
            array[0]->cb_func(array[0]->user_data);
        }
        pop_timer();
        tmp = array[0];
        tmp->setIdx(0);
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

//对文件描述符设置非阻塞
int Utils::setnonblocking(int fd)
{
    //F_GETFL——> | 功能——>F_SETFL更新
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)//ET触发模式，非阻塞高效率，反复询问
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//RDHUP：对方挂断
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)//是否设置为ONESHOT，即该事件只触发一次，再次触发需要epoll_ctl_mod
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);//加入兴趣列表
    setnonblocking(fd);//设置文件描述符为非阻塞
}

//信号处理函数
void Utils::sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);//发送管道写端信号信息，1个字节大小，0看成write函数
    errno = save_errno;
}

//添加信号到信号捕捉
void Utils::addsig(int sig, void(handler)(int), bool restart)//默认restart为true
{
    /*
    struct sigaction {
    void	(*sa_handler)(int);//信号处理函数
    -----
    sigset_t	sa_mask;//该信号集保存调用信号处理函数时所要屏蔽的信号
    int 		sa_flags;//类似open打开标志
    -----
    };
    */
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;//在信号返回时重新启动系统调用。
    sigfillset(&sa.sa_mask);//将信号集所有信号置1
    assert(sigaction(sig, &sa, NULL) != -1);//设置信号捕捉
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_time_heap.tick();//执行对应的心跳函数
    //获取到堆顶剩余时间并设置倒计时
    if (m_time_heap.empty())
    {
        //若为空，关闭定时器，有新的客户建立连接重新开启
        m_TIMESLOT = -1;
        alarm(0);
        printf("闹钟关闭了\n");
    }
    else
    {
        m_TIMESLOT = m_time_heap.top()->expire - time(NULL);
        if (m_TIMESLOT <= 0)//代表此时头节点超时
        {
            alarm(0.1);
        }
        else
            alarm(m_TIMESLOT);//重新设置倒计时
    }
}

void Utils::show_error(int connfd, const char* info)//给客户端发送错误信息
{
    send(connfd, info, strlen(info), 0);//发送数据
    close(connfd);
}

int* Utils::u_pipefd = 0;//保存管道描述符
int Utils::u_epollfd = 0;//保存epoll描述符

class Utils;
//定时器回调函数，将该定时器节点关闭
void cb_func(client_data* user_data)
{
    //将对应的文件描述符从epoll请求列表上摘除
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);//确保存在这个用户连接
    close(user_data->sockfd);//关闭连接
    http_conn::m_user_count--;//连接总数-1
    printf("******客户端fd=%d被关闭了******\n", user_data->sockfd);
}



