#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;
//*************************************************************************************
void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;//客户端的地址结构信息

    addfd(m_epollfd, sockfd, true, m_TRIGMode);//加到兴趣队列
    m_user_count++;//连接总数+1

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();//其他成员初始化
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;//是否为http长连接
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;//默认客户端连接读状态
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);//初始化http请求的文件路径
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    /*checked_index指向buffer（应用程序的读缓冲区）中当前正在分析的字节，read_index指
    向buffer中客户数据的尾部的下一字节。buffer中第0~checked_index字节都已分析完毕，
    第checked_index~(read_index-1)字节由下面的循环挨个分析*/
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        //获取当前要分析的字节
        temp = m_read_buf[m_checked_idx];
        //如果当前的字节是"\r"，即回车符，则说明可能读取到一个完整的行
        if (temp == '\r')
        {
            /*如果“\r”字符碰巧是目前buffer中的最后一个已经被读入的客户数据，那么这次
            分析没有读取到一个完整的行，返回LINE_OPEN以表示还需要继续读取客户数据才能进一步分析*/
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                /*如果下一个字符是“\n“，则说明我们成功读取到一个完整的行*/
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            /*否则的话。说明客户发送的HTTP请求存在语法问题*/
            return LINE_BAD;
        }
        /*如果当前的字节是“\n”，即换行符，则也说明可能读取到一个完整的行*/
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            /*如果所有内容都分析完毕也没遇到“\r”字符，则返回LINE_OPEN，表示还需要继续读取客户数
据才能进一步分析*/
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;//更新当前读缓冲区结尾

        if (bytes_read <= 0)//接收字节数
        {
            return false;
        }

        return true;
    }
    //ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号  组成：method| |url| |http\r\n
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    /*strpbrk(s1,s2)从s1的第一个字符向后检索，直到'\0'，如果当前字符存在于s2中，那么返回当前字符的地址，并停止检索。
    【返回值】如果s1、s2含有相同的字符，那么返回指向s1中第一个相同字符的指针，否则返回NULL。*/
    m_url = strpbrk(text, " \t");
    /*如果请求行中没有空白字符或“\t”字符，则HTTP请求必有问题*/
    if (!m_url)
    {
        return BAD_REQUEST;
    }

    *m_url++ = '\0';//将method后的空格置为\0，此时text即为method字段
    char *method = text;
    /*strcasecmp(s1,s2):若参数s1 和s2 字符串相同则返回0。s1 长度大于s2 长度则返回大于0 的值，s1 长度若小于s2 长度则返回小于0 的值*/
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;//代表使用的是post方法
    }
    else
        return BAD_REQUEST;

    /*跳过URL里的空格或“\t”*/
    //库函数 size_t strspn(const char *str1, const char *str2) 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    m_url += strspn(m_url, " \t");//跳过可能的空格或\t，指向url第一个字符
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';//将m_url变为目标url字段

    m_version += strspn(m_version, " \t");//跳过可能的空格或\t，指向version第一个字符
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    /*检查URL是否合法*//*||*/
    /*函数说明：strncasecmp()用来比较参数s1 和s2 字符串前n个字符，比较时会自动忽略大小写的差异。
    返回值：若参数s1 和s2 字符串相同则返回0。s1 若大于s2 则返回大于0 的值，s1 若小于s2 则返回小于0 的值。*/
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;//跳过http://
        /*strchr函数功能为在一个串中查找给定字符的第一个匹配位置，即返回剩余部分*/
        m_url = strchr(m_url, '/');//跳过域名
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;//跳过https://
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");//作为初始界面
    //else if (*(m_url + 1) == 'p')//压力测试
    //{
    //    bzero(m_url, sizeof(m_url));
    //    strcat(m_url, "/phpinfo.php");
    //}
        
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;//请求尚不完整需要继续读取客户端信息
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    /*遇到一个空行，说明我们得到了一个正确的HTTP请求*/
    if (text[0] == '\0')//代表遇到的空行，下面是请求消息正文
    {
        if (m_content_length != 0)//判断是否需要转移到解析请求正文
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;//请求尚不完整需要继续读取客户端信息
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)//为长连接：在一个连接上可以连续发送多个数据包，在连接保持期间，如果没有数据包发送，需要双方发链路检测包
        {
            m_linger = true;//设置为长连接
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)//域名或IP地址
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;/*记录当前行的读取状态*/
    HTTP_CODE ret = NO_REQUEST;/*记录HTTP请求的处理结果*/
    char *text = 0;/*保存http解析信息*/

    /*主状态机，用于从buffer中取出所有完整的行，主状态机器m_check_state用于状态转换*/
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();//获取当前待解析数据
        m_start_line = m_checked_idx;//更新为下一行
        LOG_INFO("%s", text);//输出当前获取的http信息

        switch (m_check_state)//初始化为CHECK_STATE_REQUESTLINE
        {
        case CHECK_STATE_REQUESTLINE:/*第一个状态，分析请求行*/
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER:/*第二个状态，分析头部字段*/
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:/*分析请求正文*/
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    printf("====正在处理返回文件==========当前初始m_url为%s============\n", m_url);
    strcpy(m_real_file, doc_root);//先放入网页目录
    int len = strlen(doc_root);    //保存长度，不包含\0的字符串大小
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');//获取到最后一个/的地址，其后是action信息用来判断回传网页

    /*处理cgi：是否启用Post，cgi = 1为客户端http请求使用post方法
    对html中action行为设置标志位，将method设置为POST
    > * 注册:3
    > * 登录:2
    > * 
    > * 
    > * 
    > * 
    > * 
    */
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))//cgi判断，确保是post请求，即传输了实体
    {
        printf("=====使用post请求：%s======\n", p);

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];//即2或3

        /*用于获取其他资源*/
        
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/");
            strcat(m_url_real, m_url + 2);//m_url_real = /文件名
            printf("m_url_real=%s\n", m_url_real);
            strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
            free(m_url_real);
        
        

        //表单提交设置了method方法为post，iput输入的用户名和密码会以字符串的形式通过http请求正文传输
        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for (i = 5; m_string[i] != '&'; ++i)//提取用户名
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)//提取密码
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            printf("--------用户注册中--------\n");
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)//为真，注册成功
                    strcpy(m_url, "/Register_Success.html");
                else
                    strcpy(m_url, "/error.html");
            }
            else
                strcpy(m_url, "/Register_Error.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            printf("--------用户登录中--------\n");
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");//登录后界面
            else
                strcpy(m_url, "/Log_Error.html");//错误界面，找不到用户或密码错误
        }
    }
  
        printf("--------用户其他请求--------\n");
        //if (*(p + 1) == 'p')//进行压测
        //{
        //    char* m_url_real = (char*)malloc(sizeof(char) * 200);
        //    strcpy(m_url_real, "/phpinfo.php");
        //    strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        //    free(m_url_real);
        //}
        
        if (*(p + 1) == '0')
        {
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/index.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == '1')
        {
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/index.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == 'p')
        {
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/picture.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == 'v')
        {
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/video.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else if (*(p + 1) == 'A')
        {
            char* m_url_real = (char*)malloc(sizeof(char) * 200);
            strcpy(m_url_real, "/About.html");
            strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

            free(m_url_real);
        }
        else
        {
            printf("m_real_file=%s   ,   m_url=%s\n", m_real_file, m_url);
            strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
        }
    


        

    if (stat(m_real_file, &m_file_stat) < 0)//获取文件属性失败
    {
        printf("m_realfile：%s\n", m_real_file);
        printf("_________获取文件属性失败_________\n");
        return NO_RESOURCE;
    }
        

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    int fd = open(m_real_file, O_RDONLY);
    printf("工作线程%lu打开文件%s\n", pthread_self(), m_real_file);

    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)//没有数据发送
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();//初始化http_conn当前类对象
        return true;
    }

    while (1)
    {
        printf("线程%lu正在进行集中写\n", pthread_self());
        temp = writev(m_sockfd, m_iv, m_iv_count);//进行集中写，将m_iv[0]的数据或m_iv[0]和m_iv[1]中数据写到客户端
         
        /* 如果 TCP 写缓冲没有空间。则等待下一轮 EPOLLOUT 事件。虽然在此期间，服务器无法立即接收到同一
        客户的下一个请求，但这可以保证连接的完整性 */
        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();//销毁共享内存
            return false;
        }

        bytes_have_send += temp;//发送数据
        bytes_to_send -= temp;  //未发送数据
        printf("待发送数据量为%d\n", bytes_to_send);
        printf("已发送数据量为%d\n", bytes_have_send);
        if (bytes_have_send >= m_iv[0].iov_len)//代表当前响应报文发送完成
        {
            m_iv[0].iov_len = 0;//将对应表示缓冲区长度的结构体成员置0
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);//更新内存映射位置
            m_iv[1].iov_len = bytes_to_send;
        }
        else//更新写缓冲区
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)//未发送数据
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)//保持长连接
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)//确保当前写缓冲区有空间
        return false;
    va_list arg_list;//通常可变参类型为char*
    va_start(arg_list, format);//对arg_list进行初始化：获取可变参数列表的第一个参数的地址，第二个参数为可变参左面第一个参数
    /*
    int _vsnprintf(char* str, size_t size, const char* format, va_list ap);
    函数功能：将可变参数格式化输出到一个字符数组。
    用法类似于vsprintf，不过加了size的限制，防止了内存溢出（size为str所指的存储空间的大小）。
    返回值：执行成功，返回最终生成字符串的长度，若生成字符串的长度大于size，则将字符串的前size个字符复制到str，
    同时将原串的长度返回（不包含终止符）；执行失败，返回负值，并置errno. 
    */
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))//写数据缓冲区空间不够写
    {
        va_end(arg_list);//清空va_list可变参数列表
        return false;
    }
    m_write_idx += len;//更新结束位置
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() && add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)//根据状态码进行处理
    {
    case INTERNAL_ERROR://服务器内部错误
    {
        add_status_line(500, error_500_title);//添加 HTTP/1.1 状态码 描述\r\n到写缓冲区
        add_headers(strlen(error_500_form));  //添加长度\r\n是否长连接\r\n\r\n到写缓冲区
        if (!add_content(error_500_form))     //添加对应信息到写缓冲区
            printf("add_content(error_500_form) ERROR\n");
            return false;
        break;
    }
    case BAD_REQUEST://客户端请求有语法错误
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            printf("add_content(error_404_form) ERROR\n");
            return false;
        break;
    }
    case FORBIDDEN_REQUEST://客户端对资源没有足够的访问权限
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            printf("add_content(error_403_form) ERROR\n");
            return false;
        break;
    }
    case FILE_REQUEST://请求文件
    {
      
        printf("请求文件 DOING!!!\n");
        add_status_line(200, ok_200_title);//添加 HTTP/1.1 状态码 描述\r\n到写缓冲区
        if (m_file_stat.st_size != 0)//判断是否打开了网页文件
        {
            printf("工作线程%lu文件成功打开，添加数据中!!!\n", pthread_self());
            add_headers(m_file_stat.st_size);//添加网页文件大小\r\n是否长连接\r\n\r\n到写缓冲区
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;//保存网页文件数据
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;//更新待发送数据
            printf("待发送数据量为%d\n", bytes_to_send);
            printf("已发送数据量为%d\n", bytes_have_send);
            return true;
        }
        else//给一个空白网页
        {
            printf("文件打开失败，返回一个空网页!\n");
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                printf("GIVE YOU AN EMPTY HTML ERROR!\n");
                return false;
        }
    }
    default:
        printf("OTHER ERROR!!!!\n");
        return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;//初始化待发送数据
    return true;
}
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();//执行私有函数状态机解析，返回服务器处理结果
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)//客户端得不到回应就被关闭
    {
        printf("------write_ret ERROR, close cfd!------\n");
        if (read_ret == 3) printf("资源获取失败！\n");
        printf("httpcode:%s\n", Check_RetCode.at(read_ret).c_str());
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);//注册EPOLLOUT，该事件被触发
}
