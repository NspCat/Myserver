#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <unordered_map>
#include <string>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/heaptimer.h"
#include "../log/log.h"
//载入锁模块、数据库连接池模块、定时器模块、日志模块
    //保存一个哈希表用来看报错
static const std::unordered_map<int, std::string> Check_RetCode{ //
    {0,"请求尚不完整需要继续读取客户端信息"},
    {1,"获得一个完整的客户端请求"},
    {2,"客户端请求有语法错误"},
    {3,"没有客户端访问资源"},
    {4,"客户端对资源没有足够的访问权限"},
    {5,"客户端访问资源"},
    {6,"服务器内部错误"},
    {7,"客户端已经关闭连接了"}
};
class http_conn
{
public:
    static const int FILENAME_LEN = 200;    //文件长度
    static const int READ_BUFFER_SIZE = 2048;//读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;//写缓冲区大小
    enum METHOD//http请求方法
    {
        GET = 0,    //请求指定的页面信息，并返回实体主体
        POST,       //请求服务器接受所指定的文档作为对所标识的URI的新的从属实体
        HEAD,       //只请求页面的首部
        PUT,        //从客户端向服务器传送的数据取代指定的文档的内容
        DELETE,     //请求服务器删除指定的页面
        TRACE,      // 请求服务器在响应中的实体主体部分返回所得到的内容
        OPTIONS,    //允许客户端查看服务器的性能
        CONNECT,    //要求用隧道协议连接代理
        PATCH        //实体中包含一个表，表中说明与该URI所表示的原内容的区别
    };
    enum CHECK_STATE//主状态机状态
    {
        CHECK_STATE_REQUESTLINE = 0,//正在分析请求行
        CHECK_STATE_HEADER,//正在分析请求头
        CHECK_STATE_CONTENT//正在分析请求正文
    };
    enum HTTP_CODE//服务器处理结果
    {
        NO_REQUEST ,//请求尚不完整需要继续读取客户端信息
        GET_REQUEST,//获得一个完整的客户端请求
        BAD_REQUEST,//客户端请求有语法错误
        NO_RESOURCE,//没有客户端访问资源
        FORBIDDEN_REQUEST,//客户端对资源没有足够的访问权限
        FILE_REQUEST,
        INTERNAL_ERROR,//服务器内部错误
        CLOSED_CONNECTION//客户端已经关闭连接了
    };

    enum LINE_STATUS//从状态机状态
    {
        LINE_OK = 0, //读取到一个完整的行
        LINE_BAD,//行出错
        LINE_OPEN//行尚不完整
    };

public:
    http_conn() {}//构造函数
    ~http_conn() {}//析构函数

public:
    //初始化连接，将cfd添加到epoll兴趣列表
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭当前连接，客户端总量-1
    void close_conn(bool real_close = true);
    //http解析入口，调用process_read私有函数
    void process();
    //读取处理客户端信息
    bool read_once();

    bool write();
    //返回用户地址结构信息的地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //初始化users用户表
    void initmysql_result(connection_pool *connPool);
    int timer_flag;//超时标志，==1代表当前节点超时
    int improv;//标识当前连接是否被线程处理过读/写事件


private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;   //代表epoll兴趣列表的文件描述符
    static int m_user_count;//用户连接总数，类唯一
    MYSQL *mysql;           //一个MYSQL连接
    int m_state;  //当前客户连接的状态，读为0, 写为1

private:
    int m_sockfd;//当前客户端的cfd
    sockaddr_in m_address;//当前客户端连接的地质结构信息
    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx;//指向buffer中客户数据的尾部的下一字节。
    int m_checked_idx;//指向buffer中当前正在分析的字节
    int m_start_line;//指向行在buffer中的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;//主状态机状态
    METHOD m_method;//保存请求方法
    char m_real_file[FILENAME_LEN];//存储客户端请求网页路径
    char *m_url;//请求url
    char *m_version;//请求版本号
    char *m_host;//请求主机
    int m_content_length;
    bool m_linger;//是否使用http长连接
    char *m_file_address;//指向网页文件的共享内存
    struct stat m_file_stat;//保存网页文件的stat状态信息
    struct iovec m_iv[2];//其内部成员指向一块缓冲区，分别用于分散读与集中写
    int m_iv_count;      //iovec结构体数量
    int cgi;        //是否启用的POST
    char *m_string; //存储消息正文数据，包含用户名和密码
    int bytes_to_send;//未发送数据
    int bytes_have_send;//已发送数据
    char *doc_root;//网页存储目录

    map<string, string> m_users;//保存用户表
    int m_TRIGMode;//混合触发模式
    int m_close_log;//日志关闭/开启标志

    char sql_user[100];//数据库用户名
    char sql_passwd[100];//数据库密码
    char sql_name[100];//数据库名
};

#endif
