#include "webserver.h"
static int n = 1;
WebServer::WebServer()//构造函数
{
    //http_conn类对象
    users = new http_conn[MAX_FD];

    //root文件夹路径:  m_root = 当前目录/root
    char server_path[200];
    getcwd(server_path, 200);//获取当前文件工作目录到server_path，大小不得超过size200
    char root[6] = "/root";//使用root存放目录
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器初始化
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()//析构函数,关闭文件描述符并销毁内存
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;//端口号
    m_user = user;//数据库用户名
    m_passWord = passWord;//数据库密码
    m_databaseName = databaseName;//数据库名
    m_sql_num = sql_num;//数据库连接总数
    m_thread_num = thread_num;//线程总数
    m_log_write = log_write;//日志同步/异步标志
    m_OPT_LINGER = opt_linger;//优雅关闭连接标志
    m_TRIGMode = trigmode;//事件触发模式
    m_close_log = close_log;//日志开/关标志
    m_actormodel = actor_model;//并发模型选择标志
}

void WebServer::trig_mode()
{
    //LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    //LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    //ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    //ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write()//初始化日志入口
{
    if (0 == m_close_log)//判断是否开启
    {
        //初始化日志
        if (1 == m_log_write)//调用异步日志，获取唯一的单例进行初始化
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else                 //调用同步日志
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::sql_pool()
{
    //初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    //localhost: 127.0.0.1
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()//将服务器套接字放入epoll监听的兴趣列表
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);//创建网络套接字
    assert(m_listenfd >= 0);//断言函数，当参数不符合条件输出assert(m_listenfd>=0);
    /*
    优雅关闭连接：
    当连接即将关闭时，会将待发送缓冲区中的数据全部发送完成后再关闭
    根据linger数据结构内变量值不同，主要分为以下情况：
    i_onof  i_linger    close行为                             发送队列                底层行为
    0         任意       立即返回                          保持直至发送完成      正常的close
    非0        0         立即返回                          立即放弃              直接发送RST包，自身即可复位，不用经过2MSL状态，对端收到复位错误号
    非零      非零  直到超时或数据发送完（套接字阻塞）     超时时间内尝试发送    超时则会发生第二种情况
    */
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)//代表使用了优雅关闭连接
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;//用于所有返回值判断
    struct sockaddr_in address;//地址结果类型，存地址族、ip、端口，需要被类型转换为struct sockaddr*类型（被淘汰）
    bzero(&address, sizeof(address));//清空内存，防止存在垃圾数据
    address.sin_family = AF_INET;//IPv4地址族
    /*
    大端存储：高地址内存存低地址数据，低地址内存存高地址数据
    小端存储：低地址存高地址，高地址存低地址
    ps：（从左到右）内存从低到高，二进制数据从高到低
    hton：host to network 本地转网络
    */
    address.sin_addr.s_addr = htonl(INADDR_ANY);//INADDR_ANY宏代表本地任意网卡的ip，ip是4个字节32位，点分十进制
    address.sin_port = htons(m_port);//端口号是2个字节，故为s

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));//端口复用
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret == -1)
    {//报错：Address already in use服务器当前处于TIME_WAIT状态，-p换端口
        printf("binderror\n");
        perror("bindError");
        //system("pause");  Linux不认识，用pause暂停
        //pause();
    }
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);//加入到监听队列，建议队列大小为5，主动套接字变为被动套接字，服务器被动接收连接，成功返回0
    assert(ret >= 0);

    utils.init(-1);//使用工具类对象初始化对应的超时检测时间，设置为-1代表还没有客户端连接请求产生

    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];//内核时间表用于接收epoll上触发的监听时间
    m_epollfd = epoll_create(5);//创建epoll
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);//工具类对加入epoll想去列表进行了浅封装：ET模式、设置nonblock等
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);//初始化管道,管道用来通知触发信号
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    //当往一个写端关闭的管道或socket连接中连续写入数据时会引发SIGPIPE信号
    utils.addsig(SIGPIPE, SIG_IGN);//SIG_IGN表示忽略注册函数执行默认动作
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    //alarm(TIMESLOT);//闹钟开始倒计时

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    //http对象初始化信息，并加入epoll监听
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    heap_timer *timer = new heap_timer;//创建定时器
    timer->user_data = &users_timer[connfd];//赋给timer客户端信息
    timer->user_data = &users_timer[connfd];//赋给timer客户端信息
    timer->cb_func = cb_func;//函数指针赋值
    time_t cur = time(NULL);//获取当前时间（1970 1 1 0 0 0）

    timer->expire = cur + 3 * TIMESLOT;//设置超时时间
    if (utils.m_TIMESLOT == -1) {//代表服务器空闲，关闭闹钟
        utils.m_TIMESLOT = 3 * TIMESLOT;
        alarm(utils.m_TIMESLOT);
        printf("闹钟开启了\n");
    }

    users_timer[connfd].timer = timer;//保存对应定时器
    utils.m_time_heap.add_timer(timer);//util将对应的函数进行了一个浅封装
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(heap_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_time_heap.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

//摘除定时器节点
void WebServer::deal_timer(heap_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);//调用函数指针取消epoll监听并关闭文件描述符
    if (timer)
    {
        utils.m_time_heap.del_timer(timer);//删除定时器节点
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address;//客户端地址结构信息
    socklen_t client_addrlength = sizeof(client_address);//保存地址结构信息大小，用于accept
    if (0 == m_LISTENTrigmode)//LT水平触发，阻塞
    {
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);//接收客户端连接
        printf("客户端cfd=%d\n", connfd);
        if (connfd < 0)//错误
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)//连接数过载
        {
            utils.show_error(connfd, "Internal server busy");//给connfd对应的客户端发送信息
            LOG_ERROR("%s", "Internal server busy");//将错误写入日志
            return false;
        }
        timer(connfd, client_address);//加入到定时器容器上
    }

    else//ET边沿触发，非阻塞
    {
        while (1)
        {
            //将lfd设置为非阻塞即accept非阻塞监听，当多个客户端请求，顺序接收
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            printf("客户端cfd=%d\n", connfd);
            if (connfd < 0)//accept错误
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;//没了就退出
            }
            if (http_conn::m_user_count >= MAX_FD)//服务器超载
            {
                utils.show_error(connfd, "Internal server busy");//给客户端发信息
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            /*
            函数功能：
            1. 初始化对应客户端连接数组对应cfd的信息
            2. 将cfd加入epoll监听
            3. 将对应的回调函数（函数指针）赋值
            4. clie_data保存timer
            5. timer保存clie_data
            6. 更新timer时间为cur + 3*超时时间
            7. timer加入定时器容器
            */
            timer(connfd, client_address);//加入定时器
        }
        return false;
    }
    return true;
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    //读管道，设置对应标志
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::dealwithread(int sockfd)
{
    printf("客户端cfd=%d\n", sockfd);
    heap_timer *timer = users_timer[sockfd].timer;

    //reactor
    if (1 == m_actormodel)
    {
        if (timer)//先更新定时器时间，并调整到合适位置
        {
            adjust_timer(timer);
        }

        //若监测到读事件，将该事件放入请求队列，区分读事件和写事件
        m_pool->append(users + sockfd, 0);

        while (true)//主线程会一直循环等待直到该任务被处理
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor
        if (users[sockfd].read_once())//是否成功完成处理读事件
        {
            //inet_ntoa只适用于ipv4地址，而inet_ntop适用ipv4和ipv6地址，参数不同使用方法不同，
            //将网络IP地址转换成“.”点隔的字符串格式
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            //将该事件放入请求队列
            m_pool->append_p(users + sockfd);//区别就是没有实现工作线程处理写事件

            if (timer)//检查timer是否存在
            {
                adjust_timer(timer);//发生信息传输，更新定时器链表
            }
        }
        else
        {
            deal_timer(timer, sockfd);//摘除timer定时器节点
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    printf("客户端cfd=%d\n", sockfd);
    //获取定时器
    heap_timer *timer = users_timer[sockfd].timer;
    //reactor：主线程监听，工作线程处理事件
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);//更新定时器
        }

        m_pool->append(users + sockfd, 1);//加入到任务队列

        while (true)//期间主线程一直循环等待
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        //proactor：主线程工作，工作线程辅助处理逻辑
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool timeout = false;       //初始化超时检测标志
    bool stop_server = false;   //初始化服务器停止标志
    while (!stop_server)//判断当前服务器是否需要关闭
    {
        
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);//阻塞在epoll上进行监听
        if (number < 0 && errno != EINTR)//errno=4为EINTR代表程序收到信号，epoll被迫中断
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)//处理epoll事件表events
        {
            
            int sockfd = events[i].data.fd;//获取到对应触发事件的文件描述符

            //处理新到的客户连接
            if (sockfd == m_listenfd)//触发的文件描述符为服务器套接字
            {
                printf("这是第%d次循环\n", n++);
                printf("主线程%lu与新客户端建立连接！\n", pthread_self());
                bool flag = dealclinetdata();//进行客户端处理
                //sleep(3);
                if (false == flag)//处理客户端连接失败,处理下一个触发事件
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))//错误事件触发：发生挂断/对端关闭连接/发生错误
            {
                //服务器端关闭连接，移除对应的定时器
                heap_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            /*两个信号：SIGALARM和SIGTERM，分别为14和15号信号，分别代表闹钟触发信号和kill命令杀手信号
            当进程接收到着两个信号，触发对应的信号捕捉函数，线程会去执行对应函数：将触发的信号写入到管道写端
            */
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))//管道读端有数据，且为读事件
            {
                bool flag = dealwithsignal(timeout, stop_server);//执行信号处理
                if (false == flag)//信号处理失败
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)//客户端发来数据
            {
                printf("这是第%d次循环\n", n++);
                printf("主线程%lu正在处理读事件！\n", pthread_self());
                
                dealwithread(sockfd);//处理读事件

                printf("主线程%lu处理读事件完成！\n", pthread_self());
                //sleep(3);
            }
            else if (events[i].events & EPOLLOUT)//客户端要发送数据
            {
                printf("这是第%d次循环\n", n++);
                /*重新注册fd，会发生EPOLLOUT事件
                EPOLLOUT事件：send或write发包函数会涉及系统调用，存在一定开销，
                如果能将数据包聚合起来，然后调用writev将多个数据包一并发送，则可以减少系统调用次数，提高效率。
                这时EPOLLOUT事件就派上用场了：当发包时，可以将先数据包发到数据buffer(用户缓存区)中存放，
                然后通过重新注册EPOLLOUT事件，从而触发EPOLLOUT事件时，再将数据包一起通过writev发送出去。
                */
                printf("主线程%lu正在处理写事件！\n", pthread_self());

                dealwithwrite(sockfd);
                
                printf("主线程%lu处理写事件完成！\n", pthread_self());
                //sleep(3);
            }
        }
        if (timeout)
        {
            utils.timer_handler();//执行工具类中封装的超时检测函数

            LOG_INFO("%s", "timer tick");//写入到日志，代表进行了一次超时检测

            timeout = false;//重置超时标志
        }
        
    }
}