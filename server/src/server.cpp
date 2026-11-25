#include "server.h"
#include "epoll_server.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <mysql/mysql.h>
#include <stdexcept>
#include <utility>

Server::Server(){
    if(mysql_library_init(0,nullptr,nullptr)!=0){
        throw std::runtime_error("MySQL library initialization failed");
    }
}

Server::~Server(){
    //先销毁网络服务器，等待内部工作线程结束
    m_network.reset();

    //再释放所有数据库连接
    m_mysqlPool.reset();

    //最后结束 MySQL 库
    mysql_library_end();
}

bool Server::init(const MySqlConfig& databaseConfig,unsigned short port){
    if(m_mysqlPool || m_network){
        fprintf(stderr,"Server is already initialized\n");
        return false;
    }

    try{
        auto mysqlPool = std::make_unique<MySqlPool>(databaseConfig,2);

        auto network = std::make_unique<EpollServer>(port,*mysqlPool);

        if(!network->init()){
            return false;
        }

        //全部初始化成功以后，再交给 Server 持有
        m_mysqlPool = std::move(mysqlPool);
        m_network = std::move(network);

        return true;
    }
    catch(const std::exception& error){
        fprintf(stderr,"Server initialization failed: %s\n",error.what());

        return false;
    }
}

void Server::run(){
    if(!m_network){
        fprintf(stderr,"Server is not initialized\n");
        return;
    }

    m_network->run();
}
