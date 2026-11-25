#pragma once
#include <mysql/mysql.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

struct MySqlConfig{
    std::string host;
    unsigned int port = 3306;

    std::string user;
    std::string password;
    std::string database;

    unsigned int connectTimeoutSeconds = 5;
};

class MySqlPool{
public:
    //指针离开作用域时，自动归还连接
    struct ConnectionReleaser{
        MySqlPool* pool;
        bool reusable = true;

        void operator()(MYSQL* connection) const noexcept;
    };

    using Lease = std::unique_ptr<MYSQL,ConnectionReleaser>;

    MySqlPool(const MySqlConfig& config,std::size_t connectionCount);
    ~MySqlPool();

    MySqlPool(const MySqlPool&) = delete;
    MySqlPool& operator=(const MySqlPool&) = delete;

    //等待可用连接，超时或连接池停止时抛出异常
    Lease acquire(std::chrono::milliseconds timeout);

    //关闭故障连接并结束本次调用，不再放回空闲队列
    void discard(Lease& connection);

private:
    void release(MYSQL* connection,bool reusable);

private:
    std::mutex m_mutex;
    std::condition_variable m_available;

    std::queue<MYSQL*> m_idleConnections;
    
    std::size_t m_borrowedCount = 0;
    bool m_stopping = false;
};
