#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

class SessionManager{
public:
    enum class BindResult{Success,userAlreadyOnline,ConnectionAlreadyLoggedIn};

    BindResult bind(std::int32_t userId,std::uint64_t connectionId);

    //断线时移除身份，返回原来的用户ID
    //未登录连接返回 std::nullopt
    std::optional<std::int32_t> unbind(std::uint64_t connectionId);
    std::optional<std::int32_t> userForConnection(std::uint64_t connectionId)const;
    std::optional<std::uint64_t> connectionForUser(std::int32_t userId) const;

private:
    std::unordered_map<std::int32_t,std::uint64_t> m_userToConnection;
    std::unordered_map<std::uint64_t,std::int32_t> m_connectionToUser;
};
