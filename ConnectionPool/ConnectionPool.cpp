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
    //����json��ʽ���ݣ���json�����������ݿ����ӵĻ�����Ϣ�������Ժ��޸ģ�
    ifstream ifs("dbconf.json");  //��ȡjson��ʽ�� read���У�
    Reader rd;                 
    Value root;                   
    rd.parse(ifs, root);         //����read�ļ��е�json����ȡΪvalueֵ�� �����л�������
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

void ConnectionPool::produceConnection()     //����һ�����ݿ�����
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

void ConnectionPool::recycleConnection()     //�����߳� ÿ500ms���һ�� �Ƿ� �ж�����е����ݿ����ӣ�����ʱ��Ϊ׼��������� ȥ�����У�delete��
{
    while (true)
    {
        this_thread::sleep_for(chrono::milliseconds(500));
        lock_guard<mutex> locker(m_mutexQ);
        while (m_connectionQ.size() > m_minSize)   // ���ж��Ƿ��ж����߳�
        {
            MysqlConn* conn = m_connectionQ.front();
            if (conn->getAliveTime() >= m_maxIdleTime)   //����ʱ�䳬������ʱ�� �����;
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

void ConnectionPool::addConnection()      //�½�һ�����ݿ����� 
{
    MysqlConn* conn = new MysqlConn;
    conn->connect(m_user, m_passwd, m_dbName, m_ip, m_port);  
    conn->refreshAliveTime();
    m_connectionQ.push(conn);       //�������
}

shared_ptr<MysqlConn> ConnectionPool::getConnection()   // ȡ��һ�����ݿ����� ����ҵ��, ʹ�ù���ָ��
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
    //ʹ������ָ��ʵ����������֮���Զ����գ� connptr��str1,str2��,str1��ʼ��connptr ��str2�Զ��������������û��ո�conn���ӵ�m_connectionQ
    //lamba���ʽָ�������� this��׽�����е����ж���  ����ʹ��m_connectionQ ���У�
    shared_ptr<MysqlConn> connptr(m_connectionQ.front(), [this](MysqlConn* conn) {
        lock_guard<mutex> locker(m_mutexQ);
        conn->refreshAliveTime();     // ���¸����ӿ���ʱ�䣻
        m_connectionQ.push(conn);    //��������
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
    // ���������ļ�
    if (!parseJsonFile())
    {
        return;
    }
    //������ʼ�����ݿ����������   m_minSize;
    for (int i = 0; i < m_minSize; ++i)
    {
        addConnection();
    }
    //������̵߳���ں���ʱ��ķǾ�̬��Ա��������Ҫָ�������ĵ�ַ��������
    //���ڸ����ǵ���ģʽ��ֱ����thisָ��ö���
    thread producer(&ConnectionPool::produceConnection, this);
    thread recycler(&ConnectionPool::recycleConnection, this);
    producer.detach();
    recycler.detach();
}
