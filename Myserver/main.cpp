#include "config.h"
#define PROT 8000

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "123456";
    string databasename = "yourdb";

    //命令行解析
    Config config;                  //创建配置对象
    config.parse_arg(argc, argv);   //解析主函数传参

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //初始化日志
    /*
    执行结果：得到一个打开的文件m_fp，异步：一个阻塞队列和一个工作线程
    */
    server.log_write();

    //初始化数据库链接池
    /*
    执行结果：获得一个8个数据库链接大小的数据库连接池，将表内信息存放到了哈希表里
    */
    server.sql_pool();

    //线程池
    /*
    执行结果：获得一个8个工作线程的线程池
    */
    server.thread_pool();

    //触发模式，为lfd和cfd设置触发模式，分别设置对应的成员变量 m_LISTENTrigmode 与 m_CONNTrigmode
    server.trig_mode();

    //初始化服务器，创建管道、epoll事件数组
    server.eventListen();

    //运行服务器，客户端可以请求数据
    server.eventLoop();

    return 0;
}
