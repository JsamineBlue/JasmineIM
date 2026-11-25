#include "server.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

int main(){
    const char* password = std::getenv("IM_DB_PASSWORD");

    if(password == nullptr || password[0] == '\0'){
        fprintf(stderr,"Environment variable IM_DB_PASSWORD is missing\n");

        return 1;
    }

    MySqlConfig config;
    config.host = "192.168.219.1";
    config.port = 3306;
    config.user = "im_app";
    config.password = password;
    config.database = "im";

    try{
        Server server;

        if(!server.init(config,14790)){
            return 1;
        }

        server.run();
    }
    catch(const std::exception& error){
        fprintf(stderr,"Server failed: %s\n",error.what());

        return 1;
    }
    return 0;
}

