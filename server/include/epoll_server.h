#pragma once
#include <unordered_map>
#include "connection.h"
#include "thread_pool.h"
#include "session_manager.h"
#include "dispatcher.h"
#include "group_protocol.h"
#include "file_protocol.h"
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <optional>
#include <unordered_set>
#include <chrono>
#include <map>
#include <utility>

class MySqlPool;

class EpollServer{
    public:
        EpollServer(unsigned short port,MySqlPool& mysqlPool);
        ~EpollServer();

        //对象拥有socket 和 epoll 资源，禁止复制
        EpollServer(const EpollServer&) = delete;
        EpollServer& operator=(const EpollServer&) = delete;

        //初始化网络和 epoll，成功返回 true
        bool init();

        //运行事件循环
        void run();

    private:
        //创建、绑定并监听 socket
        bool initListenSocket();

        //创建 epoll 树，并注册监听 server_socket
        bool initEpoll();

        //接收等待中的客户端连接
        void acceptConnections();

        void handleClientRead(int clientFd);

        void closeConnection(int clientFd);

        //解析当前接收缓冲区
        //false 表示协议长度非法或任务提交失败，需要关闭连接
        bool processReceived(Connection& connection);

        bool initWakeupFd();
        void handleWakeup();

        struct WorkResult
        {
            std::uint64_t connectionId;
            std::vector<char> body;
            bool success = true;

            std::optional<std::int32_t> authenticatedUser;
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

        bool postResult(WorkResult result);

        bool updateClientEvents(int clientFd);
        void handleClientWrite(int clientFd);

        bool queueReply(int clientFd,const std::vector<char>& body);

        //空闲时提交下一条消息
        //没有消息或当前忙碌，也视为正常，返回true
        //线程池拒绝提交时返回false
        bool startNextMessage(Connection& connection);

        //向指定用户发送通知的接口
        bool sendNotification(const UserNotification& notification);

        struct FriendApplication{
            std::uint64_t applicantConnectionId;
            std::uint64_t targetConnectionId;

            //服务端校正过申请者昵称的申请消息
            std::vector<char> requestBody;

            std::chrono::steady_clock::time_point expiresAt;
        };

        //好友申请过期清理接口
        void removeExpiredFriendApplications();

        //事件线程处理好友申请接口
        //false 表示申请者身份或内部结果不合法，需要关闭连接
        bool processFriendRequestResult(WorkResult& result);

        //好友申请回复处理函数
        bool handleFriendReply(Connection& connection,const std::vector<char>&body);

        //好友缓存更新接口
        void applyEstablishedFriendship(const EstablishedFriendship& friendship);
        void refreshUserInfoStatus(std::vector<char>& body);

        // 已发出、等待对方确认的群邀请
        struct GroupInvitationRecord {
            std::uint64_t inviterConnectionId = 0;
            std::uint64_t targetConnectionId = 0;

            std::int32_t inviterId = 0;
            std::int32_t targetUserId = 0;
            std::int32_t groupId = 0;

            PROT_GROUP_INVITE_NOTIFY notification{};

            std::chrono::steady_clock::time_point expiresAt{};
        };

        // 删除过期或参与者连接已经失效的邀请。
        void removeInvalidGroupInvitations();

        // 邀请处理函数
        // false 表示来源身份或内部邀请数据不合法
        bool processGroupInvitationResult(WorkResult& result);

        // 邀请回复处理函数
        bool handleGroupInviteDecision(Connection& connection,const std::vector<char>& body);

        // 群聊请求处理函数
        bool processGroupChatResult(WorkResult& result);

        //文件传输处理部分
        enum class FileTransferState {WaitingForDecision,Transferring,WaitingForCompletion};

        struct FileTransferRecord {
            std::uint64_t senderConnectionId = 0;
            std::uint64_t targetConnectionId = 0;

            std::int32_t senderId = 0;
            std::int32_t targetUserId = 0;

            FileTransferState state =FileTransferState::WaitingForDecision;

            PROT_FILE_OFFER_NOTIFY notification{};
            
            // 已经收到接收方确认的累计字节数
            std::uint64_t confirmedBytes = 0;

            // 当前正在等待确认的数据块
            std::uint64_t pendingOffset = 0;
            std::uint32_t pendingLength = 0;

            bool waitingForChunkAck = false;

            std::chrono::steady_clock::time_point expiresAt{};
        };

        //文件传输结果
        bool processFileOfferResult(WorkResult& result);

        //文件传输清理函数
        void finishFileTransfer(std::uint32_t transferId,std::int32_t result);
        void removeInvalidFileTransfers();

        //文件传输回复处理函数
        bool handleFileDecision(Connection& connection,const std::vector<char>& body);

        //处理收到的文件块
        bool handleFileChunk(Connection& connection,const std::vector<char>& body);

        //处理接收方发来的数据块接收信息
        bool handleFileChunkAck(Connection& connection,const std::vector<char>& body);

        //处理数据发送结束请求
        bool handleFileFinishRequest(Connection& connection,const std::vector<char>& body);

        //最终结果处理函数
        bool handleFileFinishResult(Connection& connection,const std::vector<char>& body);

        void applyRemovedFriendship(const RemovedFriendship& friendship);

    private:
        unsigned short m_port;
        int m_listenFd;
        int m_epollFd;
        std::uint64_t m_nextConnectionId = 1;
        int m_wakeupFd = -1;
        //记录当前连接，为 Connection 对象
        std::unordered_map<int,Connection> m_connections;
        MySqlPool& m_mysqlPool;
        ThreadPool m_threadPool;
        std::mutex m_resultMutex;
        std::queue<WorkResult> m_results;

        //工作线程只负责报告故障，由事件线程决定退出
        std::atomic<bool> m_resultDeliveryFailed{false};

        SessionManager m_sessions;

        //每次登录对应的好友通知名单，仅由事件线程访问
        std::unordered_map<std::uint64_t,std::vector<std::int32_t>> m_friendsByConnection;

        //键：申请者用户ID，目标用户ID
        using FriendApplicationKey = std::pair<std::int32_t,std::int32_t>;

        std::map<FriendApplicationKey,FriendApplication> m_friendApplications;

        // 邀请编号 -> 待确认邀请
        std::unordered_map<std::uint32_t, GroupInvitationRecord> m_groupInvitations;

        // 0 不作为有效邀请编号。
        std::uint32_t m_nextGroupInvitationId = 1;

        std::unordered_map<std::uint32_t, FileTransferRecord> m_fileTransfers;
        std::uint32_t m_nextFileTransferId = 1;
};
