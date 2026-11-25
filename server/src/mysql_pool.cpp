#include "mysql_pool.h"

#include <chrono>
#include <cstddef>
#include <mysql/mysql.h>
#include <stdexcept>
#include <system_error>
#include <utility>

MySqlPool::MySqlPool(const MySqlConfig& config,std::size_t connectionCount){
    if(connectionCount == 0){
        throw std::invalid_argument("MySQL connection count must be positive");
    }

    try{
        for(std::size_t i=0;i<connectionCount;i++){
            //暂时由局部智能指针管理新链接
            std::unique_ptr<MYSQL,decltype(&mysql_close)> connection(mysql_init(nullptr),&mysql_close);

            if(!connection){
                throw std::runtime_error("mysql_init failed");
            }

            unsigned int timeout = config.connectTimeoutSeconds;

            if(mysql_options(connection.get(),MYSQL_OPT_CONNECT_TIMEOUT,&timeout)!=0){
                throw std::runtime_error(std::string("Set MySQL timeout failed: ")+mysql_error(connection.get()));
            }

            if(mysql_options(connection.get(),MYSQL_SET_CHARSET_NAME,"UTF8MB4")!=0){
                throw std::runtime_error(std::string("Set MySQL charset failed: ")+mysql_error(connection.get()));
            }

            if(!mysql_real_connect(
                        connection.get(),
                        config.host.c_str(),
                        config.user.c_str(),
                        config.password.c_str(),
                        config.database.c_str(),
                        config.port,
                        nullptr,
                        0)){
                throw std::runtime_error(std::string("Connect MySQL failed: ")+mysql_error(connection.get()));
            }

            //连接池使用自动提交
            if(mysql_autocommit(connection.get(),true) != 0){
                throw std::runtime_error(std::string("Enable MySQL autocommit failed: ") + mysql_error(connection.get()));
            }

            //成功加入队列后，再移交连接所有权
            m_idleConnections.push(connection.get());
            connection.release();
        }
    }
    catch(...){
        //构造失败时，析构函数不会执行
        //这里主动关闭已经放进队列的连接
        while(!m_idleConnections.empty()){
            mysql_close(m_idleConnections.front());
            m_idleConnections.pop();
        }
        throw;
    }
}

MySqlPool::Lease MySqlPool::acquire(std::chrono::milliseconds timeout){

    std::unique_lock<std::mutex> lock(m_mutex);

    bool ready = m_available.wait_for(lock,timeout,[this]{return m_stopping||!m_idleConnections.empty();});

    if(!ready){
        throw std::runtime_error("Timed out waiting for a MySQL connection");
    }

    if(m_stopping){
        throw std::runtime_error("MySQL pool is stopping");
    }

    MYSQL* connection = m_idleConnections.front();

    Lease lease(connection,ConnectionReleaser{this,true});

    m_idleConnections.pop();
    ++m_borrowedCount;

    return lease;
}

//归还连接
void MySqlPool::release(MYSQL* connection,bool reusable){
    if(connection == nullptr){
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if(m_stopping || !reusable){
            mysql_close(connection);
        }
        else{
            try{
                m_idleConnections.push(connection);
            }
            catch(...){
                //归还队列可能因为内存不足而入队失败
                //不能让异常掏出智能指针的释放函数
                mysql_close(connection);
            }
        }

        --m_borrowedCount;
    }

    m_available.notify_all();
}

MySqlPool::~MySqlPool(){
    std::unique_lock<std::mutex> lock(m_mutex);

    m_stopping = true;
    m_available.notify_all();

    //等待已经借出的连接全部归还
    m_available.wait(lock,[this]{
            return m_borrowedCount == 0;
            });

    while(!m_idleConnections.empty()){
        mysql_close(m_idleConnections.front());
        m_idleConnections.pop();
    }
}

void MySqlPool::ConnectionReleaser::operator()(MYSQL* connection) const noexcept{
    if (connection != nullptr){
        pool->release(connection, reusable);
    }
}

void MySqlPool::discard(Lease& connection){
    if(!connection){
        return;
    }

    if(connection.get_deleter().pool != this) {
        throw std::invalid_argument("Connection belongs to another MySQL pool");
    }

    connection.get_deleter().reusable = false;
    connection.reset();
}
