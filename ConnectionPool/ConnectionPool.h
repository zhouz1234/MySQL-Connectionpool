#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include "MysqlConn.h"
using namespace std;
class ConnectionPool
{
public:
    static ConnectionPool* getConnectPool();  //����ģʽ
    ConnectionPool(const ConnectionPool& obj) = delete;
    ConnectionPool& operator=(const ConnectionPool& obj) = delete;
    shared_ptr<MysqlConn> getConnection(); // �����ӳ���ȡ��һ������
    ~ConnectionPool();
private:
    ConnectionPool();
    bool parseJsonFile();
    void produceConnection();
    void recycleConnection();
    void addConnection();

    string m_ip;                     // ���ݿ������ip��ַ              
    string m_user;                   // ���ݿ�������û���
    string m_passwd;                 // ���ݿ����������
    string m_dbName;                 // ���ݿ�����������ݿ���
    unsigned short m_port;           // ���ݿ�������󶨵Ķ˿�
    int m_minSize;                   // ���ӳ�ά������С������
    int m_maxSize;                   // ���ӳ�ά�������������
    int m_timeout;                   // ���ӳػ�ȡ���ӵĳ�ʱʱ��
    int m_maxIdleTime;               // ���ӳ������ӵ�������ʱ��

    queue<MysqlConn*> m_connectionQ;  //���ݿ��  �ö��б���  ���Ӻ�ʹ�� �� push pop��
    mutex m_mutexQ;
    condition_variable m_cond;
};

