#include "epoll_server.h"
#include "connection.h"
#include "dispatcher.h"
#include "protocol.h"
#include "session_manager.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cerrno>
#include <cstdint>
#include <utility>
#include <sys/eventfd.h>
#include <exception>
#include <cstring>
#include <algorithm>

EpollServer::EpollServer(unsigned short port,MySqlPool& mysqlPool):
    m_port(port),
    m_listenFd(-1),
    m_epollFd(-1),
    m_mysqlPool(mysqlPool),
    m_threadPool(2,1024)
{}

EpollServer::~EpollServer(){
    m_threadPool.stop();

    //关闭所有客户端
    for(const auto& entry : m_connections){
        close(entry.first);
    }

    if(m_wakeupFd != -1){
        close(m_wakeupFd);
    }

    if(m_epollFd != -1){
        close(m_epollFd);
    }

    if(m_listenFd != -1){
        close(m_listenFd);
    }
}

bool EpollServer::initListenSocket(){
    //创建socket
    m_listenFd = socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);

    if(m_listenFd == -1){
        perror("EpollServer::initListenSocket create listen sock fail");
        return false;
    }

    //设置地址复用，便于服务端停止后重新绑定
    int reuse = 1;
    if((setsockopt(m_listenFd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse)))==-1){
        perror("EpollServer::initListenSocket setsockopt fail");
        return false;
    }

    //绑定ip和端口号
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if((bind(m_listenFd,(sockaddr*)&addr,sizeof(addr)))==-1){
        perror("EpollServer::initListenSocket bind fail");
        return false;
    }

    //开启监听
    if((listen(m_listenFd,128))==-1){
        perror("EpollServer::initListenSocket listen fail");
        return false;
    }

    return true;
}

bool EpollServer::initEpoll(){
    //创建 epoll 树
    m_epollFd = epoll_create1(EPOLL_CLOEXEC);

    if(m_epollFd == -1){
        perror("EpollServer::initEpoll create epfd fail");
        return false;
    }

    //设置监听 socket 对应的事件
    epoll_event event{};
    event.data.fd = m_listenFd;
    event.events = EPOLLIN;

    if((epoll_ctl(m_epollFd,EPOLL_CTL_ADD,m_listenFd,&event))==-1){
        perror("EpollServer::initEpoll call epoll_ctl to add node fail");
        return false;
    }

    return true;
}

bool EpollServer::init(){
    //只尝试一次初始化
    if(m_listenFd != -1 || m_epollFd != -1){
        fprintf(stderr,"Server initialization already attempted\n");
        return false;
    }

    if(!initListenSocket()){
        return false;
    }

    if(!initEpoll()){
        return false;
    }
    printf("Listening on port %hu\n",m_port);

    if(!initWakeupFd()){
        return false;
    }
    return true; 
}

void EpollServer::run(){
    if(m_listenFd == -1 || m_epollFd == -1){
        fprintf(stderr,"server is not initialized\n");
        return;
    }

    constexpr int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS]{};

    while(true){
        //故障检查
        if(m_resultDeliveryFailed.load()){
            fprintf(stderr,"Stopping server: result delivery is unavailable\n");
            return;
        }

        //清除超时的好友申请
        removeExpiredFriendApplications();

        //清除超时的文件传输申请
        removeInvalidFileTransfers();

        //没有事件时阻塞等待，不帮忙轮询
        int readyCount = epoll_wait(m_epollFd,events,MAX_EVENTS,1000);
        if(readyCount == -1){
            //被信号中断时，重新等待
            if(errno == EINTR)
                continue;

            perror("EpollServer::run call epoll_wait failed");
            return;
        }

        //再次故障检查
        if(m_resultDeliveryFailed.load()){
            fprintf(stderr,"Stopping server: reslut delivery is unavaliable\n");
            return;
        }

        //只处理本次实际返回的事件
        for(int i=0;i<readyCount;i++){
            int fd = events[i].data.fd;
            auto flags = events[i].events;

            //不是客户端，必须在客户端连接容器检查之前单独处理
            if(fd == m_wakeupFd){
                if(flags & (EPOLLERR | EPOLLHUP)){
                    fprintf(stderr,"Wakeup fd error\n");
                    return;
                }

                if(flags & EPOLLIN){
                    handleWakeup();
                }
                continue;
            }

            if(fd == m_listenFd){
                if(flags & (EPOLLERR | EPOLLHUP)){
                    fprintf(stderr,"Listening socket error\n");
                    return;
                }

                if(flags & EPOLLIN){
                    acceptConnections();
                }
                continue;
            }

            if(m_connections.count(fd) == 0){
                continue;
            }

            //关闭通知可能与可读事件同时到达，先尝试读完数据
            if(flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)){
                handleClientRead(fd);
            }

            //读取过程中可能已经关闭连接，必须先检查
            if((flags & EPOLLOUT) && m_connections.count(fd)!= 0){
                handleClientWrite(fd);
            }

            //读取过程中可能已经关闭连接，因此需要再次检查
            if((flags & EPOLLERR)&& m_connections.count(fd) != 0){
                closeConnection(fd);
            }
        }
    }
}

//接收连接
void EpollServer::acceptConnections(){
    while(true){
        //直接创建非阻塞、执行新程序时自动关闭的连接socket
        int clientFd = accept4(m_listenFd,nullptr,nullptr,SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(clientFd == -1){
            if(errno == EINTR){
                continue;
            }

            if(errno == EAGAIN || errno == EWOULDBLOCK){
                //当前等待接入的连接已经全部处理完
                return;
            }

            perror("EpollServer::acceptConnections called accept4 failed");
            return;
        }

        //设置监听
        epoll_event event{};
        event.data.fd = clientFd;
        event.events = EPOLLIN | EPOLLRDHUP;

        if(epoll_ctl(m_epollFd,EPOLL_CTL_ADD,clientFd,&event)==-1){
            perror("EpollServer::acceptConnections called epoll_ctl to add clientFd failed");
            close(clientFd);
            continue;
        }

        //第一个是映射的键，用来查找连接，后两个参数传给 Connection 构造函数
        std::uint64_t connectionId = m_nextConnectionId++;
        m_connections.try_emplace(clientFd,clientFd,connectionId);
        printf("Client connected: fd=%d, id=%llu\n",clientFd,static_cast<unsigned long long>(connectionId));
    }
}

//实现断线清理
void EpollServer::closeConnection(int clientFd){
    //必须先去的连接编号，再删除 Connection
    auto it = m_connections.find(clientFd);

    if(it == m_connections.end()){
        return;
    }

    std::uint64_t connectionId = it->second.id();

    //删除好友申请
    for(auto application = m_friendApplications.begin();application != m_friendApplications.end();){
        const auto& record = application->second;

        if(record.applicantConnectionId == connectionId || record.targetConnectionId == connectionId){
            application = m_friendApplications.erase(application);
        }
        else{
            ++application;
        }
    }

    //先取走通知名单
    std::vector<std::int32_t> friendIds;

    auto friendsIt = m_friendsByConnection.find(connectionId);

    if(friendsIt != m_friendsByConnection.end()){
        friendIds = std::move(friendsIt->second);
        m_friendsByConnection.erase(friendsIt);
    }

    auto userId = m_sessions.unbind(connectionId);

    m_connections.erase(it);

    if(epoll_ctl(m_epollFd,EPOLL_CTL_DEL,clientFd,nullptr) == -1){
        perror("epoll_ctl del client");
    }

    close(clientFd);

    printf("Client disconnected: fd=%d\n",clientFd);

    if(!userId){
        return;
    }

    printf("Session removed: user=%d, connection=%llu\n",static_cast<int>(*userId),static_cast<unsigned long long>(connectionId));

    PROT_FRIEND_OFFLINE offline{};
    offline.offUserid = *userId;

    std::vector<char> body(sizeof(offline));

    std::memcpy(body.data(),&offline,sizeof(offline));

    //当前连接已经移除，再通知仍然在线的好友
    for(std::int32_t friendId : friendIds){
        sendNotification(UserNotification{friendId,body});
    }
}

//实现读取
void EpollServer::handleClientRead(int clientFd){
    //保存数据
    auto it = m_connections.find(clientFd);

    if(it == m_connections.end()){
        return;
    }
    Connection& connection = it->second;
    char buffer[4096];

    while(true){
        ssize_t received = recv(clientFd,buffer,sizeof(buffer),0);

        if(received > 0){
            connection.appendReceived(buffer,static_cast<std::size_t>(received));
            printf("fd = %d: received = %zd, buffered = %zu\n",clientFd,received,connection.recvBuffer().size());
            if(!processReceived(connection)){
                closeConnection(clientFd);
                return;
            }
            continue;
        }

        if(received == 0){
            //对端正常关闭发送方向，本阶段直接关闭连接
            closeConnection(clientFd);
            return;
        }

        if(errno == EINTR){
            continue;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK){
            //当前可读数据已经读完
            return;
        }

        perror("EpollServer::handleClientRead called recv failed");
        closeConnection(clientFd);
        return;
    }
}

//解析并提交信息
//false 表示需要关闭连接：协议错误、过载或主动下线
bool EpollServer::processReceived(Connection& connection){
    constexpr std::size_t HEADER_SIZE = 4;
    constexpr std::size_t MAX_BODY_SIZE = 64*1024;

    //按照小端格式读取 4 字节无符号整数
    auto readLittleEndian32 = [](const char* data)->std::uint32_t{
        const auto* bytes = reinterpret_cast<const unsigned char*>(data);
        return static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1])<<8)
            | (static_cast<std::uint32_t>(bytes[2])<<16)
            | (static_cast<std::uint32_t>(bytes[3])<<24);
    };

    while(true){
        const auto& buffer = connection.recvBuffer();

        //长度头尚未收齐，保留数据，等待下一次接收
        if(buffer.size()<HEADER_SIZE){
            return true;
        }

        std::uint32_t bodyLength = readLittleEndian32(buffer.data());

        //消息体至少包含 4 字节协议类型
        if(bodyLength < 4 || bodyLength > MAX_BODY_SIZE){
            fprintf(stderr,"Invalid body length: fd=%d, length=%u\n",connection.fd(),static_cast<unsigned int>(bodyLength));
            return false;
        }

        std::size_t packetLength = HEADER_SIZE + bodyLength;
        //消息体尚未收齐，不删除任何数据
        if(buffer.size()<packetLength){
            return true;
        }

        //消息体开头的 4 字节是协议编号
        std::uint32_t protocolType = readLittleEndian32(buffer.data() + HEADER_SIZE);

        printf("Complete packet: fd=%d, body=%u, protocol=%u\n",connection.fd(),static_cast<unsigned int>(bodyLength),static_cast<unsigned int>(protocolType));

        //下线判断
        if(protocolType == DEF_PROT_FRIEND_OFFLINE){
            if(bodyLength != sizeof(PROT_FRIEND_OFFLINE)){
                fprintf(stderr,"Invalid offline request size\n");
                return false;
            }

            auto userId = m_sessions.userForConnection(connection.id());

            if(!userId){
                fprintf(stderr,"Offline request from unauthenticated connection\n");
                return false;
            }

            PROT_FRIEND_OFFLINE request{};

            std::memcpy(&request,buffer.data() + HEADER_SIZE,sizeof(request));

            if(request.offUserid != *userId){
                fprintf(stderr,"Offline request identity mismatch\n");
                return false;
            }

            printf("Offline requested: user=%d\n",static_cast<int>(*userId));

            //让 handleClientRead() 统一调用 closeConnection()
            return false;
        }

        //分发业务
        //复制消息体，不包含外层长度头
        using Difference = std::vector<char>::difference_type;

        std::vector<char> body(buffer.begin() + static_cast<Difference>(HEADER_SIZE),buffer.begin() + static_cast<Difference>(packetLength));

        // 邀请决定需要由事件线程核对待确认邀请记录
        if (protocolType == DEF_PROT_GROUP_INVITE_DECISION_RQ) {
            // 先消费接收缓冲区，再处理，避免处理期间连接被删除
            connection.consumeReceived(packetLength);

            if (!handleGroupInviteDecision(connection, body)) {
                return false;
            }

            continue;
        }

        // 文件决定由事件线程核对传输记录
        if (protocolType == DEF_PROT_FILE_DECISION_RQ) {
            connection.consumeReceived(packetLength);

            if (!handleFileDecision(connection, body)) {
                return false;
            }

            continue;
        }

        // 文件传输由事件线程核对文件信息
        if (protocolType == DEF_PROT_FILE_CHUNK_RQ) {
            connection.consumeReceived(packetLength);

            if (!handleFileChunk(connection, body)) {
                return false;
            }

            continue;
        }

        // 文件传输由事件线程核对接收文件信息
        if (protocolType == DEF_PROT_FILE_CHUNK_ACK) {
            connection.consumeReceived(packetLength);

            if (!handleFileChunkAck(connection, body)) {
                return false;
            }

            continue;
        }

        // 由事件线程核对接收数据发送结束请求信息
        if (protocolType == DEF_PROT_FILE_FINISH_RQ) {
            connection.consumeReceived(packetLength);

            if (!handleFileFinishRequest(connection, body)) {
                return false;
            }

            continue;
        }

        // 由事件线程核对最终结果信息
        if (protocolType == DEF_PROT_FILE_FINISH_RS) {
            connection.consumeReceived(packetLength);

            if (!handleFileFinishResult(connection, body)) {
                return false;
            }

            continue;
        }

        if(protocolType == DEF_PROT_ADD_FRIEND_RS){
            //先消费原始包，处理回复时可能触发连接清理
            connection.consumeReceived(packetLength);

            if(!handleFriendReply(connection,body)){
                return false;
            }

            continue;
        }

        //完整消息先进入当前连接的待处理队列
        if(!connection.enqueMessage(std::move(body))){
            fprintf(stderr,"Pending message queue full: fd=%d\n",connection.fd());
            return false;
        }

        //消息体已经独立保存，可以删除接收缓冲区中的原始包
        connection.consumeReceived(packetLength);

        //当前空闲则提交一条：忙碌则让消息留在队列里
        if(!startNextMessage(connection)){
            return false;
        }
    }
}

bool EpollServer::initWakeupFd(){
    m_wakeupFd = eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);

    if(m_wakeupFd == -1){
        perror("EpollServer::initWakeupFd called eventfd failed");
        return false;
    }

    epoll_event event{};
    event.data.fd = m_wakeupFd;
    event.events = EPOLLIN;

    if(epoll_ctl(m_epollFd,EPOLL_CTL_ADD,m_wakeupFd,&event)==-1){
        perror("EpollServer::initWakeupFd call epoll_ctl add wakeup fd failed");
        return false;
    }
    return true;
}

//实现读取通知
void EpollServer::handleWakeup(){
    std::uint64_t count = 0;

    while(true){
        ssize_t readResult = read(m_wakeupFd,&count,sizeof(count));

        if(readResult == static_cast<ssize_t>(sizeof(count))){
            printf("Event loop awakened: count=%llu\n",static_cast<unsigned long long>(count));


            //取出工作线程提交的处理结果
            std::queue<WorkResult> readyResults;
            {
                std::lock_guard<std::mutex> lock(m_resultMutex);
                readyResults.swap(m_results);
            }

            //解锁后处理，避免工作线程长时间等待结果队列锁
            while(!readyResults.empty()){
                WorkResult result = std::move(readyResults.front());
                readyResults.pop();

                if (result.success && result.removedFriendship) {
                    applyRemovedFriendship(*result.removedFriendship);
                }

                //数据库可能已经提交，即使原连接断开
                //仍然更新当前在线用户的好友缓存
                if(result.success && result.establishedFriendship){
                    applyEstablishedFriendship(*result.establishedFriendship);
                }

                auto connectionIt = m_connections.end();

                for(auto it = m_connections.begin();it!=m_connections.end();++it){
                    if(it->second.id() == result.connectionId){
                        connectionIt = it;
                        break;
                    }
                }

                if(connectionIt == m_connections.end()){
                    //处理同意者已经断线，但数据库已经入库
                    if (result.success && (result.establishedFriendship || result.groupMembershipCommitted ||
                                result.removedFriendship ||result.profileUpdated)) {
                        //关系已经保存，仍然向其他参与者发送通知
                        for(auto& notification : result.notifications){
                            refreshUserInfoStatus(notification.body);
                            sendNotification(notification);
                        }
                    }

                    printf("Discard origin reply: connection=%llu is closed\n",static_cast<unsigned long long>(result.connectionId));
                    continue;
                }

                printf("Result ready: connection=%llu,fd=%d;body=%zu\n",static_cast<unsigned long long>(result.connectionId),connectionIt->first,result.body.size());
                //追加发送缓冲区
                int clientFd = connectionIt->first;
                Connection& connection = connectionIt->second;

                //失败时直接删除连接，连通等待中的业务消息一起清理，不再执行下一条请求
                if(!result.success){
                    fprintf(stderr,"Closing connection after business failure: id=%llu\n",static_cast<unsigned long long>(result.connectionId));

                    closeConnection(clientFd);
                    continue;
                }

                if(result.authenticatedUser){
                    std::int32_t userId = *result.authenticatedUser;

                    auto bindResult = m_sessions.bind(userId,result.connectionId);

                    if(bindResult == SessionManager::BindResult::Success){
                        std::vector<std::int32_t> friendIds;
                        std::unordered_set<std::int32_t> seen;

                        for(const auto& notification:result.notifications){
                            std::int32_t friendId = notification.targetUserId;

                            if(friendId != userId && seen.insert(friendId).second){
                                friendIds.push_back(friendId);
                            }
                        }

                        m_friendsByConnection[result.connectionId] = std::move(friendIds);

                        printf("Session bound: user=%d, connection=%llu\n",static_cast<int>(userId),static_cast<unsigned long long>(result.connectionId));
                    }
                    else{
                        //验证密码成功，但绑定身份被拒绝
                        //原来的成功回复改成对应失败回复
                        PROT_LOGIN_RS reply{};

                        if(bindResult == SessionManager::BindResult::userAlreadyOnline){
                            reply.result = LOGIN_RESULT_ALREADY_ONLINE;
                        }
                        else{
                            reply.result = LOGIN_RESULT_CONNECTION_LOGGED_IN;
                        }

                        reply.userid = 0;

                        result.body.resize(sizeof(reply));

                        std::memcpy(result.body.data(),&reply,sizeof(reply));

                        //登录被拒绝，不发送原本准备的本人或好友信息和上线信息
                        result.followingBodies.clear();
                        result.notifications.clear();
                    }
                }

                //填写好友在线状态
                refreshUserInfoStatus(result.body);

                for(auto& body : result.followingBodies){
                    refreshUserInfoStatus(body);
                }

                for(auto& notification : result.notifications){
                    refreshUserInfoStatus(notification.body);
                }

                //当前任务的结果已经交回事件线程
                connection.finishMessage();

                //转发私聊
                if(result.privateChat){
                    auto senderId = m_sessions.userForConnection(result.connectionId);

                    //再次确认当前连接仍然有登录身份
                    if(!senderId || result.privateChat->targetUserId == *senderId){
                        closeConnection(clientFd);
                        continue;
                    }

                    bool forward = sendNotification(UserNotification{result.privateChat->targetUserId,result.privateChat->body});

                    //清理目标连接时，可能通过下线通知间接触发其他连接的清理，因此重新确认发送者连接仍然存在
                    auto current = m_connections.find(clientFd);

                    if(current == m_connections.end() || current->second.id() != result.connectionId){
                        continue;
                    }

                    PROT_CHAT_INFO_RS reply{};
                    reply.myid = result.privateChat->targetUserId;
                    reply.friendid = *senderId;
                    reply.result = forward ? CHAT_RESULT_SUCC : CHAT_RESULT_FAIL;

                    result.body.resize(sizeof(reply));

                    std::memcpy(result.body.data(),&reply,sizeof(reply));
                }

                //好友申请转发
                if(result.friendRequest){
                    if(!processFriendRequestResult(result)){
                        closeConnection(clientFd);
                        continue;
                    }

                    //好友申请转发失败时，清理其他可能影响当前连接的连接
                    auto current = m_connections.find(clientFd);

                    if(current == m_connections.end() || current->second.id() != result.connectionId){
                        continue;
                    }
                }

                //群邀请转发
                if (result.groupInvitation) {
                    if (!processGroupInvitationResult(result)) {
                        closeConnection(clientFd);
                        continue;
                    }

                    // 转发可能触发目标连接及关联连接的清理，
                    // 因此重新确认来源连接仍然有效。
                    auto current = m_connections.find(clientFd);

                    if (current == m_connections.end() ||
                            current->second.id() != result.connectionId) {
                        continue;
                    }
                }

                //群消息转发
                if (result.groupChat) {
                    if (!processGroupChatResult(result)) {
                        closeConnection(clientFd);
                        continue;
                    }


                    // 转发可能触发关联连接清理，重新核对来源连接
                    auto current = m_connections.find(clientFd);

                    if (current == m_connections.end() ||
                            current->second.id() != result.connectionId) {
                        continue;
                    }
                }

                // 文件申请转发
                if (result.fileOffer) {
                    if (!processFileOfferResult(result)) {
                        closeConnection(clientFd);
                        continue;
                    }

                    auto current = m_connections.find(clientFd);

                    if (current == m_connections.end() ||
                            current->second.id() != result.connectionId) {
                        continue;
                    }
                }

                //先排队主回复
                bool queued = result.body.empty() || queueReply(clientFd,result.body);

                //再按照顺序排队后续消息
                if(queued){
                    for(const auto& followingBody : result.followingBodies){
                        if(!queueReply(clientFd,followingBody)){
                            queued = false;
                            break;
                        }
                    }
                }

                if(!queued){
                    closeConnection(clientFd);
                    continue;
                }

                //发送通知
                for(const auto& notification : result.notifications){
                    //只发给其他用户，避免关闭通知目标
                    //删除正在使用的 connection
                    if(result.authenticatedUser && notification.targetUserId == *result.authenticatedUser){
                        continue;
                    }

                    sendNotification(notification);
                }

                auto currentConnection = m_connections.find(clientFd);
                if(currentConnection == m_connections.end() || currentConnection->second.id() != result.connectionId){
                    continue;
                }
                //所有回复都排入发送缓冲区后，才启动下一条业务
                if(!startNextMessage(currentConnection->second)){
                    closeConnection(clientFd);
                    continue;
                }

                //此后不要再使用 connectionIt，连接可能已被删除
            }
            return;
        }

        if(readResult == -1 && errno == EINTR){
            continue;
        }

        if(readResult == -1 &&(errno == EAGAIN || errno == EWOULDBLOCK)){
            return;
        }

        if(readResult == -1){
            perror("EpollServer::handleWakeup read wakeup fd failed");
        }
        else{
            fprintf(stderr,"Unexpected eventfd read length\n");
        }
        return;
    }
}


bool EpollServer::postResult(WorkResult result){
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);

        //暂时设置一个结果队列容量上限
        if(m_results.size() >= 1024){
            return false;
        }

        m_results.push(std::move(result));
    }

    //必须先入队，再通知事件线程
    std::uint64_t value = 1;

    while(true){
        ssize_t written = write(m_wakeupFd,&value,sizeof(value));

        if(written == static_cast<ssize_t>(sizeof(value))){
            return true;
        }

        if(written == -1 && errno == EINTR){
            continue;
        }

        if(written ==  -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            //eventfd 计数已满，此时本身已经可读
            return true;
        }

        if(written == -1){
            perror("write wakeup fd");
        }
        else{
            fprintf(stderr,"Unexpected eventfd write length\n");
        }

        //结果已入队，但通知失败
        return false;
    }
}


//实现 epoll 事件更新
bool EpollServer::updateClientEvents(int clientFd){
    auto it = m_connections.find(clientFd);

    if(it == m_connections.end()){
        return false;
    }

    epoll_event event{};
    event.data.fd = clientFd;
    event.events = EPOLLIN | EPOLLRDHUP;

    //仅在有待发送数据时监听可写事件
    if(it->second.pendingSendBytes() > 0){
        event.events |= EPOLLOUT;
    }

    if(epoll_ctl(m_epollFd,EPOLL_CTL_MOD,clientFd,&event) == -1){
        perror("epoll_ctl modify client");
        return false;
    }

    return true;
}

//实现发送函数
void EpollServer::handleClientWrite(int clientFd){
    auto it = m_connections.find(clientFd);

    if(it == m_connections.end()){
        return;
    }

    Connection& connection = it->second;

    while(connection.pendingSendBytes() > 0){
        ssize_t sent = send(clientFd,connection.pendingSendData(),connection.pendingSendBytes(),MSG_NOSIGNAL);

        if(sent > 0){
            connection.consumeSent(static_cast<std::size_t>(sent));
            continue;
        }

        if(sent == -1 && errno == EINTR){
            continue;
        }

        if(sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
            //暂时写不进去，保留剩余数据
            break;
        }

        if(sent == -1){
            perror("EpollServer::handleClientWrite call send failed");
        }
        else{
            fprintf(stderr,"send returned zero\n");
        }

        closeConnection(clientFd);
        return;
    }

    //未发完则保留 EPOLLOUT，发完则取消
    if(!updateClientEvents(clientFd)){
        closeConnection(clientFd);
    }
}

//封装长度头+消息体并排队
bool EpollServer::queueReply(int clientFd,const std::vector<char>& body){
    auto it = m_connections.find(clientFd);

    if(it == m_connections.end()){
        return false;
    }

    if(body.size() <4 || body.size() > 64*1024){
        fprintf(stderr,"Invalid reply body size\n");
        return false;
    }

    std::uint32_t length = static_cast<std::uint32_t>(body.size());

    std::vector<char> packet;

    //unsigned char 可以直接保存小端头的原始字节
    unsigned char header[4]{
        static_cast<unsigned char>(length & 0xff),
            static_cast<unsigned char>((length >> 8) & 0xff),
            static_cast<unsigned char>((length >> 16) & 0xff),
            static_cast<unsigned char>((length >> 24) & 0xff)
    };

    const char* headerBytes = reinterpret_cast<const char*>(header);

    packet.reserve(4 + body.size());
    packet.insert(packet.end(),headerBytes,headerBytes + 4);
    packet.insert(packet.end(),body.begin(),body.end());

    if(!it->second.appendToSend(packet.data(),packet.size())){
        fprintf(stderr,"Send buffer capacity exceeded\n");
        return false;
    }

    return updateClientEvents(clientFd);
}

bool EpollServer::startNextMessage(Connection& connection){
    std::vector<char> body;

    if(!connection.beginNextMessage(body)){
        return true;
    }

    std::uint64_t connectionId = connection.id();

    //获取身份快照
    auto authenticatedUser = m_sessions.userForConnection(connectionId);

    bool accepted = m_threadPool.trySubmint(
            [this,connectionId,authenticatedUser,body = std::move(body)]() mutable{
            WorkResult result{};
            result.connectionId = connectionId;

            try{
            Dispatcher dispatcher(m_mysqlPool);

            DispatchResult dispatched = dispatcher.dispatch(connectionId,body,authenticatedUser);
            result.body = std::move(dispatched.body);
            result.authenticatedUser = dispatched.authenticatedUser;
            result.followingBodies = std::move(dispatched.followingBodies);
            result.notifications = std::move(dispatched.notifications);
            result.privateChat = std::move(dispatched.privateChat);
            result.friendRequest = std::move(dispatched.friendRequest);
            result.establishedFriendship = dispatched.establishedFriendship;
            result.groupInvitation = std::move(dispatched.groupInvitation);
            result.groupMembershipCommitted = dispatched.groupMembershipCommitted;
            result.groupChat = std::move(dispatched.groupChat);
            result.fileOffer = std::move(dispatched.fileOffer);
            result.removedFriendship = dispatched.removedFriendship;
            result.profileUpdated = dispatched.profileUpdated;
            }

            catch(const std::exception& error){
                result.success = false;

                fprintf(stderr,"Business failed: connection=%llu, reason=%s\n",static_cast<unsigned long long>(connectionId),error.what());
            }
            catch(...){
                result.success = false;

                fprintf(stderr,"Business failed: connection=%llu, unknown exception\n",static_cast<unsigned long long>(connectionId));
            }

            try{
                if(!postResult(std::move(result))){
                    m_resultDeliveryFailed.store(true);
                    fprintf(stderr,"Result delivery failed: connection=%llu\n",static_cast<unsigned long long>(connectionId));
                }
            }
            catch(const std::exception& error){
                m_resultDeliveryFailed.store(true);

                fprintf(stderr,"Result delivery exception: %s\n",error.what());
            }
            catch(...){
                m_resultDeliveryFailed.store(true);
                fprintf(stderr,"Unknown result delivery exception\n");
            }
            }
    );

    if(!accepted){
        connection.finishMessage();

        fprintf(stderr,"Task submission rejected: connection=%llu\n",static_cast<unsigned long long>(connectionId));

        return false;
    }

    return true;
}

//向指定用户发送通知的接口
bool EpollServer::sendNotification(const UserNotification& notification){
    auto connectionId = m_sessions.connectionForUser(notification.targetUserId);

    if(!connectionId){
        //对方离线，不发送实时通知
        return false;
    }

    int targetFd = -1;

    for(const auto& entry : m_connections){
        if(entry.second.id() == *connectionId){
            targetFd = entry.first;
            break;
        }
    }

    if(targetFd == -1){
        return false;
    }

    if(!queueReply(targetFd,notification.body)){
        closeConnection(targetFd);
        return false;
    }

    printf("Notification queued: target user=%d, connection=%llu\n",static_cast<int>(notification.targetUserId),static_cast<unsigned long long>(*connectionId));

    return true;
}

//好友申请过期清理接口
void EpollServer::removeExpiredFriendApplications(){
    auto now = std::chrono::steady_clock::now();

    for(auto it = m_friendApplications.begin();it!= m_friendApplications.end();){
        if(it->second.expiresAt <= now){
            it = m_friendApplications.erase(it);
        }
        else{
            ++it;
        }
    }
}

bool EpollServer::processFriendRequestResult(WorkResult& result){
    if(!result.friendRequest){
        return true;
    }

    const auto& request = *result.friendRequest;

    auto applicantId = m_sessions.userForConnection(result.connectionId);

    if(!applicantId || *applicantId != request.applicantId || request.targetId == *applicantId){
        return false;
    }

    if(result.body.size() != sizeof(PROT_ADD_FRIEND_RS) || request.body.size() != sizeof(PROT_ADD_FRIEND_RQ)){
        return false;
    }

    //修改已经准备好的申请结果，保留里面的昵称
    auto setReplyResult = [&](std::int32_t code){
        PROT_ADD_FRIEND_RS reply{};

        std::memcpy(&reply,result.body.data(),sizeof(reply));

        reply.result = code;

        std::memcpy(result.body.data(),&reply,sizeof(reply));
    };

    auto targetConnectionId = m_sessions.connectionForUser(request.targetId);

    if(!targetConnectionId){
        setReplyResult(ADDFRI_RESULT_OFFLINE);
        return true;
    }

    removeExpiredFriendApplications();

    FriendApplicationKey key{request.applicantId,request.targetId};

    FriendApplicationKey reverseKey{request.targetId,request.applicantId};

    //同向重复申请和双方交叉申请，都先等待现有申请处理
    if(m_friendApplications.count(key) != 0 || m_friendApplications.count(reverseKey) != 0){
        setReplyResult(ADDFRI_RESULT_PENDING);
        return true;
    }

    std::size_t applicantPendingCount = 0;

    for(const auto& entry : m_friendApplications){
        if(entry.first.first == request.applicantId){
            ++applicantPendingCount;
        }
    }

    if(applicantPendingCount >= 16 || m_friendApplications.size() >= 1024){
        setReplyResult(ADDFRI_RESULT_BUSY);
        return true;
    }

    FriendApplication application{
        result.connectionId,
            *targetConnectionId,
            request.body,
            std::chrono::steady_clock::now() + std::chrono::minutes(5)
    };

    //先保存记录，再转发。目标回复时才能查到原申请
    m_friendApplications.emplace(key,std::move(application));

    bool forwarded = sendNotification(UserNotification{request.targetId,request.body});

    if(!forwarded){
        //目标连接清理时也可能已经删除记录，所以按key删除，不使用旧迭代器
        m_friendApplications.erase(key);

        setReplyResult(ADDFRI_RESULT_OFFLINE);
        return true;
    }

    printf("Friend request forwarded: applicant=%d, target=%d\n",static_cast<int>(request.applicantId),static_cast<int>(request.targetId));

    //已转发，等待目标回复，不立即向申请者发送成功消息
    result.body.clear();

    return true;
}

//好友申请回复处理函数
bool EpollServer::handleFriendReply(Connection& connection,const std::vector<char>& body){
    if(body.size() != sizeof(PROT_ADD_FRIEND_RS)){
        fprintf(stderr,"Invalid friend reply size\n");
        return false;
    }

    const int currentFd = connection.fd();
    const std::uint64_t currentConnectionId = connection.id();

    auto responderId = m_sessions.userForConnection(currentConnectionId);

    if(!responderId){
        fprintf(stderr,"Friend reply requires login\n");
        return false;
    }

    PROT_ADD_FRIEND_RS incoming{};

    std::memcpy(&incoming,body.data(),sizeof(incoming));

    if(incoming.myid != *responderId || incoming.destid <= 0 || incoming.destid == *responderId){
        fprintf(stderr,"Friend reply identity mismatch\n");
        return false;
    }

    if(incoming.result != ADDFRI_RESULT_ACCEPT && incoming.result != ADDFRI_RESULT_REJECT){
        fprintf(stderr,"Invalid friend decision\n");
        return false;
    }

    removeExpiredFriendApplications();

    //destid 是原来的申请者，myid 是当前回复者
    FriendApplicationKey key{incoming.destid,*responderId};

    auto application = m_friendApplications.find(key);

    if(application == m_friendApplications.end()){
        fprintf(stderr,"Friend reply has no valid application\n");
        return false;
    }

    auto applicantConnectionId = m_sessions.connectionForUser(incoming.destid);

    if(application->second.targetConnectionId != currentConnectionId || !applicantConnectionId || application->second.applicantConnectionId != *applicantConnectionId){
        fprintf(stderr,"Friend application session mismatch\n");
        return false;
    }

    if(application->second.requestBody.size() != sizeof(PROT_ADD_FRIEND_RQ)){
        fprintf(stderr,"Invalid stored friend application\n");
        return false;
    }

    PROT_ADD_FRIEND_RQ original{};

    std::memcpy(&original,application->second.requestBody.data(),sizeof(original));

    //同意后的数据库处理
    if(incoming.result == ADDFRI_RESULT_ACCEPT){
        PROT_ADD_FRIEND_RS accepted{};
        accepted.result = ADDFRI_RESULT_ACCEPT;

        accepted.destid = original.myid;
        accepted.myid = *responderId;

        std::memcpy(accepted.destnick,original.mynick,sizeof(accepted.destnick));
        std::memcpy(accepted.mynick,original.frinick,sizeof(accepted.mynick));

        std::vector<char> acceptedBody(sizeof(accepted));

        std::memcpy(acceptedBody.data(),&accepted,sizeof(accepted));

        //使用当前连接的业务队列，保持任务顺序
        if(!connection.enqueMessage(std::move(acceptedBody))){
            fprintf(stderr,"Friend acceptance queue is full\n");
            return false;
        }

        //决定已经接收，同一条申请不能再次处理
        m_friendApplications.erase(application);

        return startNextMessage(connection);
    }

    //使用服务端保存的申请内容构造回复，不转发客户端自行填写的昵称
    PROT_ADD_FRIEND_RS reply{};
    reply.result = ADDFRI_RESULT_REJECT;

    reply.destid = original.myid;
    reply.myid = *responderId;

    std::memcpy(reply.destnick,original.mynick,sizeof(reply.destnick));
    std::memcpy(reply.mynick,original.frinick,sizeof(reply.mynick));

    std::vector<char> replyBody(sizeof(reply));

    std::memcpy(replyBody.data(),&reply,sizeof(reply));

    //一条申请只处理一次
    m_friendApplications.erase(application);

    bool forwarded = sendNotification(UserNotification{incoming.destid,std::move(replyBody)});

    printf("Friend request rejected: applicant=%d, responder=%d, queued=%d\n",static_cast<int>(incoming.destid),static_cast<int>(*responderId),forwarded? 1:0);

    //通知过程中可能触发其他连接的清理，所以不再使用之前的connection引用
    auto current = m_connections.find(currentFd);

    return current != m_connections.end() && current->second.id() == currentConnectionId;
}

//好友缓存更新
void EpollServer::applyEstablishedFriendship(const EstablishedFriendship& friendship){
    auto addToCache = [this](std::int32_t ownerId,std::int32_t friendId){
        auto connectionId = m_sessions.connectionForUser(ownerId);

        if(!connectionId){
            return;
        }

        auto& friends = m_friendsByConnection[*connectionId];

        if(std::find(friends.begin(),friends.end(),friendId) == friends.end()){
            friends.push_back(friendId);
        }
    };

    addToCache(friendship.applicantId,friendship.responderId);
    addToCache(friendship.responderId,friendship.applicantId);
}

//在线状态填写函数
void EpollServer::refreshUserInfoStatus(std::vector<char>& body){
    if(body.size() != sizeof(PROT_FRIEND_INFO)){
        return;
    }

    PROT_FRIEND_INFO info{};

    std::memcpy(&info,body.data(),sizeof(info));

    if(info.prottype != DEF_PROT_FRIEND_INFO){
        return;
    }

    auto connectionId = m_sessions.connectionForUser(info.friendid);

    info.status = connectionId ? FRIEND_STATUS_ONLINE:FRIEND_STATUS_OFFLINE;

    std::memcpy(body.data(),&info,sizeof(info));
}

// 删除过期或参与者连接已经失效的邀请
void EpollServer::removeInvalidGroupInvitations() {
    const auto now = std::chrono::steady_clock::now();

    for (auto it = m_groupInvitations.begin();it != m_groupInvitations.end();) {
        const auto& invitation = it->second;

        auto inviterConnection = m_sessions.connectionForUser(invitation.inviterId);

        auto targetConnection = m_sessions.connectionForUser(invitation.targetUserId);

        const bool expired = now >= invitation.expiresAt;
        const bool inviterDisconnected = !inviterConnection || *inviterConnection != invitation.inviterConnectionId;
        const bool targetDisconnected = !targetConnection || *targetConnection != invitation.targetConnectionId;

        if (expired || inviterDisconnected || targetDisconnected) {
            it = m_groupInvitations.erase(it);
        }
        else {
            ++it;
        }
    }
}

bool EpollServer::processGroupInvitationResult(WorkResult& result) {
    if (!result.groupInvitation) {
        return true;
    }

    const auto& pending = *result.groupInvitation;

    // 再次核对当前连接的登录身份。
    auto inviterId = m_sessions.userForConnection(result.connectionId);

    if (!inviterId || *inviterId != pending.inviterId || pending.requestId == 0 || pending.groupId <= 0 || pending.targetUserId <= 0 || pending.targetUserId == pending.inviterId) {
        return false;
    }

    if (pending.notification.prottype != DEF_PROT_GROUP_INVITE_NOTIFY || pending.notification.groupId != pending.groupId || pending.notification.inviterId != pending.inviterId) {
        return false;
    }

    // 先准备回复，后面根据处理情况填写结果。
    PROT_GROUP_INVITE_RS reply{};
    reply.requestId = pending.requestId;
    reply.groupId = pending.groupId;
    reply.targetUserId = pending.targetUserId;
    reply.result = GROUP_INVITE_FAIL;

    auto setReply = [&](std::int32_t code,std::uint32_t invitationId) {
        reply.result = code;
        reply.invitationId = invitationId;

        result.body.resize(sizeof(reply));

        std::memcpy(result.body.data(),&reply,sizeof(reply));
    };

    removeInvalidGroupInvitations();

    auto targetConnectionId =m_sessions.connectionForUser(pending.targetUserId);

    if (!targetConnectionId) {
        setReply(GROUP_INVITE_OFFLINE, 0);
        return true;
    }

    // 同一个群、同一个目标，只保留一条有效邀请。
    std::size_t inviterPendingCount = 0;

    for (const auto& entry : m_groupInvitations) {
        const auto& invitation = entry.second;

        if (invitation.groupId == pending.groupId && invitation.targetUserId == pending.targetUserId) {
            setReply(GROUP_INVITE_PENDING, 0);
            return true;
        }

        if (invitation.inviterId == pending.inviterId) {
            ++inviterPendingCount;
        }
    }


    // 限制内存中的待确认邀请数量。
    if (inviterPendingCount >= 16 || m_groupInvitations.size() >= 1024) {
        setReply(GROUP_INVITE_FAIL, 0);
        return true;
    }

    // 分配非零、当前未被占用的邀请编号。
    std::uint32_t invitationId = 0;

    do {
        invitationId = m_nextGroupInvitationId++;
    } 
    while (invitationId == 0 || m_groupInvitations.count(invitationId) != 0);

    PROT_GROUP_INVITE_NOTIFY notification = pending.notification;

    notification.invitationId = invitationId;

    GroupInvitationRecord record{};
    record.inviterConnectionId = result.connectionId;
    record.targetConnectionId = *targetConnectionId;
    record.inviterId = pending.inviterId;
    record.targetUserId = pending.targetUserId;
    record.groupId = pending.groupId;
    record.notification = notification;
    record.expiresAt = std::chrono::steady_clock::now() + std::chrono::minutes(5);

    UserNotification outgoing{};
    outgoing.targetUserId = pending.targetUserId;
    outgoing.body.resize(sizeof(notification));

    std::memcpy(outgoing.body.data(),&notification,sizeof(notification));

    // 先保存，确保目标回复时能找到邀请。
    m_groupInvitations.emplace(invitationId,std::move(record));

    if (!sendNotification(outgoing)) {
        // 发送失败可能触发连接清理，按编号删除，
        // 不保留并使用发送前的容器迭代器。
        m_groupInvitations.erase(invitationId);

        setReply(GROUP_INVITE_OFFLINE, 0);
        return true;
    }

    setReply(GROUP_INVITE_SUCC, invitationId);

    printf("Group invitation forwarded: invitation=%u, group=%d, inviter=%d, target=%d\n",static_cast<unsigned int>(invitationId),static_cast<int>(pending.groupId),static_cast<int>(pending.inviterId),static_cast<int>(pending.targetUserId));

    return true;
}

bool EpollServer::handleGroupInviteDecision(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_GROUP_INVITE_DECISION_RQ)) {
        fprintf(stderr, "Invalid group decision size\n");
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();

    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        fprintf(stderr, "Group decision requires login\n");
        return false;
    }

    PROT_GROUP_INVITE_DECISION_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.invitationId == 0 || (request.decision != GROUP_INVITE_DECISION_ACCEPT && request.decision != GROUP_INVITE_DECISION_REJECT)) {
        fprintf(stderr, "Invalid group decision fields\n");
        return false;
    }

    PROT_GROUP_INVITE_DECISION_RS reply{};
    reply.requestId = request.requestId;
    reply.invitationId = request.invitationId;

    auto sendReply = [&](std::int32_t code) {
        reply.result = code;

        std::vector<char> replyBody(sizeof(reply));

        std::memcpy(replyBody.data(),&reply,sizeof(reply));

        return queueReply(clientFd, replyBody);
    };

    auto it = m_groupInvitations.find(request.invitationId);

    if (it == m_groupInvitations.end()) {
        // 找不到记录时，groupId 保持为 0。
        return sendReply(GROUP_INVITE_EXPIRED);
    }

    // 先核对目标身份，不能处理别人的邀请。
    if (it->second.targetUserId != *userId || it->second.targetConnectionId != connectionId) {
        fprintf(stderr, "Group decision identity mismatch\n");
        return false;
    }

    // 使用独立副本，后续删除记录不会影响这些数据。
    const GroupInvitationRecord invitation = it->second;
    reply.groupId = invitation.groupId;

    auto inviterConnection = m_sessions.connectionForUser(invitation.inviterId);

    const bool expired = std::chrono::steady_clock::now() >= invitation.expiresAt;
    const bool inviterDisconnected = !inviterConnection || *inviterConnection != invitation.inviterConnectionId;

    if (expired || inviterDisconnected) {
        m_groupInvitations.erase(it);
        return sendReply(GROUP_INVITE_EXPIRED);
    }

    if (request.decision == GROUP_INVITE_DECISION_ACCEPT) {
        GroupInvitationAcceptanceTask task{};
        task.requestId = request.requestId;
        task.invitationId = request.invitationId;

        // 使用经过身份、连接和有效期校验的邀请记录。
        task.groupId = invitation.groupId;
        task.inviterId = invitation.inviterId;
        task.targetUserId = invitation.targetUserId;

        std::vector<char> taskBody(sizeof(task));

        std::memcpy(taskBody.data(),&task,sizeof(task));

        // 使用当前连接的业务队列，保持业务处理顺序。
        if (!connection.enqueMessage(std::move(taskBody))) {
            fprintf(stderr,"Group acceptance queue is full\n");
            return false;
        }

        // 已成功接收决定，同一条邀请不能再次处理。
        m_groupInvitations.erase(it);

        printf("Group acceptance queued: invitation=%u, group=%d, target=%d\n",static_cast<unsigned int>(request.invitationId),static_cast<int>(invitation.groupId),static_cast<int>(invitation.targetUserId));

        return startNextMessage(connection);
    }

    // 拒绝已经生效，删除记录，不能重复处理。
    m_groupInvitations.erase(it);

    if (!sendReply(GROUP_INVITE_REJECTED)) {
        return false;
    }


    // 告知群主：目标拒绝了邀请。
    PROT_GROUP_INVITE_RESULT_NOTIFY notification{};
    notification.invitationId = request.invitationId;
    notification.groupId = invitation.groupId;
    notification.targetUserId = invitation.targetUserId;
    notification.result = GROUP_INVITE_REJECTED;

    std::vector<char> notificationBody(sizeof(notification));

    std::memcpy(notificationBody.data(),&notification,sizeof(notification));

    sendNotification(
            UserNotification{
            invitation.inviterId,
            std::move(notificationBody)
            });

    printf("Group invitation rejected: invitation=%u, group=%d, target=%d\n",static_cast<unsigned int>(request.invitationId),static_cast<int>(invitation.groupId),static_cast<int>(invitation.targetUserId));

    // 通知发送可能触发连接清理，重新确认当前连接。
    auto current = m_connections.find(clientFd);

    return current != m_connections.end() && current->second.id() == connectionId;
}

bool EpollServer::processGroupChatResult(WorkResult& result) {
    if (!result.groupChat) {
        return true;
    }

    const auto& pending = *result.groupChat;

    auto senderId = m_sessions.userForConnection(
            result.connectionId
            );

    if (!senderId || *senderId != pending.senderId || pending.requestId == 0 || pending.groupId <= 0) {
        fprintf(stderr, "Invalid group chat sender\n");
        return false;
    }

    if (result.body.size() != sizeof(PROT_GROUP_CHAT_RS) || pending.body.size() != sizeof(PROT_GROUP_CHAT_NOTIFY)) {
        fprintf(stderr, "Invalid group chat result size\n");
        return false;
    }

    PROT_GROUP_CHAT_RS reply{};
    std::memcpy(&reply,result.body.data(),sizeof(reply));

    PROT_GROUP_CHAT_NOTIFY notification{};
    std::memcpy(&notification,pending.body.data(),sizeof(notification));

    if (reply.prottype != DEF_PROT_GROUP_CHAT_RS ||
            reply.requestId != pending.requestId ||
            reply.groupId != pending.groupId ||
            notification.prottype != DEF_PROT_GROUP_CHAT_NOTIFY ||
            notification.requestId != pending.requestId ||
            notification.groupId != pending.groupId ||
            notification.senderId != pending.senderId) {
        fprintf(stderr, "Group chat result mismatch\n");
        return false;
    }

    if (pending.recipients.size() >= MAX_GROUP_MEMBERS) {
        fprintf(stderr, "Too many group chat recipients\n");
        return false;
    }


    // 转发前验证目标列表，避免异常数据造成重复转发
    for (std::size_t i = 0;i < pending.recipients.size(); ++i) {
        const auto targetId = pending.recipients[i];

        if (targetId <= 0 || targetId == pending.senderId) {
            return false;
        }

        for (std::size_t j = 0; j < i; ++j) {
            if (pending.recipients[j] == targetId) {
                return false;
            }
        }
    }

    std::int32_t forwardedCount = 0;

    for (std::int32_t targetId : pending.recipients) {
        // 发送过程中可能触发连接清理
        // 如果发送者已经断开，就停止剩余转发
        auto currentSender = m_sessions.userForConnection(
                result.connectionId
                );

        if (!currentSender || *currentSender != pending.senderId) {
            break;
        }

        // 离线成员直接跳过，不作为整条消息失败
        if (!m_sessions.connectionForUser(targetId)) {
            continue;
        }

        if (sendNotification(UserNotification{targetId,pending.body})) {
            ++forwardedCount;
        }
    }

    reply.result = GROUP_CHAT_SUCC;
    reply.forwardedCount = forwardedCount;

    std::memcpy(result.body.data(),&reply,sizeof(reply));

    printf("Group message forwarded: group=%d, sender=%d, count=%d\n",static_cast<int>(pending.groupId),static_cast<int>(pending.senderId),static_cast<int>(forwardedCount));

    return true;
}

bool EpollServer::processFileOfferResult(WorkResult& result) {
    if (!result.fileOffer) {
        return true;
    }

    const auto& pending = *result.fileOffer;

    auto senderId = m_sessions.userForConnection(result.connectionId);

    if (!senderId || *senderId != pending.senderId || pending.requestId == 0 || pending.targetUserId <= 0 || pending.targetUserId == pending.senderId || 
            pending.notification.senderId != pending.senderId ||
            pending.notification.prottype != DEF_PROT_FILE_OFFER_NOTIFY ||
            pending.notification.fileSize > MAX_TRANSFER_FILE_BYTES ||
            result.body.size() != sizeof(PROT_FILE_OFFER_RS)) {
        fprintf(stderr, "Invalid pending file offer\n");
        return false;
    }

    PROT_FILE_OFFER_RS reply{};
    reply.requestId = pending.requestId;

    auto setReply = [&](std::int32_t code,std::uint32_t transferId) {
        reply.result = code;
        reply.transferId = transferId;

        std::memcpy(result.body.data(),&reply,sizeof(reply));
    };

    auto targetConnectionId = m_sessions.connectionForUser(pending.targetUserId);

    if (!targetConnectionId) {
        setReply(FILE_RESULT_OFFLINE, 0);
        return true;
    }

    removeInvalidFileTransfers();

    // 清理时可能触发来源连接关闭，重新核对身份
    auto currentSender = m_sessions.userForConnection(result.connectionId);

    if (!currentSender || *currentSender != pending.senderId) {
        return false;
    }

    // 一个用户作为发送者或接收者，总共最多参与 4 个任务
    std::size_t senderTaskCount = 0;
    std::size_t targetTaskCount = 0;

    for (const auto& entry : m_fileTransfers) {
        const auto& task = entry.second;

        if (task.senderId == pending.senderId || task.targetUserId == pending.senderId) {
            ++senderTaskCount;
        }

        if (task.senderId == pending.targetUserId || task.targetUserId == pending.targetUserId) {
            ++targetTaskCount;
        }
    }

    if (senderTaskCount >= 4 || targetTaskCount >= 4 || m_fileTransfers.size() >= 128) {
        setReply(FILE_RESULT_BUSY, 0);
        return true;
    }

    std::uint32_t transferId = 0;

    do {
        transferId = m_nextFileTransferId++;
    } while (transferId == 0 || m_fileTransfers.count(transferId) != 0);

    PROT_FILE_OFFER_NOTIFY notification = pending.notification;

    notification.transferId = transferId;

    FileTransferRecord record{};
    record.senderConnectionId = result.connectionId;
    record.targetConnectionId = *targetConnectionId;
    record.senderId = pending.senderId;
    record.targetUserId = pending.targetUserId;
    record.notification = notification;
    record.expiresAt = std::chrono::steady_clock::now() + std::chrono::minutes(5);

    std::vector<char> notificationBody(sizeof(notification));

    std::memcpy(notificationBody.data(),&notification,sizeof(notification));

    // 先保存任务，接收者回复时才能查到它
    m_fileTransfers.emplace(transferId,std::move(record));

    if (!sendNotification(UserNotification{pending.targetUserId,std::move(notificationBody)})) {
        // 发送可能触发连接清理，按编号删除
        m_fileTransfers.erase(transferId);
        setReply(FILE_RESULT_OFFLINE, 0);
        return true;
    }

    setReply(FILE_RESULT_SUCC, transferId);

    printf("File offer forwarded: transfer=%u, sender=%d, target=%d\n",static_cast<unsigned int>(transferId),static_cast<int>(pending.senderId),static_cast<int>(pending.targetUserId));

    return true;
}

void EpollServer::finishFileTransfer(std::uint32_t transferId,std::int32_t result) {
    auto it = m_fileTransfers.find(transferId);

    if (it == m_fileTransfers.end()) {
        return;
    }

    const FileTransferRecord record = it->second;

    // 先删除，再发送通知。
    // 通知发送失败可能触发断线清理，避免重复结束任务
    m_fileTransfers.erase(it);

    auto notifyPeer = [&](std::int32_t userId,std::uint64_t expectedConnectionId,std::int32_t peerUserId) {
        auto connectionId = m_sessions.connectionForUser(userId);

        // 不把旧任务的通知发给重新登录的新连接
        if (!connectionId || *connectionId != expectedConnectionId) {
            return;
        }

        PROT_FILE_END_NOTIFY notification{};
        notification.transferId = transferId;
        notification.result = result;
        notification.peerUserId = peerUserId;

        std::vector<char> body(sizeof(notification));
        std::memcpy(body.data(),&notification,sizeof(notification));

        sendNotification(UserNotification{userId,std::move(body)});
    };

    notifyPeer(record.senderId,record.senderConnectionId,record.targetUserId);
    notifyPeer(record.targetUserId,record.targetConnectionId,record.senderId);

    printf("File task ended: transfer=%u, result=%d\n",static_cast<unsigned int>(transferId),static_cast<int>(result));
}

void EpollServer::removeInvalidFileTransfers() {
    const auto now = std::chrono::steady_clock::now();

    // 扫描时只记录编号，扫描结束后再删除和通知
    std::vector<std::pair<std::uint32_t, std::int32_t>> invalidTasks;

    for (const auto& entry : m_fileTransfers) {
        const auto& record = entry.second;

        auto senderConnection = m_sessions.connectionForUser(record.senderId);
        auto targetConnection = m_sessions.connectionForUser(record.targetUserId);

        const bool disconnected = !senderConnection || *senderConnection != record.senderConnectionId || !targetConnection ||*targetConnection != record.targetConnectionId;

        if (disconnected) {
            invalidTasks.emplace_back(entry.first,FILE_RESULT_OFFLINE);
        } 
        else if (now >= record.expiresAt) {
            invalidTasks.emplace_back(entry.first,FILE_RESULT_EXPIRED);
        }
    }

    for (const auto& task : invalidTasks) {
        finishFileTransfer(task.first, task.second);
    }
}

bool EpollServer::handleFileDecision(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_FILE_DECISION_RQ)) {
        fprintf(stderr, "Invalid file decision size\n");
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();

    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        fprintf(stderr, "File decision requires login\n");
        return false;
    }

    PROT_FILE_DECISION_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.requestId == 0 || request.transferId == 0 ||
            (request.decision != FILE_DECISION_ACCEPT && request.decision != FILE_DECISION_REJECT)) {
        fprintf(stderr, "Invalid file decision fields\n");
        return false;
    }

    PROT_FILE_DECISION_RS reply{};
    reply.requestId = request.requestId;
    reply.transferId = request.transferId;

    auto sendReply = [&](std::int32_t code) {
        reply.result = code;

        std::vector<char> replyBody(sizeof(reply));
        std::memcpy(replyBody.data(),&reply,sizeof(reply));

        return queueReply(clientFd, replyBody);
    };

    auto it = m_fileTransfers.find(request.transferId);

    if (it == m_fileTransfers.end()) {
        return sendReply(FILE_RESULT_EXPIRED);
    }

    if (it->second.targetUserId != *userId || it->second.targetConnectionId != connectionId) {
        fprintf(stderr, "File decision identity mismatch\n");
        return false;
    }

    if (it->second.state != FileTransferState::WaitingForDecision) {
        fprintf(stderr, "Unexpected file decision state\n");
        return false;
    }

    const FileTransferRecord record = it->second;

    // 先排队决定回复，再结束任务并通知双方
    auto endTask = [&](std::int32_t code) {
        const bool queued = sendReply(code);

        finishFileTransfer(request.transferId, code);

        // 发送结束通知可能触发连接清理
        auto current = m_connections.find(clientFd);

        return queued && current != m_connections.end() && current->second.id() == connectionId;
    };

    auto senderConnection = m_sessions.connectionForUser(record.senderId);

    if (!senderConnection || *senderConnection != record.senderConnectionId) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    if (std::chrono::steady_clock::now() >= record.expiresAt) {
        return endTask(FILE_RESULT_EXPIRED);
    }

    if (request.decision == FILE_DECISION_REJECT) {
        printf("File offer rejected: transfer=%u, target=%d\n",static_cast<unsigned int>(request.transferId),static_cast<int>(*userId));

        return endTask(FILE_RESULT_REJECTED);
    }

    // 临时分支：后续替换为接收文件准备和传输启动流程
    // 当前不发送文件数据，也不把接受误报为传输成功
    //return endTask(FILE_RESULT_FAIL);

    PROT_FILE_START_NOTIFY start{};
    start.transferId = request.transferId;
    start.targetUserId = record.targetUserId;

    std::vector<char> startBody(sizeof(start));
    std::memcpy(startBody.data(),&start,sizeof(start));

    // 当前仍持有有效的任务迭代器
    // 设置传输状态后，不再接受重复的接收决定
    it->second.state = FileTransferState::Transferring;
    it->second.confirmedBytes = 0;
    it->second.pendingOffset = 0;
    it->second.pendingLength = 0;
    it->second.waitingForChunkAck = false;

    // 进入传输阶段，先给发送方 30 秒准备第一块
    // 后面每次有效的数据交互都会刷新这个时间
    it->second.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    // 先确认接收方的决定
    if (!sendReply(FILE_RESULT_SUCC)) {
        finishFileTransfer(request.transferId,FILE_RESULT_FAIL);
        return false;
    }

    // 回复发送可能引发连接清理，重新核对发送方连接
    auto currentSender = m_sessions.connectionForUser(record.senderId);

    if (!currentSender || *currentSender != record.senderConnectionId) {
        finishFileTransfer(request.transferId,FILE_RESULT_OFFLINE);
    } 
    else if (!sendNotification(UserNotification{record.senderId,std::move(startBody)})) {
        finishFileTransfer(request.transferId,FILE_RESULT_OFFLINE);
    } 
    else {
        printf("File transfer started: transfer=%u, sender=%d, target=%d\n",static_cast<unsigned int>(request.transferId),static_cast<int>(record.senderId),static_cast<int>(record.targetUserId));
    }

    // 此后不再使用旧的 connection 引用或任务迭代器
    auto current = m_connections.find(clientFd);
    return current != m_connections.end() && current->second.id() == connectionId;
}

bool EpollServer::handleFileChunk(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_FILE_CHUNK_RQ)) {
        fprintf(stderr, "Invalid file chunk size\n");
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();

    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        fprintf(stderr, "File chunk requires login\n");
        return false;
    }

    PROT_FILE_CHUNK_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.transferId == 0) {
        return false;
    }

    auto it = m_fileTransfers.find(request.transferId);

    if (it == m_fileTransfers.end()) {
        // 任务可能刚刚超时或被取消，忽略迟到的数据
        fprintf(stderr, "Discard chunk for ended file task\n");
        return true;
    }

    if (it->second.senderId != *userId || it->second.senderConnectionId != connectionId) {
        fprintf(stderr, "File chunk sender mismatch\n");
        return false;
    }

    // 结束任务、发送通知后，重新确认来源连接
    auto endTask = [&](std::int32_t code) {
        finishFileTransfer(request.transferId, code);
        auto current = m_connections.find(clientFd);
        return current != m_connections.end() && current->second.id() == connectionId;
    };

    auto& transfer = it->second;

    if (std::chrono::steady_clock::now() >= transfer.expiresAt) {
        return endTask(FILE_RESULT_EXPIRED);
    }

    if (transfer.state != FileTransferState::Transferring || transfer.waitingForChunkAck) {
        return endTask(FILE_RESULT_INVALID);
    }

    const std::uint64_t fileSize = transfer.notification.fileSize;

    // 先检查进度，再做减法，避免无符号整数下溢
    if (transfer.confirmedBytes > fileSize || request.offset != transfer.confirmedBytes ||
            request.dataLength == 0 || request.dataLength > FILE_CHUNK_BYTES) {
        return endTask(FILE_RESULT_INVALID);
    }

    const std::uint64_t remaining = fileSize - transfer.confirmedBytes;

    if (request.dataLength > remaining) {
        return endTask(FILE_RESULT_INVALID);
    }

    auto targetConnection = m_sessions.connectionForUser(transfer.targetUserId);

    if (!targetConnection || *targetConnection != transfer.targetConnectionId) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    PROT_FILE_CHUNK_NOTIFY notification{};
    notification.transferId = request.transferId;
    notification.offset = request.offset;
    notification.dataLength = request.dataLength;

    std::memcpy(notification.data,request.data,request.dataLength);
    const std::int32_t targetUserId = transfer.targetUserId;
    std::vector<char> notificationBody(sizeof(notification));
    std::memcpy(notificationBody.data(),&notification,sizeof(notification));

    // 先记录本块，接收方确认时必须完全匹配
    transfer.pendingOffset = request.offset;
    transfer.pendingLength = request.dataLength;
    transfer.waitingForChunkAck = true;

    transfer.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    if (!sendNotification(UserNotification{targetUserId,std::move(notificationBody)})) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    // 不提前增加 confirmedBytes，也不提前回复发送方成功
    // 等接收方写入确认后，再通知发送方继续
    auto current = m_connections.find(clientFd);
    return current != m_connections.end() && current->second.id() == connectionId;
}

bool EpollServer::handleFileChunkAck(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_FILE_CHUNK_ACK)) {
        fprintf(stderr, "Invalid file chunk ack size\n");
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();

    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        return false;
    }

    PROT_FILE_CHUNK_ACK ack{};
    std::memcpy(&ack, body.data(), sizeof(ack));

    if (ack.transferId == 0) {
        return false;
    }

    auto it = m_fileTransfers.find(ack.transferId);

    if (it == m_fileTransfers.end()) {
        // 忽略已经结束任务的迟到确认
        return true;
    }

    if (it->second.targetUserId != *userId || it->second.targetConnectionId != connectionId) {
        fprintf(stderr, "File chunk ack identity mismatch\n");
        return false;
    }

    auto endTask = [&](std::int32_t code) {
        finishFileTransfer(ack.transferId, code);
        auto current = m_connections.find(clientFd);
        return current != m_connections.end() && current->second.id() == connectionId;
    };

    auto& transfer = it->second;

    if (std::chrono::steady_clock::now() >= transfer.expiresAt) {
        return endTask(FILE_RESULT_EXPIRED);
    }

    if (transfer.state != FileTransferState::Transferring || !transfer.waitingForChunkAck ||
            ack.offset != transfer.pendingOffset ||
            ack.dataLength != transfer.pendingLength ||
            ack.dataLength == 0) {
        return endTask(FILE_RESULT_INVALID);
    }

    if (ack.result != FILE_RESULT_SUCC) {
        // 接收方写入失败，不能允许发送方继续发送
        return endTask(FILE_RESULT_FAIL);
    }

    const std::uint64_t fileSize = transfer.notification.fileSize;

    if (transfer.confirmedBytes > fileSize ||
            ack.offset != transfer.confirmedBytes ||
            ack.dataLength > fileSize - transfer.confirmedBytes) {
        return endTask(FILE_RESULT_INVALID);
    }

    auto senderConnection = m_sessions.connectionForUser(transfer.senderId);

    if (!senderConnection || *senderConnection != transfer.senderConnectionId) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    PROT_FILE_CHUNK_RS reply{};
    reply.transferId = ack.transferId;
    reply.offset = ack.offset;
    reply.dataLength = ack.dataLength;
    reply.result = FILE_RESULT_SUCC;

    std::vector<char> replyBody(sizeof(reply));
    std::memcpy(replyBody.data(),&reply,sizeof(reply));
    const std::int32_t senderId = transfer.senderId;

    // 接收方已确认，才推进累计进度
    transfer.confirmedBytes += ack.dataLength;
    transfer.pendingOffset = 0;
    transfer.pendingLength = 0;
    transfer.waitingForChunkAck = false;
    transfer.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    if (!sendNotification(UserNotification{senderId,std::move(replyBody)})) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    // 通知发送可能触发连接清理，不使用旧引用
    auto current = m_connections.find(clientFd);
    return current != m_connections.end() && current->second.id() == connectionId;
}

bool EpollServer::handleFileFinishRequest(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_FILE_FINISH_RQ)) {
        fprintf(stderr, "Invalid file finish request size\n");
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();

    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        return false;
    }

    PROT_FILE_FINISH_RQ request{};
    std::memcpy(&request, body.data(), sizeof(request));

    if (request.transferId == 0) {
        return false;
    }

    auto it = m_fileTransfers.find(request.transferId);

    if (it == m_fileTransfers.end()) {
        // 任务可能已经超时结束，忽略迟到请求
        return true;
    }

    if (it->second.senderId != *userId || it->second.senderConnectionId != connectionId) {
        fprintf(stderr, "File finish sender mismatch\n");
        return false;
    }

    auto endTask = [&](std::int32_t code) {
        finishFileTransfer(request.transferId, code);
        auto current = m_connections.find(clientFd);
        return current != m_connections.end() && current->second.id() == connectionId;
    };

    auto& transfer = it->second;

    if (std::chrono::steady_clock::now() >= transfer.expiresAt) {
        return endTask(FILE_RESULT_EXPIRED);
    }

    // 必须没有尚未确认的数据块，且确认字节数与申请中的文件大小完全一致
    if (transfer.state != FileTransferState::Transferring ||
            transfer.waitingForChunkAck ||
            transfer.confirmedBytes != transfer.notification.fileSize) {
        return endTask(FILE_RESULT_INVALID);
    }

    auto targetConnection = m_sessions.connectionForUser(transfer.targetUserId);

    if (!targetConnection || *targetConnection != transfer.targetConnectionId) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    PROT_FILE_FINISH_NOTIFY notification{};
    notification.transferId = request.transferId;
    std::vector<char> notificationBody(sizeof(notification));
    std::memcpy(notificationBody.data(),&notification,sizeof(notification));

    const std::int32_t targetUserId = transfer.targetUserId;

    // 先切换状态，再通知接收方
    transfer.state = FileTransferState::WaitingForCompletion;
    transfer.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    if (!sendNotification(UserNotification{targetUserId,std::move(notificationBody)})) {
        return endTask(FILE_RESULT_OFFLINE);
    }

    printf("File verification requested: transfer=%u\n",static_cast<unsigned int>(request.transferId));

    auto current = m_connections.find(clientFd);
    return current != m_connections.end() && current->second.id() == connectionId;
}

bool EpollServer::handleFileFinishResult(Connection& connection,const std::vector<char>& body) {
    if (body.size() != sizeof(PROT_FILE_FINISH_RS)) {
        return false;
    }

    const int clientFd = connection.fd();
    const std::uint64_t connectionId = connection.id();
    auto userId = m_sessions.userForConnection(connectionId);

    if (!userId) {
        return false;
    }

    PROT_FILE_FINISH_RS reply{};
    std::memcpy(&reply, body.data(), sizeof(reply));

    if (reply.transferId == 0) {
        return false;
    }

    auto it = m_fileTransfers.find(reply.transferId);

    if (it == m_fileTransfers.end()) {
        return true;
    }

    const auto& transfer = it->second;

    if (transfer.targetUserId != *userId || transfer.targetConnectionId != connectionId) {
        fprintf(stderr, "File finish result identity mismatch\n");
        return false;
    }

    std::int32_t result = reply.result;

    if (transfer.state != FileTransferState::WaitingForCompletion ||
            transfer.waitingForChunkAck ||
            transfer.confirmedBytes != transfer.notification.fileSize) {
        result = FILE_RESULT_INVALID;
    } 
    else if (std::chrono::steady_clock::now() >= transfer.expiresAt) {
        result = FILE_RESULT_EXPIRED;
    } 
    else {
        switch (reply.result) {
            case FILE_RESULT_SUCC:
            case FILE_RESULT_FAIL:
            case FILE_RESULT_INVALID:
            case FILE_RESULT_HASH_MISMATCH:
                break;

            default:
                result = FILE_RESULT_INVALID;
                break;
        }
    }

    // 删除服务端任务，并通知仍属于原连接的双方
    finishFileTransfer(reply.transferId, result);

    auto current = m_connections.find(clientFd);
    return current != m_connections.end() &&current->second.id() == connectionId;
}

void EpollServer::applyRemovedFriendship(const RemovedFriendship& friendship) {
    const auto firstId = friendship.firstUserId;
    const auto secondId = friendship.secondUserId;

    auto removeFromCache = [this](std::int32_t ownerId,std::int32_t friendId) {
        auto connectionId = m_sessions.connectionForUser(ownerId);

        if (!connectionId) {
            return;
        }

        auto it = m_friendsByConnection.find(
                *connectionId
                );

        if (it == m_friendsByConnection.end()) {
            return;
        }

        auto& friends = it->second;

        friends.erase(std::remove(friends.begin(),friends.end(),friendId),friends.end());
    };

    removeFromCache(firstId, secondId);
    removeFromCache(secondId, firstId);

    // 清理双方之间尚未处理的好友申请
    m_friendApplications.erase(FriendApplicationKey{firstId, secondId});
    m_friendApplications.erase(FriendApplicationKey{secondId, firstId});

    // 先收集编号，再结束任务，避免通知发送改变容器
    std::vector<std::uint32_t> fileTasks;

    for (const auto& entry : m_fileTransfers) {
        const auto& task = entry.second;

        const bool samePair = (task.senderId == firstId && task.targetUserId == secondId) ||
            (task.senderId == secondId && task.targetUserId == firstId);

        if (samePair) {
            fileTasks.push_back(entry.first);
        }
    }

    for (std::uint32_t transferId : fileTasks) {
        finishFileTransfer(transferId,FILE_RESULT_NOT_FRIEND);
    }

    printf("Friendship removed: first=%d, second=%d\n",static_cast<int>(firstId),static_cast<int>(secondId));
}
