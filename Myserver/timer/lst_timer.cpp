//#include "lst_timer.h"
//#include "../http/http_conn.h"
//
//sort_timer_lst::sort_timer_lst()
//{
//    head = NULL;
//    tail = NULL;
//}
//sort_timer_lst::~sort_timer_lst()
//{
//    util_timer *tmp = head;
//    while (tmp)
//    {
//        head = tmp->next;
//        delete tmp;
//        tmp = head;
//    }
//}
//
//void sort_timer_lst::add_timer(util_timer *timer)
//{
//    if (!timer)
//    {
//        return;
//    }
//    if (!head)
//    {
//        head = tail = timer;
//        return;
//    }
//    if (timer->expire < head->expire)//直接当头节点
//    {
//        timer->next = head;
//        head->prev = timer;
//        head = timer;
//        return;
//    }
//    add_timer(timer, head);
//}
//void sort_timer_lst::adjust_timer(util_timer *timer)
//{
//    if (!timer)
//    {
//        return;
//    }
//    util_timer *tmp = timer->next;
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
//void sort_timer_lst::del_timer(util_timer *timer)
//{
//    if (!timer)
//    {
//        return;
//    }
//    if ((timer == head) && (timer == tail))
//    {
//        delete timer;
//        head = NULL;
//        tail = NULL;
//        return;
//    }
//    if (timer == head)
//    {
//        head = head->next;
//        head->prev = NULL;
//        delete timer;
//        return;
//    }
//    if (timer == tail)
//    {
//        tail = tail->prev;
//        tail->next = NULL;
//        delete timer;
//        return;
//    }
//    timer->prev->next = timer->next;
//    timer->next->prev = timer->prev;
//    delete timer;
//}
//void sort_timer_lst::tick()
//{
//    if (!head)
//    {
//        return;
//    }
//    
//    time_t cur = time(NULL);
//    util_timer *tmp = head;
//    while (tmp)//升序链表，不断断开惰性连接
//    {
//        if (cur < tmp->expire)
//        {
//            break;
//        }
//        tmp->cb_func(tmp->user_data);//执行定时器的回调函数，将客户连接关闭
//        head = tmp->next;//不断更新头节点
//        if (head)
//        {
//            head->prev = NULL;
//        }
//        delete tmp;
//        tmp = head;
//    }
//}
//
//void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
//{
//    util_timer *prev = lst_head;
//    util_timer *tmp = prev->next;
//    while (tmp)
//    {
//        if (timer->expire < tmp->expire)
//        {
//            prev->next = timer;
//            timer->next = tmp;
//            tmp->prev = timer;
//            timer->prev = prev;
//            break;
//        }
//        prev = tmp;
//        tmp = tmp->next;
//    }
//    if (!tmp)
//    {
//        prev->next = timer;
//        timer->prev = prev;
//        timer->next = NULL;
//        tail = timer;
//    }
//}
//
//void Utils::init(int timeslot)
//{
//    m_TIMESLOT = timeslot;
//}
//
////对文件描述符设置非阻塞
//int Utils::setnonblocking(int fd)
//{
//    //F_GETFL——> | 功能——>F_SETFL更新
//    int old_option = fcntl(fd, F_GETFL);
//    int new_option = old_option | O_NONBLOCK;
//    fcntl(fd, F_SETFL, new_option);
//    return old_option;
//}
//
////将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
//void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
//{
//    epoll_event event;
//    event.data.fd = fd;
//
//    if (1 == TRIGMode)//ET触发模式，非阻塞高效率，反复询问
//        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//RDHUP：对方挂断
//    else
//        event.events = EPOLLIN | EPOLLRDHUP;
//
//    if (one_shot)//是否设置为ONESHOT，即该事件只触发一次，再次触发需要epoll_ctl_mod
//        event.events |= EPOLLONESHOT;
//    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);//加入兴趣列表
//    setnonblocking(fd);//设置文件描述符为非阻塞
//}
//
////信号处理函数
//void Utils::sig_handler(int sig)
//{
//    //为保证函数的可重入性，保留原来的errno
//    int save_errno = errno;
//    int msg = sig;
//    send(u_pipefd[1], (char *)&msg, 1, 0);//发送管道写端信号信息，1个字节大小，0看成write函数
//    errno = save_errno;
//}
//
////添加信号到信号捕捉
//void Utils::addsig(int sig, void(handler)(int), bool restart)//默认restart为true
//{
//    /*
//    struct sigaction {
//    void	(*sa_handler)(int);//信号处理函数
//    -----
//    sigset_t	sa_mask;//该信号集保存调用信号处理函数时所要屏蔽的信号
//    int 		sa_flags;//类似open打开标志
//    -----
//    };
//    */
//    struct sigaction sa;
//    memset(&sa, '\0', sizeof(sa));
//    sa.sa_handler = handler;
//    if (restart)
//        sa.sa_flags |= SA_RESTART;//在信号返回时重新启动系统调用。
//    sigfillset(&sa.sa_mask);//将信号集所有信号置1
//    assert(sigaction(sig, &sa, NULL) != -1);//设置信号捕捉
//}
//
////定时处理任务，重新定时以不断触发SIGALRM信号
//void Utils::timer_handler()
//{
//    m_timer_lst.tick();//执行对应的心跳函数
//    alarm(m_TIMESLOT);//重新设置倒计时
//}
//
//void Utils::show_error(int connfd, const char *info)//给客户端发送错误信息
//{
//    send(connfd, info, strlen(info), 0);//发送数据
//    close(connfd);
//}
//
//int *Utils::u_pipefd = 0;//保存管道描述符
//int Utils::u_epollfd = 0;//保存epoll描述符
//
//class Utils;
////定时器回调函数，将该定时器节点关闭
//void cb_func(client_data *user_data)
//{
//    //将对应的文件描述符从epoll请求列表上摘除
//    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
//    assert(user_data);//确保存在这个用户连接
//    close(user_data->sockfd);//关闭连接
//    http_conn::m_user_count--;//连接总数-1
//}
