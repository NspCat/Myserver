//
//#ifndef LST_TIMER
//#define LST_TIMER
//
//#include <unistd.h>
//#include <signal.h>
//#include <sys/types.h>
//#include <sys/epoll.h>
//#include <fcntl.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <assert.h>
//#include <sys/stat.h>
//#include <string.h>
//#include <pthread.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <sys/mman.h>
//#include <stdarg.h>
//#include <errno.h>
//#include <sys/wait.h>
//#include <sys/uio.h>
//
//#include <time.h>
//#include "../log/log.h"
//
//class util_timer;
//
//struct client_data
//{
//    sockaddr_in address;
//    int sockfd;
//    util_timer *timer;
//};
//
//class util_timer
//{
//public:
//    util_timer() : prev(NULL), next(NULL) {}
//
//public:
//    time_t expire;//超时时间
//    
//    //函数指针，参数为client_data*类型
//    //通常作为回调函数的形参，调用者传递指定函数名给回调函数的函数指针，回调函数不需要知道函数指针内容，执行即可
//    /* 回调函数就是一个通过指针函数调用的函数。其将函数指针作为一个参数，传递给另一个函数。
//    回调函数并不是由实现方直接调用，而是在特定的事件或条件发生时由另外一方来调用的*/
//    //提高效率
//    void (* cb_func)(client_data *);//这是一个指向函数地址的指针
//    /*初始化：对象->cb_fun = 函数名*/
//    client_data *user_data;//客户端数据
//    util_timer *prev;//上个节点
//    util_timer *next;//下个节点
//};
//
//class sort_timer_lst
//{
//public:
//    sort_timer_lst();
//    ~sort_timer_lst();
//
//    void add_timer(util_timer *timer);
//    void adjust_timer(util_timer *timer);
//    void del_timer(util_timer *timer);
//    void tick();//心跳函数
//
//private:
//    void add_timer(util_timer *timer, util_timer *lst_head);
//
//    util_timer *head;
//    util_timer *tail;
//};
//
//class Utils
//{
//public:
//    Utils() {}
//    ~Utils() {}
//
//    void init(int timeslot);
//
//    //对文件描述符设置非阻塞
//    int setnonblocking(int fd);
//
//    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
//    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
//
//    //信号处理函数
//    static void sig_handler(int sig);
//
//    //设置信号函数
//    void addsig(int sig, void(handler)(int), bool restart = true);
//
//    //定时处理任务，重新定时以不断触发SIGALRM信号
//    void timer_handler();
//
//    void show_error(int connfd, const char *info);
//
//public:
//    static int *u_pipefd;
//    sort_timer_lst m_timer_lst;
//    static int u_epollfd;
//    int m_TIMESLOT;//超时时间
//};
//
//void cb_func(client_data *user_data);
//
//#endif
