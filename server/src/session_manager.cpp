#include "session_manager.h"

#include <optional>
#include <stdexcept>

SessionManager::BindResult SessionManager::bind(std::int32_t userId,std::uint64_t connectionId){
    if(userId <= 0 || connectionId == 0){
        throw std::invalid_argument("Invalid session identity");
    }

    if(m_connectionToUser.count(connectionId) != 0){
        return BindResult::ConnectionAlreadyLoggedIn;
    }

    if(m_userToConnection.count(userId) != 0){
        return BindResult::userAlreadyOnline;
    }

    auto inserted = m_userToConnection.emplace(userId,connectionId);

    try{
        m_connectionToUser.emplace(connectionId,userId);
    }
    catch(...){
        //第二张映射表写入失败时，撤销第一张映射
        m_userToConnection.erase(inserted.first);
        throw;
    }

    return BindResult::Success;
}

std::optional<std::int32_t> SessionManager::userForConnection(std::uint64_t connectionId) const{
    auto it = m_connectionToUser.find(connectionId);

    if(it == m_connectionToUser.end()){
        return std::nullopt;
    }
    
    return it->second;
}

std::optional<std::uint64_t> SessionManager::connectionForUser(std::int32_t userId) const{
    auto it = m_userToConnection.find(userId);

    if(it == m_userToConnection.end()){
        return std::nullopt;
    }
    
    return it->second;
}

//实现解绑
std::optional<std::int32_t> SessionManager::unbind(std::uint64_t connectionId){
    auto it = m_connectionToUser.find(connectionId);

    if(it == m_connectionToUser.end()){
        return std::nullopt;
    }

    std::int32_t userId = it->second;

    m_userToConnection.erase(userId);
    m_connectionToUser.erase(it);

    return userId;
}
