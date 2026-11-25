#pragma once

#include <vector>
#include <cstdint>
#include <optional>
#include "group_protocol.h"
#include "file_protocol.h"

class MySqlPool;

//用户上线通知
struct UserNotification{
    std::int32_t targetUserId;
    std::vector<char> body;
};

//待转发消息
struct PendingPrivateChat{
    std::int32_t targetUserId;
    std::vector<char> body;
};

struct PendingFriendRequest{
    std::int32_t applicantId;
    std::int32_t targetId;

    std::vector<char> body;
};

//关系变更结果
struct EstablishedFriendship{
    std::int32_t applicantId;
    std::int32_t responderId;
};

//邀请进群信息结构
struct PendingGroupInvitation {
    std::uint32_t requestId = 0;
    std::int32_t groupId = 0;
    std::int32_t inviterId = 0;
    std::int32_t targetUserId = 0;

    // 提前准备通知内容，邀请编号稍后由事件线程填写。
    PROT_GROUP_INVITE_NOTIFY notification{};
};

// 服务端内部生成的入群任务，不是客户端发送的协议结构。
struct GroupInvitationAcceptanceTask {
    std::uint32_t prottype =
        DEF_PROT_GROUP_INVITE_DECISION_RQ;

    std::uint32_t requestId = 0;
    std::uint32_t invitationId = 0;

    std::int32_t groupId = 0;
    std::int32_t inviterId = 0;
    std::int32_t targetUserId = 0;
};

// 群聊消息结构
struct PendingGroupChat {
    std::uint32_t requestId = 0;
    std::int32_t groupId = 0;
    std::int32_t senderId = 0;

    std::vector<std::int32_t> recipients;
    std::vector<char> body;
};

// 文件发送结构
struct PendingFileOffer {
    std::uint32_t requestId = 0;
    std::int32_t senderId = 0;
    std::int32_t targetUserId = 0;

    PROT_FILE_OFFER_NOTIFY notification{};
};

struct RemovedFriendship {
        std::int32_t firstUserId = 0;
            std::int32_t secondUserId = 0;
};

struct DispatchResult{
    std::vector<char> body;

    //有值表示本次登录验证成功，等待事件线程绑定身份
    std::optional<std::int32_t> authenticatedUser;

    //主回复之后，一次发给同一连接的消息体
    std::vector<std::vector<char>> followingBodies;

    std::vector<UserNotification> notifications;
    std::optional<PendingPrivateChat> privateChat;
    std::optional<PendingFriendRequest> friendRequest;
    std::optional<EstablishedFriendship> establishedFriendship;
    std::optional<PendingGroupInvitation> groupInvitation;
    bool groupMembershipCommitted = false;
    std::optional<PendingGroupChat> groupChat;
    std::optional<PendingFileOffer> fileOffer;
    std::optional<RemovedFriendship> removedFriendship;
    bool profileUpdated = false;
};

class Dispatcher{
    public:
        explicit Dispatcher(MySqlPool& mysqlPool):m_mysqlPool(mysqlPool){}
        //body 只包含消息体，不包含外层的 4 字节长度头
        DispatchResult dispatch(std::uint64_t connectionId,const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        //这里不传 Connection& ，因为之后这个函数会在工作线程运行。连接和接收缓冲区由时间线程管理，业务线程处理独立的消息数据，避免两边同时访问连接容器

    private:
        std::vector<char> handleRegister(const std::vector<char>& body) const;
        DispatchResult handleLogin(const std::vector<char>& body) const;
        DispatchResult handleChat(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleAddFriendRequest(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleFriendAcceptance(const std::vector<char>& body,std::optional<std::int32_t> authentiactedUser) const;
        DispatchResult handleGroupCreate(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleGroupMembers(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleGroupInvite(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleGroupAcceptance(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleGroupChat(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleFileOffer(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleDeleteFriend(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleGroupLeave(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleUpdateProfile(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;
        DispatchResult handleChangePassword(const std::vector<char>& body,std::optional<std::int32_t> authenticatedUser) const;

    private:
        MySqlPool& m_mysqlPool;
};
