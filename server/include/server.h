#pragma once

#include "mysql_pool.h"
#include <memory>

class EpollServer;

class Server{
public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool init(const MySqlConfig& databaseConfig,unsigned short port);

    void run();

private:
    std::unique_ptr<MySqlPool> m_mysqlPool;
    std::unique_ptr<EpollServer> m_network;
};
