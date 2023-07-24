#include "ConnectionPool.h"
#include <json/json.h>
#include <fstream>
#include <thread>
using namespace Json;
ConnectionPool* ConnectionPool::getConnectPool()
{
    static ConnectionPool pool;
    return &pool;
}

bool ConnectionPool::parseJsonFile()
{
    //解析json格式数据，用json数据设置数据库链接的基本信息，便于以后修改；
    ifstream ifs("dbconf.json");  //读取json格式到 read流中；
    Reader rd;                 
    Value root;                   
    rd.parse(ifs, root);         //解析read文件中的json，读取为value值； 反序列化操作；
    if (root.isObject())
    {
        m_ip = root["ip"].asString();
        m_port = root["port"].asInt();
        m_user = root["userName"].asString();
        m_passwd = root["password"].asString();
        m_dbName = root["dbName"].asString();
        m_minSize = root["minSize"].asInt();
        m_maxSize = root["maxSize"].asInt();
        m_maxIdleTime = root["maxIdleTime"].asInt();
        m_timeout = root["timeout"].asInt();
        return true;
    }
    return false;
}

void ConnectionPool::produceConnection()     //生产一个数据库连接
{
    while (true)
    {
        unique_lock<mutex> locker(m_mutexQ);
        while (m_connectionQ.size() >= m_minSize)
        {
            m_cond.wait(locker);
        }
        addConnection();
        m_cond.notify_all();
    }
}

void ConnectionPool::recycleConnection()     //回收线程 每500ms检查一次 是否 有多余空闲的数据库连接，空闲时间为准，有则回收 去除队列，delete；
{
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(500));
        lock_guard<mutex> locker(m_mutexQ);
        while (m_connectionQ.size() > m_minSize)   // 先判断是否有多余线程
        {
            MysqlConn* conn = m_connectionQ.front();
            if (conn->getAliveTime() >= m_maxIdleTime)   //空闲时间超过设置时间 则回收;
            {
                m_connectionQ.pop();
                delete conn;
            }
            else
            {
                break;
            }
        }
    }
}

void ConnectionPool::addConnection()      //新建一个数据库连接 
{
    MysqlConn* conn = new MysqlConn;
    conn->connect(m_user, m_passwd, m_dbName, m_ip, m_port);  
    conn->refreshAliveTime();
    m_connectionQ.push(conn);       //加入队列
}

shared_ptr<MysqlConn> ConnectionPool::getConnection()   // 取出一个数据库连接 用于业务, 使用共享指针
{
    unique_lock<mutex> locker(m_mutexQ);
    while (m_connectionQ.empty())
    {
        if (cv_status::timeout == m_cond.wait_for(locker, chrono::milliseconds(m_timeout)))
        {
            if (m_connectionQ.empty())
            {
                //return nullptr;
                continue;
            }
        }
    }
    //使用智能指针实现连接用完之后自动回收， connptr（str1,str2）,str1初始化connptr ，str2自定义析构器，设置回收改conn连接到m_connectionQ
    //lamba表达式指定析构器 this捕捉该类中的所有对象  才能使用m_connectionQ 队列；
    shared_ptr<MysqlConn> connptr(m_connectionQ.front(), [this](MysqlConn* conn) {
        lock_guard<mutex> locker(m_mutexQ);
        conn->refreshAliveTime();     // 更新改连接空闲时间；
        m_connectionQ.push(conn);    //回收连接
        });
    m_connectionQ.pop();
    m_cond.notify_all();
    return connptr;
}

ConnectionPool::~ConnectionPool()
{
    while (!m_connectionQ.empty())
    {
        MysqlConn* conn = m_connectionQ.front();
        m_connectionQ.pop();
        delete conn;
    }
}

ConnectionPool::ConnectionPool()
{
    // 加载配置文件
    if (!parseJsonFile())
    {
        return;
    }
    //创建初始与数据库的链接数量   m_minSize;
    for (int i = 0; i < m_minSize; ++i)
    {
        addConnection();
    }
    //如果子线程的入口函数时类的非静态成员函数，需要指定函数的地址和所有者
    //由于该类是单例模式，直接用this指向该对象；
    thread producer(&ConnectionPool::produceConnection, this);
    thread recycler(&ConnectionPool::recycleConnection, this);
    producer.detach();
    recycler.detach();
}
