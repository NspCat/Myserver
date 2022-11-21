
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"
#include <mysql/mysql.h>

using namespace std;

connection_pool::connection_pool()
{
	//初始化连接数
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()//获取数据库连接池
{
	static connection_pool connPool;//创建唯一的对象
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;

	for (int i = 0; i < MaxConn; i++)
	{
		//创建并初始化一个mysql连接
		MYSQL *con = NULL;
		/*
		分配或初始化与mysql_real_connect()相适应的MYSQL对象。
		如果mysql是NULL指针，该函数将分配、初始化、并返回新对象。
		否则，将初始化对象，并返回对象的地址。如果mysql_init()分配了新的对象，
		当调用mysql_close()来关闭连接时。将释放该对象。
		返回值：初始化的MYSQL*句柄。如果无足够内存以分配新的对象，返回NULL。
		错误：在内存不足的情况下，返回NULL。
		*/
		con = mysql_init(con);

		if (con == NULL)//初始化出错
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//conn为分配到的mysql对象，与数据库建立连接
		/*函数原型描述:
  	  MYSQL *mysql_real_connect (MYSQL *mysql, const char *host, const char *user, const char *passwd,const char *db,unsigned int port, const char *unix_socket, unsigned long client_flag)；
		  参数：
  		  mysql: mysql_init函数返回的指针
			  host：null 或 localhost时链接的是本地的计算机，
  		  当mysql默认安装在unix（或类unix）系统中，root账户是没有密码的，因此用户名使用root，密码为null，
  		  当db为空的时候，函数链接到默认数据库，在进行 mysql安装时会存在默认的test数据库，因此此处可以使用test数据库名称，
			  port：使用3306为默认mysql数据库端口号
  		  使用 unix连接方式，unix_socket为null时，表明不使用socket或管道机制，最后一个参数经常设置为0
			  mysql_real_connect()尝试与运行在主机上的MySQL数据库引擎建立连接。
			  在你能够执行需要有效MySQL连接句柄结构的任何其他API函数之前，mysql_real_connect()必须成功完成。

		返回值：如果连接成功，返回MYSQL*连接句柄。如果连接失败，返回NULL。对于成功的连接，返回值与第1个参数的值相同。*/
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		connList.push_back(con);//放入建立连接的数据库连接到链表
		++m_FreeConn;//可用连接+1
	}

	reserve = sem(m_FreeConn);//初始化信号量

	m_MaxConn = m_FreeConn;//初始化最大连接数
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()//只能被connectionRAII构造函数调用
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();//信号P--操作
	
	lock.lock();//加锁

	con = connList.front();//取连接
	connList.pop_front();  //弹出

	--m_FreeConn;//可用连接-1
	++m_CurConn;//当前使用连接数+1

	lock.unlock();//解锁
	return con;//返回取出的连接
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)//只能被connectionRAII析构函数调用
{
	if (NULL == con)
		return false;

	lock.lock();//加锁

	connList.push_back(con);//放回链表
	++m_FreeConn;//当前可用连接+1
	--m_CurConn;//当前使用连接数-1

	lock.unlock();//解锁

	reserve.post();//信号量V++操作
	return true;//返回调用是否成功
}

//销毁数据库连接池
void connection_pool::DestroyPool()//只能被当前类的析构函数调用
{

	lock.lock();//加锁
	if (connList.size() > 0)//判断当前数据库连接池是否存在
	{
		list<MYSQL *>::iterator it;//迭代器遍历容器元素关闭连接
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		//清空连接数
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();//释放空间
	}

	lock.unlock();//解锁
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;//返回当前空闲连接数
}

connection_pool::~connection_pool()
{
	DestroyPool();//单例模式析构自动销毁对象
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){//构造获取连接
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){//析构自动释放连接当前连接
	poolRAII->ReleaseConnection(conRAII);
}