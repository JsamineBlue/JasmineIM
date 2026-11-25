#include "kernel.h"
#include"QDebug"
#include"net/def.h"
#include"loginwidget.h"
#include"QMessageBox"
#include"mediator/TCP_ClientMediator.h"
#include "net/group_protocol.h"
#include "net/file_protocol.h"
#include<algorithm>
#include<cstring>
#include <memory>
#include <QSet>
#include <QTimer>
#include <QFileInfo>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QFileDialog>
#include <QDir>
#include <QCoreApplication>
#include <QEvent>

#define SENDCLIENTDATA(PROT_TYPE,PROT_NAME)\
{int len = sizeof(PROT_TYPE);\
char* pbuf = new char[len]();\
memcpy_s(pbuf,len,&PROT_NAME,len);\
emit signals_sendClientData(pbuf,len,0);}

namespace {
    //QString → UTF-8，写入协议固定数组
    //太长就拒绝，不能直接截断 UTF-8 字节
    template<std::size_t N>
    bool writeUtf8Field(char(&destination)[N],const QString& text,const QString& fieldName){
        QByteArray bytes = text.toUtf8();

        if (bytes.contains('\0') || bytes.size() >= static_cast<int>(N)) {
            QMessageBox::warning(nullptr,"提示",QString("%1包含无效字符或超过长度限制，最多允许 %2 个 UTF-8 字节").arg(fieldName).arg(static_cast<int>(N - 1)));

            return false;
        }
        std::fill(destination, destination + N, '\0');

        if (!bytes.isEmpty()) {
            std::memcpy(destination,bytes.constData(),static_cast<std::size_t>(bytes.size()));
        }

        return true;
    }

    // 从固定数组解码，限定读取范围，避免越界查找结束符。
    template<std::size_t N>
    QString readUtf8Field(const char (&source)[N])
    {
        const char* end = std::find(source, source + N, '\0');

        return QString::fromUtf8(source,static_cast<int>(end - source));
    }
}


Kernel::Kernel(QObject *parent)
    : QObject{parent},m_pLogin(new LoginWidget) /*创建登录框*/,m_pMainWidget(new MainWidget)/*创建主聊天窗口*/
{
    qDebug()<<__func__;
    //----------绑定所有协议与处理函数-------------------------------------------------------------------------------------------------------
    m_handlers = {
        {
            DEF_PROT_REGISTER_RS,
            {
                sizeof(PROT_REGISTER_RS),
                &Kernel::deal_prot_register_rs
            }
        },
        {
            DEF_PPROT_LOGIN_RS,
            {
                sizeof(PROT_LOGIN_RS),
                &Kernel::deal_prot_login_rs
            }
        },
        {
            DEF_PROT_FRIEND_INFO,
            {
                sizeof(PROT_FRIEND_INFO),
                &Kernel::deal_prot_friend_info
            }
        },
        {
            DEF_PROT_CHAT_INFO_RQ,
            {
                sizeof(PROT_CHAT_INFO_RQ),
                &Kernel::deal_prot_chat_info_rq
            }
        },
        {
            DEF_PROT_CHAT_INFO_RS,
            {
                sizeof(PROT_CHAT_INFO_RS),
                &Kernel::deal_prot_chat_info_rs
            }
        },
        {
            DEF_PROT_ADD_FRIEND_RS,
            {
                sizeof(PROT_ADD_FRIEND_RS),
                &Kernel::deal_prot_add_friend_rs
            }
        },
        {
            DEF_PROT_ADD_FRIEND_RQ,
            {
                sizeof(PROT_ADD_FRIEND_RQ),
                &Kernel::deal_prot_add_friend_rq
            }
        },
        {
            DEF_PROT_FRIEND_OFFLINE,
            {
                sizeof(PROT_FRIEND_OFFLINE),
                &Kernel::deal_prot_friend_offline
            }
        }
    };

    m_handlers.emplace(
        DEF_PROT_GROUP_CREATE_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_CREATE_RS),
            &Kernel::deal_prot_group_create_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_INFO,
        ProtocolHandler{
            sizeof(PROT_GROUP_INFO),
            &Kernel::deal_prot_group_info
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_MEMBERS_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_MEMBERS_RS),
            &Kernel::deal_prot_group_members_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_INVITE_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_INVITE_RS),
            &Kernel::deal_prot_group_invite_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_INVITE_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_GROUP_INVITE_NOTIFY),
            &Kernel::deal_prot_group_invite_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_INVITE_DECISION_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_INVITE_DECISION_RS),
            &Kernel::deal_prot_group_invite_decision_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_INVITE_RESULT_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_GROUP_INVITE_RESULT_NOTIFY),
            &Kernel::deal_prot_group_invite_result_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_CHAT_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_GROUP_CHAT_NOTIFY),
            &Kernel::deal_prot_group_chat_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_CHAT_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_CHAT_RS),
            &Kernel::deal_prot_group_chat_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_OFFER_RS,
        ProtocolHandler{
            sizeof(PROT_FILE_OFFER_RS),
            &Kernel::deal_prot_file_offer_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_END_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_FILE_END_NOTIFY),
            &Kernel::deal_prot_file_end_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_OFFER_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_FILE_OFFER_NOTIFY),
            &Kernel::deal_prot_file_offer_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_DECISION_RS,
        ProtocolHandler{
            sizeof(PROT_FILE_DECISION_RS),
            &Kernel::deal_prot_file_decision_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_START_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_FILE_START_NOTIFY),
            &Kernel::deal_prot_file_start_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_CHUNK_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_FILE_CHUNK_NOTIFY),
            &Kernel::deal_prot_file_chunk_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_CHUNK_RS,
        ProtocolHandler{
            sizeof(PROT_FILE_CHUNK_RS),
            &Kernel::deal_prot_file_chunk_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_FILE_FINISH_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_FILE_FINISH_NOTIFY),
            &Kernel::deal_prot_file_finish_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_DELETE_FRIEND_RS,
        ProtocolHandler{
            sizeof(PROT_DELETE_FRIEND_RS),
            &Kernel::deal_prot_delete_friend_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_DELETE_FRIEND_NOTIFY,
        ProtocolHandler{
            sizeof(PROT_DELETE_FRIEND_NOTIFY),
            &Kernel::deal_prot_delete_friend_notify
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_LEAVE_RS,
        ProtocolHandler{
            sizeof(PROT_GROUP_LEAVE_RS),
            &Kernel::deal_prot_group_leave_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_GROUP_MEMBER_LEFT,
        ProtocolHandler{
            sizeof(PROT_GROUP_MEMBER_LEFT),
            &Kernel::deal_prot_group_member_left
        }
    );

    m_handlers.emplace(
        DEF_PROT_UPDATE_PROFILE_RS,
        ProtocolHandler{
            sizeof(PROT_UPDATE_PROFILE_RS),
            &Kernel::deal_prot_update_profile_rs
        }
    );

    m_handlers.emplace(
        DEF_PROT_CHANGE_PASSWORD_RS,
        ProtocolHandler{
            sizeof(PROT_CHANGE_PASSWORD_RS),
            &Kernel::deal_prot_change_password_rs
        }
    );

    //-------------------------------------------------------------------------------------------------------------------------------

    m_pLogin->show();

    //绑定 登录框注册信息
    connect(m_pLogin,SIGNAL(signals_register(QString,QString,QString)),this,SLOT(slots_sendRegisterInfo(QString,QString,QString)));
    connect(m_pLogin,SIGNAL(signals_login(QString,QString)),this,SLOT(slots_sendloginInfo(QString,QString)));

    //绑定 收到的聊天内容和朋友id 和 对应的槽的绑定
    connect(m_pMainWidget,SIGNAL(signals_sendMsgAndIdToKernel(int,QString)),this,SLOT(slots_sendServerMsg(int,QString)));

    //绑定 添加好友请求 和 对应槽
    connect(m_pMainWidget,SIGNAL(signals_addFriend(QString)),this,SLOT(slots_addFriend(QString)));

    //绑定 下线请求 和 对应槽
    connect(m_pMainWidget,SIGNAL(signals_userOffLine()),this,SLOT(slots_sendUserOffLine()));

    //绑定 关闭登录窗口信号 和 对应槽
    connect(m_pLogin,SIGNAL(signals_closeWindow()),this,SLOT(slots_delete()));

    //绑定 创建群聊请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_createGroup,this,&Kernel::slots_createGroup);

    //绑定 群成员资料查询请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_requestGroupMembers,this,&Kernel::slots_requestGroupMembers);

    //绑定 邀请好友进群查询请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_inviteGroupFriend,this,&Kernel::slots_inviteGroupFriend);

    //绑定 发送群聊消息请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_sendGroupMessage,this,&Kernel::slots_sendGroupMessage);

    //绑定 发送文件请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_sendFileToKernel,this,&Kernel::slots_sendFile);

    //绑定 删除好友请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_deleteFriend,this,&Kernel::slots_deleteFriend);

    //绑定 退出群聊请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_leaveGroup,this,&Kernel::slots_leaveGroup);

    //绑定 修改信息请求 和 对应槽
    connect(m_pMainWidget,&MainWidget::signals_updateProfile,this,&Kernel::slots_updateProfile);

    connect(m_pMainWidget,&MainWidget::signals_changePassword,this,&Kernel::slots_changePassword);

    connect(m_pMainWidget,&MainWidget::signals_logoutRequested,this,&Kernel::slots_logout);

    //打开客户端网络
    m_pMediator = new TCP_ClientMediator;
    if(!m_pMediator->openNet()){
        QMessageBox::about(m_pLogin,"提示","客户端网络打开失败");
        exit(-1);
    }
    //连接处理所有接收到数据的信号和槽函数
    connect(m_pMediator,SIGNAL(signals_recvServerData(char*,int,unsigned long)),
            this,SLOT(slots_recvServerData(char*,int,unsigned long)));
}

Kernel::~Kernel() {
    m_endingSession = true;

    if (m_pMediator) {
        // 先停止接收线程，不允许旧连接继续产生消息。
        m_pMediator->closeNet();

        // 处理已排队的接收消息。
        // 下一步添加的状态检查会直接释放这些缓冲区。
        QCoreApplication::sendPostedEvents(this,QEvent::MetaCall);
        delete m_pMediator;
        m_pMediator = nullptr;
    }

    // 释放读文件句柄和未提交的临时文件。
    m_outgoingFileTransfers.clear();
    m_incomingFileTransfers.clear();

    if (m_pLogin) {
        disconnect(m_pLogin, nullptr, this, nullptr);
        delete m_pLogin;
        m_pLogin = nullptr;
    }

    if (m_pMainWidget) {
        disconnect(m_pMainWidget, nullptr, this, nullptr);
        delete m_pMainWidget;
        m_pMainWidget = nullptr;
    }
}

void Kernel::slots_sendRegisterInfo(QString nick,QString tel,QString pass){
    qDebug()<<__func__;

    //定义注册请求的协议并填充数据
    PROT_REGISTER_RQ registerRq;
    //nick.toStdString().c_str();     //由 QString 转化为标准库 string，再转化为 const char*
    if (!writeUtf8Field(registerRq.nick, nick, "昵称") ||
        !writeUtf8Field(registerRq.tel, tel, "手机号") ||
        !writeUtf8Field(registerRq.pass, pass, "密码")) {
        return;
    }

    //通过网络发送给服务端
    m_pMediator->sendData((char*)&registerRq,sizeof(registerRq),0);
}

void Kernel::slots_sendloginInfo(QString tel,QString pass){
    qDebug()<<__func__;
    // 定义登录请求的协议并填充数据
    PROT_LOGIN_RQ loginRq;
    if (!writeUtf8Field(loginRq.tel, tel, "手机号") ||
        !writeUtf8Field(loginRq.pass, pass, "密码")) {
        return;
    }

    // 服务端收到数据
    m_pMediator->sendData((char*)&loginRq,sizeof(loginRq),0);
}

//统一接收服务端的数据
void Kernel::slots_recvServerData(char* pbuf,int len,unsigned long from){
    qDebug()<<__func__;
    //收到什么协议
    // 不论从哪个分支返回，都自动释放接收缓冲区。
    std::unique_ptr<char[]> bufferOwner(pbuf);

    if (m_endingSession) {
        return;
    }

    if (pbuf == nullptr || len < static_cast<int>(sizeof(prot_type))) {
        qWarning() << "收到不完整的协议头，长度：" << len;
        return;
    }

    prot_type type = 0;

    std::memcpy(&type,pbuf,sizeof(type));

    auto it = m_handlers.find(type);

    if (it == m_handlers.end()) {
        qWarning() << "未注册的协议编号：" << type;
        return;
    }

    const ProtocolHandler& handler = it->second;

    if (handler.function == nullptr || len != handler.expectedLength) {
        qWarning() << "协议长度错误，编号：" << type << "实际：" << len << "预期：" << handler.expectedLength;
        return;
    }

    (this->*handler.function)(pbuf, len, from);
}

void Kernel::slots_sendServerMsg(int friendId,QString text){
    qDebug()<<__func__;
    qDebug()<<"Kernel::slots_sendServerMsg：friendId："<<friendId<<"，text："<<text;

    //定义聊天请求
    PROT_CHAT_INFO_RQ chatinfoRq;
    chatinfoRq.myid = m_pMainWidget->getUserId();
    chatinfoRq.friendid = friendId;
    if (!writeUtf8Field(chatinfoRq.msg, text, "聊天内容")) {
        return;
    }

    //发送给服务端
    m_pMediator->sendData((char*)&chatinfoRq,sizeof(chatinfoRq),0);
}

void Kernel::slots_addFriend(QString firnick){
    qDebug()<<__func__;
    PROT_ADD_FRIEND_RQ addFriRq;
    addFriRq.myid = m_pMainWidget->getUserId();
    if (!writeUtf8Field(addFriRq.mynick,m_pMainWidget->getUserNick(),"我的昵称")
        || !writeUtf8Field(addFriRq.frinick,firnick,"好友昵称")) {
        return;
    }

    //添加好友请求发给服务端
    m_pMediator->sendData((char*)&addFriRq,sizeof(addFriRq),0);
}

void Kernel::slots_sendUserOffLine() {
    // 点击主窗口右上角关闭按钮：退出程序。
    slots_delete();
}

void Kernel::slots_delete() {
    if (m_endingSession) {
        return;
    }

    m_endingSession = true;
    emit signals_sessionFinished(false);
}

void Kernel::slots_createGroup(QString groupName){
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0) {
        QMessageBox::warning(nullptr,"提示","请先登录");
        return;
    }

    if (!m_pendingGroupCreates.empty()) {
        QMessageBox::information(m_pMainWidget,"提示","正在创建群，请等待服务器回复");
        return;
    }

    groupName = groupName.trimmed();

    if (groupName.isEmpty() || groupName.toUcs4().size() > 30) {
        QMessageBox::warning(m_pMainWidget,"提示","群名称必须为1到30个字符");
        return;
    }

    PROT_GROUP_CREATE_RQ request{};

    if (!writeUtf8Field(request.groupName,groupName,"群名称")) {
        return;
    }

    // 编号 0 保留不用。
    if (m_nextGroupRequestId == 0) {
        m_nextGroupRequestId = 1;
    }

    request.requestId = m_nextGroupRequestId++;

    m_pendingGroupCreates.emplace(request.requestId,groupName);

    bool sent = m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    if (!sent) {
        m_pendingGroupCreates.erase(request.requestId);

        QMessageBox::warning(m_pMainWidget,"提示","创建群请求发送失败，请检查网络");
        return;
    }

    qDebug()<< "创建群请求已发送，编号："<< request.requestId;
}

//发送查询群成员资料请求
void Kernel::slots_requestGroupMembers(int groupId){
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || groupId <= 0) {
        return;
    }

    if (m_pendingMemberQueries.count(groupId) != 0) {
        return;
    }

    if (m_nextMemberRequestId == 0) {
        m_nextMemberRequestId = 1;
    }

    PROT_GROUP_MEMBERS_RQ request{};
    request.requestId = m_nextMemberRequestId++;
    request.groupId = groupId;

    m_pendingMemberQueries.emplace(groupId,request.requestId);

    bool sent = m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    if (!sent) {
        m_pendingMemberQueries.erase(groupId);
        qWarning() << "群成员查询发送失败，群ID：" << groupId;
        return;
    }

    std::uint32_t requestId = request.requestId;

    QTimer::singleShot(5000,this,
        [this, groupId, requestId] {
            auto pending = m_pendingMemberQueries.find(groupId);

            if (pending != m_pendingMemberQueries.end() &&
                pending->second == requestId) {
                m_pendingMemberQueries.erase(pending);

                qWarning()
                    << "群成员查询超时，群ID："
                    << groupId;
            }
        }
        );
}

void Kernel::slots_inviteGroupFriend(int groupId,int targetUserId) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || groupId <= 0 || targetUserId <= 0) {
        return;
    }

    // 同一个群、同一个好友，有请求正在等待回复时不重复发送
    for (const auto& entry : m_pendingGroupInvites) {
        if (entry.second.groupId == groupId && entry.second.targetUserId == targetUserId) {
            QMessageBox::information(m_pMainWidget,"提示","邀请请求正在处理中，请稍候");
            return;
        }
    }

    if (m_pendingGroupInvites.size() >= 16) {
        QMessageBox::information(m_pMainWidget,"提示","等待回复的邀请请求过多，请稍后再试");
        return;
    }

    // 跳过 0 和仍在使用中的请求编号
    std::uint32_t requestId = 0;

    do {
        requestId = m_nextInviteRequestId++;
    } while (
        requestId == 0 || m_pendingGroupInvites.count(requestId) != 0
        );

    PROT_GROUP_INVITE_RQ request{};
    request.requestId = requestId;
    request.groupId = groupId;
    request.targetUserId = targetUserId;

    // 先记录，再发送，方便收到回复时核对
    m_pendingGroupInvites.emplace(
        requestId,
        PendingGroupInvite{groupId, targetUserId}
        );

    // 沿用项目现有的 sendData() 调用方式
    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    qDebug() << "邀请请求已发送，编号：" << requestId << "群ID：" << groupId << "目标用户ID：" << targetUserId;

    // 这里只等待服务端对请求的确认，不是等待好友作出决定
    QTimer::singleShot(5000, this, [this, requestId] {
        auto it = m_pendingGroupInvites.find(requestId);

        if (it == m_pendingGroupInvites.end()) {
            return;
        }

        m_pendingGroupInvites.erase(it);

        QMessageBox::warning(m_pMainWidget,"提示","邀请请求确认超时，无法确定是否已送达");
    });
}

void Kernel::slots_sendGroupMessage(int groupId,const QString& message) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || groupId <= 0) {
        return;
    }

    // Kernel 再检查一次，不只依赖窗口的输入校验。
    if (message.trimmed().isEmpty() || message.contains(QChar('\0'))) {
        QMessageBox::warning(m_pMainWidget,"发送消息","消息为空或包含不支持的字符");
        return;
    }

    const QByteArray bytes = message.toUtf8();

    if (bytes.size() >=
        static_cast<int>(GROUP_MESSAGE_BYTES)) {
        QMessageBox::warning(m_pMainWidget,"发送消息","消息过长，请分开发送");
        return;
    }

    // 避免网络异常时不断积累待处理消息。
    if (m_pendingGroupMessages.size() >= 64) {
        QMessageBox::warning(m_pMainWidget,"发送消息","等待处理的消息过多，请稍后再试");
        return;
    }

    std::uint32_t requestId = 0;

    do {
        requestId = m_nextGroupMessageRequestId++;
    }
    while (requestId == 0 || m_pendingGroupMessages.count(requestId) != 0);

    PROT_GROUP_CHAT_RQ request{};
    request.requestId = requestId;
    request.groupId = groupId;
    request.contentLength = static_cast<std::uint32_t>(bytes.size());

    // request 已清零，复制后仍然保留末尾 '\0'。
    std::memcpy(request.content,bytes.constData(),static_cast<std::size_t>(bytes.size()));

    m_pendingGroupMessages.emplace(requestId,PendingGroupMessage{groupId, message});

    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    qDebug() << "群消息请求已发送，编号：" << requestId << "群ID：" << groupId << "UTF-8字节数：" << bytes.size();

    QTimer::singleShot(10000, this, [this, requestId] {
        auto it = m_pendingGroupMessages.find(requestId);

        if (it == m_pendingGroupMessages.end()) {
            return;
        }

        const PendingGroupMessage pending = it->second;
        m_pendingGroupMessages.erase(it);

        qWarning() << "群消息确认超时，编号：" << requestId << "群ID：" << pending.groupId;

        m_pMainWidget->setGroupMessageResult(pending.groupId,pending.message,QStringLiteral("确认超时，无法确定是否已送达"),false);
    });
}

void Kernel::slots_sendFile(int friendId,const QString& filePath) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || friendId <= 0 || friendId == m_pMainWidget->getUserId()) {
        return;
    }

    if (m_preparingFile) {
        QMessageBox::information(m_pMainWidget,"发送文件","正在准备文件，请稍候");
        return;
    }

    const int currentUserId = m_pMainWidget->getUserId();

    m_preparingFile = true;

    auto* watcher = new QFutureWatcher<PreparedOutgoingFile>(this);

    connect(watcher,&QFutureWatcher<PreparedOutgoingFile>::finished,this,[this, watcher, currentUserId] {
            const PreparedOutgoingFile prepared =watcher->result();

            watcher->deleteLater();
            m_preparingFile = false;

            // 准备期间如果已经退出或切换账号，不继续发送。
            if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() != currentUserId) {
                return;
            }

            if (!prepared.error.isEmpty()) {
                QMessageBox::warning(m_pMainWidget,"发送文件",prepared.error);
                return;
            }

            submitFileOffer(prepared);
        }
        );

    watcher->setFuture(
        QtConcurrent::run(
            [friendId, filePath]() -> PreparedOutgoingFile {
                PreparedOutgoingFile result;
                result.targetUserId = friendId;
                result.filePath = filePath;

                // 文件对象在后台线程中创建和使用。
                QFile file(filePath);

                if (!file.open(QIODevice::ReadOnly)) {
                    result.error = "无法读取文件，请重新选择";
                    return result;
                }

                const QFileInfo info(file);
                result.fileName = info.fileName();
                result.fileSize = file.size();

                if (!info.isFile() || result.fileSize < 0 || static_cast<quint64>(result.fileSize) > MAX_TRANSFER_FILE_BYTES) {
                    result.error = "请选择不超过 64 MiB 的普通文件";
                    return result;
                }

                const QByteArray nameBytes = result.fileName.toUtf8();

                if (nameBytes.isEmpty() || result.fileName.contains(QChar('\0')) ||
                    nameBytes.size() >= static_cast<int>(FILE_NAME_BYTES)) {
                    result.error = "文件名过长或包含不支持的字符";
                    return result;
                }

                QCryptographicHash hash(QCryptographicHash::Sha256);

                qint64 totalRead = 0;

                while (true) {
                    const QByteArray chunk = file.read(64 * 1024);

                    if (file.error() != QFileDevice::NoError) {
                        result.error = "读取文件失败，请检查文件后重试";
                        return result;
                    }

                    if (chunk.isEmpty()) {
                        break;
                    }

                    totalRead += chunk.size();

                    if (totalRead > result.fileSize) {
                        result.error = "文件在准备过程中发生变化，请重新选择";
                        return result;
                    }

                    hash.addData(chunk);
                }

                if (totalRead != result.fileSize || file.size() != result.fileSize) {
                    result.error = "文件在准备过程中发生变化，请重新选择";
                    return result;
                }

                result.sha256 = hash.result();
                return result;
            }
            )
        );
}

void Kernel::submitFileOffer(const PreparedOutgoingFile& prepared) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || prepared.targetUserId <= 0 || prepared.targetUserId == m_pMainWidget->getUserId()) {
        return;
    }

    if (prepared.sha256.size() != static_cast<int>(FILE_SHA256_BYTES) || prepared.fileSize < 0 ||
        static_cast<quint64>(prepared.fileSize) > MAX_TRANSFER_FILE_BYTES) {
        QMessageBox::warning(m_pMainWidget,"发送文件","文件信息无效，请重新选择");
        return;
    }

    const QByteArray nameBytes = prepared.fileName.toUtf8();

    if (nameBytes.isEmpty() || prepared.fileName.contains(QChar('\0')) ||
        nameBytes.size() >= static_cast<int>(FILE_NAME_BYTES)) {
        QMessageBox::warning(m_pMainWidget,"发送文件","文件名过长或包含不支持的字符");
        return;
    }

    if (m_pendingFileOffers.size() + m_outgoingFileTransfers.size() >= 4) {
        QMessageBox::information(m_pMainWidget,"发送文件","待处理的文件任务过多，请等待完成或取消后再试");
        return;
    }

    // 同一文件发给同一好友时，不重复提交待确认申请。
    for (const auto& entry : m_pendingFileOffers) {
        const auto& pending = entry.second;

        if (pending.targetUserId ==prepared.targetUserId && pending.filePath == prepared.filePath) {
            QMessageBox::information(m_pMainWidget,"发送文件","这个文件的申请正在处理中，请稍候");
            return;
        }
    }

    // 已获得传输编号的任务，也不能重复提交。
    for (const auto& entry : m_outgoingFileTransfers) {
        const auto& file = entry.second.file;

        if (file.targetUserId == prepared.targetUserId && file.filePath == prepared.filePath) {
            QMessageBox::information(m_pMainWidget,"发送文件","这个文件已有发送任务，请等待完成或取消");
            return;
        }
    }

    std::uint32_t requestId = 0;

    do {
        requestId = m_nextFileOfferRequestId++;
    } while (requestId == 0 || m_pendingFileOffers.count(requestId) != 0);

    PROT_FILE_OFFER_RQ request{};
    request.requestId = requestId;
    request.fileSize = static_cast<std::uint64_t>(prepared.fileSize);
    request.targetUserId = prepared.targetUserId;

    std::memcpy(request.fileName,nameBytes.constData(),static_cast<std::size_t>(nameBytes.size()));

    // 复制 32 字节原始摘要，不复制 toHex() 的文本。
    std::memcpy(request.sha256,prepared.sha256.constData(),FILE_SHA256_BYTES);

    // 先保存本地信息，再发送申请。
    m_pendingFileOffers.emplace(requestId, prepared);

    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    qDebug() << "文件申请已提交，编号：" << requestId << "目标好友ID：" << prepared.targetUserId << "文件名：" << prepared.fileName;

    QTimer::singleShot(10000, this, [this, requestId] {
        auto it = m_pendingFileOffers.find(requestId);

        if (it == m_pendingFileOffers.end()) {
            return;
        }

        const PreparedOutgoingFile pending = it->second;
        m_pendingFileOffers.erase(it);

        qWarning() << "文件申请确认超时，编号：" << requestId;

        // 私聊显示接口使用富文本，因此转义文件名。
        m_pMainWidget->setChatMsg(pending.targetUserId,QStringLiteral("文件“%1”：申请确认超时，无法确定是否已送达").arg(pending.fileName.toHtmlEscaped()));
    });
}

void Kernel::slots_logout() {
    if (m_endingSession || m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0) {
        return;
    }

    m_endingSession = true;
    emit signals_sessionFinished(true);
}

//---------所有的 deal 函数----------------------------------------------------------
void Kernel::deal_prot_register_rs(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_REGISTER_RS* pRegisiterRs = (PROT_REGISTER_RS*) pbuf;
    if(pRegisiterRs->result==REGISTER_RESULT_SUCC){
        QMessageBox::information(nullptr,"提示","注册成功！");
    }
    else if(pRegisiterRs->result==REGISTER_RESULT_FAIL_TEL){
        QMessageBox::critical(nullptr,"错误","注册失败：该电话号已被注册！");
    }
    else if(pRegisiterRs->result==REGISTER_RESULT_FAIL_NICK){
        QMessageBox::critical(nullptr,"错误","注册失败：昵称已存在！");
    }
}
void Kernel::deal_prot_login_rs(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_LOGIN_RS* pLoginRs = (PROT_LOGIN_RS*)pbuf;
    if(pLoginRs->result == LOGIN_RESULT_SUCC){
        //存储用户id
        m_pMainWidget->setUserId(pLoginRs->userid);

        //显示主聊天窗口
        m_pMainWidget->show();

        //隐藏登录窗口
        m_pLogin->hide();

        qDebug()<<"用户的id为："<<m_pMainWidget->getUserId();
    }
    else if(pLoginRs->result == LOGIN_RESULT_NOEXIST){
        QMessageBox::critical(nullptr,"错误","登录失败：用户不存在！");
    }
    else if(pLoginRs->result == LOGIN_RESULT_PASSERR){
        QMessageBox::critical(nullptr,"错误","登录失败：密码错误！");
    }
    else if(pLoginRs->result == LOGIN_RESULT_ALREADY_ONLINE){
        QMessageBox::critical(nullptr,"错误","登录失败：该账号已在线！请先退出原来的登录");
    }
    else if(pLoginRs->result == LOGIN_RESULT_CONNECTION_LOGGED_IN){
        QMessageBox::critical(nullptr,"错误","登录失败：当前连接已经登录，请重新启动客户端后切换账号！");
    }
}

void Kernel::deal_prot_friend_info(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_FRIEND_INFO* pmyinfo = (PROT_FRIEND_INFO*)pbuf;

    //转化收到的昵称和签名为utf-8
    QString nick = readUtf8Field(pmyinfo->nick);
    QString feeling = readUtf8Field(pmyinfo->feeling);
    //判断自己还是朋友
    if(m_pMainWidget->getUserId() == pmyinfo->friendid){
        m_pMainWidget->setMyInfo(pmyinfo->iconid,nick,feeling);
    }
    else{//朋友
        qDebug()<<"朋友";
        m_pMainWidget->setFriendInfo(pmyinfo->friendid,pmyinfo->iconid,pmyinfo->status,nick,feeling);
    }
}
void Kernel::deal_prot_chat_info_rs(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_CHAT_INFO_RS* pChatinfoRs = (PROT_CHAT_INFO_RS*)pbuf;
    qDebug()<<"收到了本次请求的回复，ID为"<<pChatinfoRs->myid<<"的朋友，是否收到了我（ID为"
             <<pChatinfoRs->friendid<<"）发送的消息，结果是："<<pChatinfoRs->result;

    if(pChatinfoRs->result == CHAT_RESULT_SUCC){    //已送达
        m_pMainWidget->setChatMsg(pChatinfoRs->myid,"已提交转发");
    }
    else if(pChatinfoRs->result == CHAT_RESULT_FAIL){   //已下线
        m_pMainWidget->setChatMsg(pChatinfoRs->myid,"发送失败：对方离线或不允许发送");
    }
}
void Kernel::deal_prot_chat_info_rq(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_CHAT_INFO_RQ* pChatinfoRq = (PROT_CHAT_INFO_RQ*)pbuf;
    QString message = readUtf8Field(pChatinfoRq->msg);
    QString text = QString("<font color='black'><p><span style='font-size:16pt; font-weight:bold'>") + message + "</span></p></font>";
    m_pMainWidget->setChatMsg(pChatinfoRq->myid,text);
}
void Kernel::deal_prot_add_friend_rs(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    qDebug()<<"收到添加好友的回复";

    PROT_ADD_FRIEND_RS* pAddFriRs = (PROT_ADD_FRIEND_RS*)pbuf;
    QString firnick = readUtf8Field(pAddFriRs->mynick);
    if(pAddFriRs->result == ADDFRI_RESULT_ACCEPT){
        QMessageBox::information(nullptr,"提示",QString("【%1】同意了我的好友请求").arg(firnick));
    }
    else if(pAddFriRs->result == ADDFRI_RESULT_REJECT){
        QMessageBox::information(nullptr,"提示",QString("【%1】拒绝了我的好友请求").arg(firnick));
    }
    else if(pAddFriRs->result == ADDFRI_RESULT_OFFLINE){
        QMessageBox::information(nullptr,"提示",QString("【%1】用户不在线").arg(firnick));
    }
    else if(pAddFriRs->result == ADDFRI_RESULT_NOEXIST){
        QMessageBox::information(nullptr,"提示",QString("【%1】用户不存在").arg(firnick));
    }
    else if (pAddFriRs->result == ADDFRI_RESULT_SELF) {
        QMessageBox::information(nullptr, "提示", "不能添加自己为好友");
    }
    else if (pAddFriRs->result == ADDFRI_RESULT_ALREADY_FRIEND) {
        QMessageBox::information(nullptr, "提示", QString("【%1】已经是你的好友").arg(firnick));
    }
    else if (pAddFriRs->result == ADDFRI_RESULT_PENDING) {
        QMessageBox::information(nullptr, "提示", "双方已有待处理的好友申请，请等待处理");
    }
    else if (pAddFriRs->result == ADDFRI_RESULT_BUSY) {
        QMessageBox::information(nullptr, "提示", "待处理好友申请过多，请稍后重试");
    }
}
void Kernel::deal_prot_add_friend_rq(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    qDebug()<<"收到好友请求";

    PROT_ADD_FRIEND_RQ* pAddFriRq = (PROT_ADD_FRIEND_RQ*)pbuf;
    QString applicantNick = readUtf8Field(pAddFriRq->mynick);
    auto but = QMessageBox::information(nullptr,"收到好友请求",
        QString("【%1】想要添加你为好友").arg(applicantNick),QMessageBox::Yes | QMessageBox::No);

    //定义回复协议
    PROT_ADD_FRIEND_RS addFriendRs;
    addFriendRs.destid = pAddFriRq->myid;
    addFriendRs.myid = m_pMainWidget->getUserId();
    if (!writeUtf8Field(addFriendRs.destnick,readUtf8Field(pAddFriRq->mynick),"对方昵称")
        || !writeUtf8Field(addFriendRs.mynick,m_pMainWidget->getUserNick(),"我的昵称")) {
        return;
    }

    if(but == QMessageBox::Yes){
        addFriendRs.result = ADDFRI_RESULT_ACCEPT;
    }
    else if(but == QMessageBox::No){
        addFriendRs.result = ADDFRI_RESULT_REJECT;
    }

    //将回复添加好友请求的协议发送给服务端
    m_pMediator->sendData((char*)&addFriendRs,sizeof(addFriendRs),0);
}
void Kernel::deal_prot_friend_offline(char* pbuf,int len,unsigned long){
    qDebug()<<__func__;
    PROT_FRIEND_OFFLINE* pfriOff = (PROT_FRIEND_OFFLINE*)pbuf;

    qDebug()<<QString("收到朋友(%1)下线的通知").arg(pfriOff->offUserid);
    //下线的一系列处理
    m_pMainWidget->setFriendOffline(pfriOff->offUserid);
}

void Kernel::deal_prot_group_create_rs(char* pbuf,int len,unsigned long){
    if (len != static_cast<int>(sizeof(PROT_GROUP_CREATE_RS))) {
        qWarning() << "创建群回复长度错误";
        return;
    }

    PROT_GROUP_CREATE_RS reply{};

    std::memcpy(&reply,pbuf,sizeof(reply));

    auto pending = m_pendingGroupCreates.find(reply.requestId);

    if (pending == m_pendingGroupCreates.end()) {
        qWarning() << "未找到对应的创建群请求：" << reply.requestId;
        return;
    }

    QString requestedName = pending->second;
    m_pendingGroupCreates.erase(pending);

    if (reply.result == GROUP_CREATE_INVALID_NAME) {
        QMessageBox::warning(m_pMainWidget,"提示","服务器拒绝了群名称，请检查长度和内容");
        return;
    }

    if (reply.result != GROUP_CREATE_SUCC) {
        QMessageBox::warning(m_pMainWidget,"提示","创建群失败");
        return;
    }

    if (m_pMainWidget == nullptr ||reply.groupId <= 0 ||reply.ownerId != m_pMainWidget->getUserId()) {
        qWarning() << "创建群回复中的身份或群ID不正确";
        return;
    }

    QString returnedName = readUtf8Field(reply.groupName);

    if (returnedName != requestedName) {
        qWarning() << "创建群回复中的群名与请求不一致";
        return;
    }

    m_pMainWidget->setGroupInfo(reply.groupId,returnedName,reply.ownerId);

    QMessageBox::information(m_pMainWidget,"创建成功",QString("群名称：%1\n群ID：%2").arg(returnedName).arg(reply.groupId));

    // 后续在这里加入群列表。
}

void Kernel::deal_prot_group_info(char* pbuf,int len,unsigned long){
    if (m_pMainWidget == nullptr ||
        len != static_cast<int>(sizeof(PROT_GROUP_INFO))) {
        return;
    }

    PROT_GROUP_INFO info{};

    std::memcpy(&info,pbuf,sizeof(info));

    if (info.groupId <= 0 || info.ownerId <= 0) {
        qWarning() << "收到无效的群资料";
        return;
    }

    QString groupName = readUtf8Field(info.groupName);

    if (groupName.isEmpty()) {
        qWarning() << "收到空群名";
        return;
    }

    m_pMainWidget->setGroupInfo(info.groupId,groupName,info.ownerId);

    //新成员收到群资料时，如果对应窗口已经存在，也会刷新成员列表；没有打开窗口则不额外查询
    m_pMainWidget->refreshOpenGroupMembers(info.groupId);
}

void Kernel::deal_prot_group_members_rs(char* pbuf,int len,unsigned long){
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_GROUP_MEMBERS_RS))) {
        return;
    }

    PROT_GROUP_MEMBERS_RS reply{};

    std::memcpy(&reply,pbuf,sizeof(reply));

    auto pending = m_pendingMemberQueries.find(reply.groupId);

    if (pending == m_pendingMemberQueries.end() || pending->second != reply.requestId) {
        qWarning() << "忽略不匹配或已超时的群成员回复";
        return;
    }

    m_pendingMemberQueries.erase(pending);

    if (reply.result != GROUP_MEMBERS_SUCC) {
        QString reason = "查询群成员失败";

        if (reply.result == GROUP_MEMBERS_NOT_MEMBER) {
            reason = "你不是该群成员";
        }
        else if (reply.result == GROUP_MEMBERS_NOT_FOUND) {
            reason = "该群不存在";
        }
        else if (reply.result == GROUP_MEMBERS_TOO_MANY) {
            reason = "群成员数量超过当前支持上限";
        }

        // 无成员资格或群已不存在时，不能继续显示旧资料。
        if (reply.result == GROUP_MEMBERS_NOT_MEMBER || reply.result == GROUP_MEMBERS_NOT_FOUND) {
            m_pMainWidget->setGroupMembers(reply.groupId,QVector<GroupMemberView>{});
        }

        QMessageBox::warning(m_pMainWidget,"群成员",reason);

        return;
    }

    // 已创建的群至少应包含群主。
    if (reply.memberCount <= 0 || reply.memberCount > static_cast<int>(MAX_GROUP_MEMBERS)) {
        qWarning() << "群成员数量非法";
        return;
    }

    QVector<GroupMemberView> members;
    QSet<int> seen;

    for (int i = 0; i < reply.memberCount; ++i) {
        const auto& source = reply.members[i];

        auto end = std::find(source.nick,source.nick + sizeof(source.nick),'\0');

        if (source.userId <= 0 || seen.contains(source.userId) || end == source.nick + sizeof(source.nick)) {
            qWarning() << "群成员资料非法";
            return;
        }

        QString nick = readUtf8Field(source.nick);

        if (nick.isEmpty()) {
            qWarning() << "群成员昵称为空";
            return;
        }

        seen.insert(source.userId);

        GroupMemberView member;
        member.userId = source.userId;
        member.iconId = source.iconId;
        member.nick = nick;

        members.push_back(member);
    }

    if (!seen.contains(m_pMainWidget->getUserId())) {
        qWarning() << "群成员回复不包含当前用户";
        return;
    }

    // 整份回复检查通过后，再一次性更新界面。
    m_pMainWidget->setGroupMembers(reply.groupId,members);
}

void Kernel::deal_prot_group_invite_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_GROUP_INVITE_RS))) {
        return;
    }

    PROT_GROUP_INVITE_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingGroupInvites.find(reply.requestId);

    if (it == m_pendingGroupInvites.end()) {
        qWarning() << "收到未知或已超时的群邀请回复：" << reply.requestId;
        return;
    }

    const PendingGroupInvite pending = it->second;

    if (reply.groupId != pending.groupId || reply.targetUserId != pending.targetUserId) {
        qWarning() << "群邀请回复与原请求不匹配";
        return;
    }

    m_pendingGroupInvites.erase(it);

    QString message;

    switch (reply.result) {
    case GROUP_INVITE_SUCC:
        if (reply.invitationId == 0) {
            qWarning() << "群邀请回复缺少有效邀请编号";
            return;
        }

        message = "邀请已送达，等待对方确认";

        qDebug() << "群邀请已送达，邀请编号：" << reply.invitationId;
        break;

    case GROUP_INVITE_NOT_OWNER:
        message = "只有群主可以邀请好友";
        break;

    case GROUP_INVITE_NOT_FRIEND:
        message = "只能邀请自己的好友";
        break;

    case GROUP_INVITE_OFFLINE:
        message = "对方当前不在线";
        break;

    case GROUP_INVITE_ALREADY_MEMBER:
        message = "对方已经是群成员";
        break;

    case GROUP_INVITE_FULL:
        message = "群成员人数已达到上限";
        break;

    case GROUP_INVITE_NOT_FOUND:
        message = "群不存在";
        break;

    case GROUP_INVITE_PENDING:
        message = "已有待处理的邀请，请等待对方确认";
        break;

    case GROUP_INVITE_FAIL:
        message = "邀请失败，请稍后再试";
        break;

    default:
        qWarning() << "未知的群邀请回复结果：" << reply.result;
        return;
    }

    QMessageBox::information(m_pMainWidget,"邀请好友入群",message);
}

void Kernel::deal_prot_group_invite_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_GROUP_INVITE_NOTIFY))) {
        return;
    }

    // 先复制数据，弹窗期间不再依赖原接收缓冲区。
    PROT_GROUP_INVITE_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.invitationId == 0 || notification.groupId <= 0 || notification.inviterId <= 0) {
        qWarning() << "收到无效的群邀请";
        return;
    }

    // 在固定数组范围内寻找字符串结束符。
    const char* groupEnd = static_cast<const char*>(std::memchr(notification.groupName,'\0',sizeof(notification.groupName)));

    const char* nickEnd = static_cast<const char*>(std::memchr(notification.inviterNick,'\0',sizeof(notification.inviterNick)));

    if (groupEnd == nullptr || nickEnd == nullptr || groupEnd == notification.groupName || nickEnd == notification.inviterNick) {
        qWarning() << "群邀请名称字段不合法";
        return;
    }

    const QString groupName = QString::fromUtf8(notification.groupName,static_cast<int>(groupEnd - notification.groupName));

    const QString inviterNick = QString::fromUtf8(notification.inviterNick,static_cast<int>(nickEnd - notification.inviterNick));

    QMessageBox dialog(m_pMainWidget);

    dialog.setWindowTitle("收到群邀请");
    dialog.setIcon(QMessageBox::Question);

    // 名称作为普通文本显示，避免被解释为 HTML。
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QString("【%1】邀请你加入群：\n%2（群号：%3）\n\n是否接受邀请？").arg(inviterNick).arg(groupName).arg(notification.groupId));
    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dialog.setDefaultButton(QMessageBox::No);

    const int answer = dialog.exec();

    const std::int32_t decision = answer == QMessageBox::Yes ? GROUP_INVITE_DECISION_ACCEPT : GROUP_INVITE_DECISION_REJECT;

    qDebug() << "群邀请决定，邀请编号：" << notification.invitationId << "群ID：" << notification.groupId << "决定：" << decision;

    sendGroupInviteDecision(notification.invitationId,notification.groupId,decision);
}

// 构造并发送邀请进群回复
void Kernel::sendGroupInviteDecision(std::uint32_t invitationId,std::int32_t groupId,std::int32_t decision) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || invitationId == 0 || groupId <= 0) {
        return;
    }

    if (decision != GROUP_INVITE_DECISION_ACCEPT && decision != GROUP_INVITE_DECISION_REJECT) {
        return;
    }

    // 同一条邀请正在等待处理结果时，不重复发送决定。
    for (const auto& entry : m_pendingGroupDecisions) {
        if (entry.second.invitationId == invitationId) {
            return;
        }
    }

    if (m_pendingGroupDecisions.size() >= 16) {
        QMessageBox::warning(m_pMainWidget,"群邀请","等待处理的决定过多，请稍后再试");
        return;
    }

    std::uint32_t requestId = 0;

    do {
        requestId = m_nextGroupDecisionRequestId++;
    }
    while (requestId == 0 || m_pendingGroupDecisions.count(requestId) != 0);

    PROT_GROUP_INVITE_DECISION_RQ request{};
    request.requestId = requestId;
    request.invitationId = invitationId;
    request.decision = decision;

    m_pendingGroupDecisions.emplace(requestId,
        PendingGroupDecision{
            invitationId,
            groupId,
            decision
        });

    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    QTimer::singleShot(5000, this, [this, requestId] {
        auto it = m_pendingGroupDecisions.find(requestId);

        if (it == m_pendingGroupDecisions.end()) {
            return;
        }

        m_pendingGroupDecisions.erase(it);

        QMessageBox::warning(m_pMainWidget,"群邀请","处理结果确认超时，无法确定操作是否成功，请重新登录刷新群列表");
    });
}

//收到进群回复处理函数
void Kernel::deal_prot_group_invite_decision_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_GROUP_INVITE_DECISION_RS))) {
        return;
    }

    PROT_GROUP_INVITE_DECISION_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingGroupDecisions.find(reply.requestId);

    if (it == m_pendingGroupDecisions.end()) {
        qWarning() << "收到未知或已超时的群邀请决定回复";
        return;
    }

    const PendingGroupDecision pending = it->second;

    const bool expiredWithoutGroup = reply.result == GROUP_INVITE_EXPIRED && reply.groupId == 0;

    if (reply.invitationId != pending.invitationId || (!expiredWithoutGroup && reply.groupId != pending.groupId)) {
        qWarning() << "群邀请决定回复与原请求不匹配";
        return;
    }

    m_pendingGroupDecisions.erase(it);

    QString message;

    switch (reply.result) {
    case GROUP_INVITE_SUCC:
        if (pending.decision != GROUP_INVITE_DECISION_ACCEPT) {
            qWarning() << "拒绝操作收到异常的加入成功结果";
            return;
        }

        message = "已成功加入群";
        break;

    case GROUP_INVITE_REJECTED:
        if (pending.decision != GROUP_INVITE_DECISION_REJECT) {
            qWarning() << "接受操作收到异常的拒绝结果";
            return;
        }

        message = "已拒绝群邀请";
        break;

    case GROUP_INVITE_EXPIRED:
        message = "邀请已失效，请让群主重新邀请";
        break;

    case GROUP_INVITE_FULL:
        message = "群成员人数已达到上限";
        break;

    case GROUP_INVITE_ALREADY_MEMBER:
        message = "你已经是群成员";
        break;

    case GROUP_INVITE_NOT_FOUND:
        message = "群不存在";
        break;

    case GROUP_INVITE_NOT_OWNER:
        message = "邀请人已不是群主，邀请无法完成";
        break;

    case GROUP_INVITE_NOT_FRIEND:
        message = "好友关系已发生变化，邀请无法完成";
        break;

    case GROUP_INVITE_FAIL:
        message = "操作失败，请稍后再试";
        break;

    default:
        qWarning() << "未知的群邀请决定结果：" << reply.result;
        return;
    }

    QMessageBox::information(m_pMainWidget,"群邀请处理结果",message);
}

void Kernel::deal_prot_group_invite_result_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_GROUP_INVITE_RESULT_NOTIFY))) {
        return;
    }

    PROT_GROUP_INVITE_RESULT_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.invitationId == 0 || notification.groupId <= 0 || notification.targetUserId <= 0) {
        qWarning() << "收到无效的群邀请结果通知";
        return;
    }

    const QString friendName = m_pMainWidget->friendDisplayName(notification.targetUserId);
    const QString groupName = m_pMainWidget->groupDisplayName(notification.groupId);

    QString message;

    switch (notification.result) {
        case GROUP_INVITE_REJECTED:
            message = QStringLiteral("【%1】拒绝了加入“%2”的邀请").arg(friendName, groupName);
            break;

        case GROUP_INVITE_SUCC:
            message = QStringLiteral("【%1】已加入“%2”").arg(friendName, groupName);
            m_pMainWidget->refreshOpenGroupMembers(notification.groupId);
            break;

        case GROUP_INVITE_EXPIRED:
            message = QStringLiteral("邀请【%1】加入“%2”的邀请已失效").arg(friendName, groupName);
            break;

        case GROUP_INVITE_FAIL:
            message = QStringLiteral("【%1】未能加入“%2”，请稍后重试").arg(friendName, groupName);
            break;

        default:
            qWarning() << "未知的群邀请最终结果：" << notification.result;
            return;
    }

    QMessageBox::information(m_pMainWidget,"群邀请结果",message);
}

void Kernel::deal_prot_group_chat_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_GROUP_CHAT_NOTIFY))) {
        return;
    }

    PROT_GROUP_CHAT_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.requestId == 0 ||
        notification.groupId <= 0 ||
        notification.senderId <= 0 ||
        notification.contentLength == 0 ||
        notification.contentLength >= GROUP_MESSAGE_BYTES) {
        qWarning() << "收到无效的群消息字段";
        return;
    }

    if (notification.content[notification.contentLength] != '\0' ||
        std::memchr(notification.content,'\0',notification.contentLength) != nullptr) {
        qWarning() << "群消息正文长度不合法";
        return;
    }

    const char* nickEnd =static_cast<const char*>(std::memchr(notification.senderNick,'\0',sizeof(notification.senderNick)));

    if (nickEnd == nullptr || nickEnd == notification.senderNick) {
        qWarning() << "群消息昵称不合法";
        return;
    }

    const QByteArray nickBytes(notification.senderNick,static_cast<int>(nickEnd - notification.senderNick));
    const QByteArray messageBytes(notification.content,static_cast<int>(notification.contentLength));

    const QString nick = QString::fromUtf8(nickBytes);
    const QString message = QString::fromUtf8(messageBytes);

    // 防止无效 UTF-8 被静默替换后显示。
    if (nick.toUtf8() != nickBytes || message.toUtf8() != messageBytes) {
        qWarning() << "群消息包含无效的 UTF-8 数据";
        return;
    }

    m_pMainWidget->appendGroupMessage(notification.groupId,nick,message);
}

void Kernel::deal_prot_group_chat_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_GROUP_CHAT_RS))) {
        return;
    }

    PROT_GROUP_CHAT_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingGroupMessages.find(reply.requestId);

    if (it == m_pendingGroupMessages.end()) {
        qWarning() << "收到未知或已超时的群消息回复";
        return;
    }

    if (reply.groupId != it->second.groupId) {
        qWarning() << "群消息回复与原请求不匹配";
        return;
    }

    QString status;
    bool success = false;

    switch (reply.result) {
    case GROUP_CHAT_SUCC:
        if (reply.forwardedCount < 0 || reply.forwardedCount >= static_cast<std::int32_t>(MAX_GROUP_MEMBERS)) {
            qWarning() << "群消息转发人数不合法";
            return;
        }

        success = true;

        if (reply.forwardedCount == 0) {
            status = "当前没有其他成员可接收，本消息未保存";
        }
        else {
            status = QStringLiteral("已提交转发给 %1 位在线成员").arg(reply.forwardedCount);
        }
        break;

    case GROUP_CHAT_NOT_MEMBER:
        status = "发送失败：你不是该群成员";
        break;

    case GROUP_CHAT_NOT_FOUND:
        status = "发送失败：该群不存在";
        break;

    case GROUP_CHAT_INVALID_MESSAGE:
        status = "发送失败：消息内容不符合要求";
        break;

    case GROUP_CHAT_FAIL:
        status = "发送失败，请稍后重试";
        break;

    default:
        qWarning() << "未知的群消息回复结果：" << reply.result;
        return;
    }

    const PendingGroupMessage pending = it->second;
    m_pendingGroupMessages.erase(it);

    m_pMainWidget->setGroupMessageResult(pending.groupId,pending.message,status,success);
}

void Kernel::deal_prot_file_offer_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_OFFER_RS))) {
        return;
    }

    PROT_FILE_OFFER_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingFileOffers.find(reply.requestId);

    if (it == m_pendingFileOffers.end()) {
        qWarning() << "收到未知或已超时的文件申请回复：" << reply.requestId;
        return;
    }

    const PreparedOutgoingFile prepared = it->second;

    QString status;

    switch (reply.result) {
    case FILE_RESULT_SUCC: {
        if (reply.transferId == 0 || m_outgoingFileTransfers.count(reply.transferId) != 0) {
            qWarning() << "文件传输编号无效或重复";
            return;
        }

        OutgoingFileTransfer transfer;
        transfer.file = prepared;

        m_outgoingFileTransfers.emplace(reply.transferId,std::move(transfer));

        status = QStringLiteral("文件“%1”：申请已送达，等待对方接受").arg(prepared.fileName);

        qDebug() << "文件申请转发成功，传输编号：" << reply.transferId;
        break;
    }

    case FILE_RESULT_NOT_FRIEND:
        status = QStringLiteral("文件“%1”：发送失败，对方不是你的好友").arg(prepared.fileName);
        break;

    case FILE_RESULT_OFFLINE:
        status = QStringLiteral("文件“%1”：发送失败，对方当前不在线").arg(prepared.fileName);
        break;

    case FILE_RESULT_INVALID:
        status = QStringLiteral("文件“%1”：文件信息不符合要求，请重新选择").arg(prepared.fileName);
        break;

    case FILE_RESULT_BUSY:
        status = QStringLiteral("文件“%1”：文件任务过多，请稍后再试").arg(prepared.fileName);
        break;

    case FILE_RESULT_FAIL:
        status = QStringLiteral("文件“%1”：申请失败，请稍后重试").arg(prepared.fileName);
        break;

    default:
        qWarning() << "未知的文件申请回复结果：" << reply.result;
        return;
    }

    m_pendingFileOffers.erase(it);

    if (reply.result == FILE_RESULT_SUCC) {
        auto early = m_earlyFileEndNotifications.find(reply.transferId);

        if (early != m_earlyFileEndNotifications.end()) {
            PROT_FILE_END_NOTIFY notification = early->second;

            m_earlyFileEndNotifications.erase(early);

            deal_prot_file_end_notify(reinterpret_cast<char*>(&notification),static_cast<int>(sizeof(notification)),0);

            // 已结束，就不再显示“等待对方接受”。
            return;
        }
    }

    // 现有私聊接口按富文本显示，统一转义整条提示。
    m_pMainWidget->setChatMsg(prepared.targetUserId,status.toHtmlEscaped());
}

void Kernel::deal_prot_file_end_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_END_NOTIFY))) {
        return;
    }

    PROT_FILE_END_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.transferId == 0 || notification.peerUserId <= 0) {
        qWarning() << "无效的文件结束通知";
        return;
    }

    QString reason;

    switch (notification.result) {
    case FILE_RESULT_OFFLINE:
        reason = "对方已断开连接，文件任务已结束";
        break;

    case FILE_RESULT_EXPIRED:
        reason = "等待超时，文件任务已结束";
        break;

    case FILE_RESULT_REJECTED:
        reason = "对方拒绝接收";
        break;

    case FILE_RESULT_CANCELLED:
        reason = "文件传输已取消";
        break;

    case FILE_RESULT_HASH_MISMATCH:
        reason = "文件校验失败";
        break;

    case FILE_RESULT_FAIL:
        reason = "文件任务失败";
        break;

    case FILE_RESULT_SUCC:
        reason = "对方已校验并保存，发送完成";
        break;

    case FILE_RESULT_INVALID:
        reason = "文件传输数据或顺序异常，任务已结束";
        break;

    case FILE_RESULT_NOT_FRIEND:
        reason = "好友关系已解除，文件任务已结束";
        break;

    default:
        qWarning() << "未知的文件结束结果：" << notification.result;
        return;
    }

    auto incoming = m_incomingFileTransfers.find(notification.transferId);

    if (incoming != m_incomingFileTransfers.end()) {
        auto& transfer = incoming->second;

        if (transfer.offer.senderId != notification.peerUserId) {
            qWarning() << "文件结束通知的发送者不匹配";
            return;
        }

        // 服务端说成功，本地也必须确实保存成功。
        if (notification.result == FILE_RESULT_SUCC &&
            (!transfer.locallyCommitted || !transfer.finishResultSubmitted)) {
            qWarning() << "本地文件尚未保存，不能确认接收成功";
            return;
        }

        const QString fileName = transfer.fileName;
        QString incomingReason;

        if (transfer.locallyCommitted) {
            incomingReason = QStringLiteral("已校验并保存至：%1").arg(QDir::toNativeSeparators(transfer.savePath));

            if (notification.result != FILE_RESULT_SUCC) {
                incomingReason += QStringLiteral("；最终确认未完成，但本地文件已保存");
            }
        }
        else if (notification.result == FILE_RESULT_REJECTED) {
            incomingReason = "已拒绝接收";
        }
        else {
            incomingReason = reason;
        }

        if (!transfer.locallyCommitted && transfer.outputFile) {
            transfer.outputFile->cancelWriting();
        }

        m_incomingFileTransfers.erase(incoming);
        m_pMainWidget->setChatMsg(notification.peerUserId,QStringLiteral("文件“%1”：%2")
                                                               .arg(fileName, incomingReason).toHtmlEscaped());

        return;
    }

    auto it = m_outgoingFileTransfers.find(notification.transferId);

    if (it == m_outgoingFileTransfers.end()) {
        // 申请确认尚未到达时，短暂保留结束通知。
        if (!m_pendingFileOffers.empty() &&
            m_earlyFileEndNotifications.count(notification.transferId) == 0 &&
            m_earlyFileEndNotifications.size() < 16) {
            const std::uint32_t transferId = notification.transferId;

            m_earlyFileEndNotifications.emplace(transferId,notification);

            QTimer::singleShot(15000,this,
                               [this, transferId] {
                                   m_earlyFileEndNotifications.erase(transferId);
                               });
        }
        else {
            qWarning() << "未找到对应的本地文件任务：" << notification.transferId;
        }

        return;
    }

    if (it->second.file.targetUserId != notification.peerUserId) {
        qWarning() << "文件结束通知的好友身份不匹配";
        return;
    }

    if (notification.result == FILE_RESULT_SUCC) {
        const auto& transfer = it->second;

        if (transfer.state != OutgoingFileState::WaitingForCompletion ||
            transfer.file.fileSize < 0 ||
            transfer.confirmedBytes != static_cast<std::uint64_t>(transfer.file.fileSize)) {
            qWarning() << "文件发送尚未完成，不能确认成功";
            return;
        }
    }

    const PreparedOutgoingFile file = it->second.file;

    // 先清理，再显示结果，释放任务名额。
    m_outgoingFileTransfers.erase(it);
    m_earlyFileEndNotifications.erase(notification.transferId);

    const QString status = QStringLiteral("文件“%1”：%2").arg(file.fileName, reason);

    m_pMainWidget->setChatMsg(file.targetUserId,status.toHtmlEscaped());

    qDebug() << "本地文件任务已清理，传输编号：" << notification.transferId;
}

void Kernel::deal_prot_file_offer_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_OFFER_NOTIFY))) {
        return;
    }

    PROT_FILE_OFFER_NOTIFY offer{};
    std::memcpy(&offer, pbuf, sizeof(offer));

    if (offer.transferId == 0 ||
        offer.senderId <= 0 ||
        offer.senderId == m_pMainWidget->getUserId() ||
        offer.fileSize > MAX_TRANSFER_FILE_BYTES ||
        m_incomingFileTransfers.count(offer.transferId) != 0 ||
        m_incomingFileTransfers.size() >= 4) {
        qWarning() << "文件申请字段无效、重复或任务过多";
        return;
    }

    const char* nameEnd = static_cast<const char*>(std::memchr(offer.fileName, '\0', sizeof(offer.fileName)));

    const char* nickEnd = static_cast<const char*>(std::memchr(offer.senderNick, '\0', sizeof(offer.senderNick)));

    if (!nameEnd || nameEnd == offer.fileName ||
        !nickEnd || nickEnd == offer.senderNick) {
        qWarning() << "文件申请名称字段无效";
        return;
    }

    const QByteArray nameBytes(offer.fileName,static_cast<int>(nameEnd - offer.fileName));
    const QByteArray nickBytes(offer.senderNick,static_cast<int>(nickEnd - offer.senderNick));

    const QString fileName = QString::fromUtf8(nameBytes);
    const QString nick = QString::fromUtf8(nickBytes);

    if (fileName.toUtf8() != nameBytes ||
        nick.toUtf8() != nickBytes ||
        fileName == "." ||
        fileName == ".." ||
        fileName.endsWith('.') ||
        fileName.endsWith(' ')) {
        qWarning() << "文件名或昵称编码不合法";
        return;
    }

    for (QChar ch : fileName) {
        if (ch.unicode() < 32 ||
            ch.unicode() == 127 ||
            QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            qWarning() << "文件名包含不支持的字符";
            return;
        }
    }

    IncomingFileTransfer transfer;
    transfer.offer = offer;
    transfer.fileName = fileName;

    // 先记录，再弹窗，弹窗期间可能收到任务结束通知。
    m_incomingFileTransfers.emplace(offer.transferId,std::move(transfer));

    QString sizeText;

    if (offer.fileSize < 1024) {
        sizeText = QStringLiteral("%1 字节").arg(static_cast<qulonglong>(offer.fileSize));
    }
    else {
        const bool useMiB = offer.fileSize >= 1024ULL * 1024;

        const double divisor = useMiB ? 1024.0 * 1024.0 : 1024.0;

        sizeText = QStringLiteral("%1 %2（%3 字节）").arg(static_cast<double>(offer.fileSize) / divisor,0,'f',2)
                       .arg(useMiB ? QStringLiteral("MiB") : QStringLiteral("KiB"))
                       .arg(static_cast<qulonglong>(offer.fileSize));
    }

    QMessageBox dialog(m_pMainWidget);
    dialog.setWindowTitle("收到文件");
    dialog.setIcon(QMessageBox::Question);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("【%1】想发送文件给你：\n\n%2\n大小：%3\n\n是否接收？").arg(nick, fileName, sizeText));
    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dialog.setDefaultButton(QMessageBox::No);

    const bool accepted = dialog.exec() == QMessageBox::Yes;

    if (m_incomingFileTransfers.count(offer.transferId) == 0) {
        return;
    }

    QString savePath;

    if (accepted) {
        savePath = QFileDialog::getSaveFileName(m_pMainWidget,"选择文件保存位置",QDir::home().filePath(fileName));
    }

    // 保存窗口运行期间，也可能收到结束通知。
    auto it = m_incomingFileTransfers.find(offer.transferId);

    if (it == m_incomingFileTransfers.end()) {
        return;
    }

    bool willReceive = accepted && !savePath.isEmpty();

    if (willReceive) {
        auto outputFile = std::make_unique<QSaveFile>(savePath);

        // 无法建立临时文件时，不允许退回直接覆盖目标文件。
        outputFile->setDirectWriteFallback(false);

        if (!outputFile->open(QIODevice::WriteOnly)) {
            willReceive = false;

            QMessageBox::warning(m_pMainWidget,"接收文件","无法在所选位置创建临时文件，本次接收已取消，请重新申请");
        }
        else {
            //空文件也会创建这个摘要对象，后面可以计算空文件的正确摘要
            it->second.savePath = savePath;
            it->second.outputFile = std::move(outputFile);
            it->second.receivedBytes = 0;
            it->second.writeFailed = false;
            it->second.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        }
    }

    qDebug() << "文件接收决定，传输编号：" << offer.transferId << "是否接受：" << willReceive;

    sendFileDecision(offer.transferId,willReceive);
}

void Kernel::sendFileDecision(std::uint32_t transferId,bool accepted) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0) {
        return;
    }

    auto it = m_incomingFileTransfers.find(transferId);

    if (it == m_incomingFileTransfers.end()) {
        return;
    }

    auto& transfer = it->second;

    // 同一任务只提交一次决定。
    if (transfer.decisionRequestId != 0) {
        return;
    }

    if (accepted && (transfer.savePath.isEmpty() || !transfer.outputFile || !transfer.outputFile->isOpen())) {
        qWarning() << "接收文件尚未准备好，不能接受传输";
        return;
    }

    std::uint32_t requestId = 0;

    // 只需避开当前仍在使用的决定请求编号。
    while (true) {
        requestId = m_nextFileDecisionRequestId++;

        if (requestId == 0) {
            continue;
        }

        bool used = false;

        for (const auto& entry : m_incomingFileTransfers) {
            if (entry.second.decisionRequestId == requestId) {
                used = true;
                break;
            }
        }

        if (!used) {
            break;
        }
    }

    PROT_FILE_DECISION_RQ request{};
    request.requestId = requestId;
    request.transferId = transferId;
    request.decision = accepted ? FILE_DECISION_ACCEPT : FILE_DECISION_REJECT;

    // 先保存状态，再发送。
    transfer.decisionRequestId = requestId;
    transfer.accepted = accepted;

    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    qDebug() << "文件接收决定已提交，传输编号：" << transferId << "请求编号：" << requestId << "是否接受：" << accepted;
}

void Kernel::deal_prot_file_decision_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_DECISION_RS))) {
        return;
    }

    PROT_FILE_DECISION_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_incomingFileTransfers.find(reply.transferId);

    if (it == m_incomingFileTransfers.end()) {
        qDebug() << "文件任务已经结束，忽略决定回复";
        return;
    }

    auto& transfer = it->second;

    if (reply.requestId == 0 || transfer.decisionRequestId != reply.requestId) {
        qWarning() << "文件决定回复与原请求不匹配";
        return;
    }

    switch (reply.result) {
        case FILE_RESULT_SUCC:
            if (!transfer.accepted) {
                qWarning() << "拒绝操作收到异常的接受确认";
                return;
            }

            transfer.decisionConfirmed = true;

            // 后续在这里衔接文件接收准备。
            qDebug() << "接收决定已确认，传输编号：" << reply.transferId;
            return;

        case FILE_RESULT_REJECTED:
            if (transfer.accepted) {
                qWarning() << "接受操作收到异常的拒绝确认";
                return;
            }

            transfer.decisionConfirmed = true;

            // 紧接着的结束通知负责清理和显示，避免重复提示。
            return;

        case FILE_RESULT_OFFLINE:
        case FILE_RESULT_FAIL:
            // 服务端会随后发送结束通知，统一在那里清理。
            transfer.decisionConfirmed = true;
            return;

        case FILE_RESULT_EXPIRED: {
            // 服务端可能已没有该任务，因此不一定再发结束通知。
            const int senderId = transfer.offer.senderId;
            const QString fileName = transfer.fileName;

            m_incomingFileTransfers.erase(it);
            m_pMainWidget->setChatMsg(senderId,QStringLiteral("文件“%1”：申请已失效，请让对方重新发送").arg(fileName).toHtmlEscaped());
            return;
        }

        default:
            qWarning() << "未知的文件决定回复结果：" << reply.result;
            return;
    }
}

void Kernel::deal_prot_file_start_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_START_NOTIFY))) {
        return;
    }

    PROT_FILE_START_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.transferId == 0 || notification.targetUserId <= 0) {
        qWarning() << "无效的文件开始通知";
        return;
    }

    auto it = m_outgoingFileTransfers.find(notification.transferId);

    if (it == m_outgoingFileTransfers.end()) {
        qWarning() << "文件开始通知没有对应的本地任务";
        return;
    }

    auto& transfer = it->second;

    if (transfer.file.targetUserId != notification.targetUserId ||
        transfer.state != OutgoingFileState::WaitingForDecision) {
        qWarning() << "文件开始通知的身份或状态不匹配";
        return;
    }

    auto failPreparation = [&](const QString& reason) {
        transfer.inputFile.reset();
        transfer.state = OutgoingFileState::FailedWaitingForCleanup;

        m_pMainWidget->setChatMsg(transfer.file.targetUserId,
            QStringLiteral("文件“%1”：%2").arg(transfer.file.fileName, reason).toHtmlEscaped());
    };

    auto inputFile = std::make_unique<QFile>(
        transfer.file.filePath
        );

    if (!inputFile->open(QIODevice::ReadOnly)) {
        failPreparation("无法重新读取文件，传输不能开始");
        return;
    }

    const QFileInfo info(*inputFile);

    if (!info.isFile() || inputFile->size() != transfer.file.fileSize) {
        failPreparation("文件已经发生变化，请重新选择后发送");
        return;
    }

    transfer.inputFile = std::move(inputFile);
    transfer.confirmedBytes = 0;
    transfer.waitingForChunkAck = false;
    transfer.state = OutgoingFileState::Sending;

    m_pMainWidget->setChatMsg(transfer.file.targetUserId,
                              QStringLiteral("文件“%1”：对方已接受，准备开始传输").arg(transfer.file.fileName).toHtmlEscaped());

    qDebug() << "文件读取已准备好，传输编号：" << notification.transferId << "文件大小：" << transfer.file.fileSize;

    sendNextFileChunk(notification.transferId);
}

void Kernel::sendNextFileChunk(std::uint32_t transferId) {
    auto it = m_outgoingFileTransfers.find(transferId);

    if (it == m_outgoingFileTransfers.end()) {
        return;
    }

    auto& transfer = it->second;

    if (transfer.state != OutgoingFileState::Sending || transfer.waitingForChunkAck) {
        return;
    }

    auto failReading = [&](const QString& reason) {
        transfer.inputFile.reset();
        transfer.state = OutgoingFileState::FailedWaitingForCleanup;

        m_pMainWidget->setChatMsg(transfer.file.targetUserId,
            QStringLiteral("文件“%1”：%2").arg(transfer.file.fileName, reason).toHtmlEscaped());
    };

    if (!transfer.inputFile || !transfer.inputFile->isOpen() || transfer.file.fileSize < 0) {
        failReading("文件读取状态异常，已停止发送");
        return;
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(transfer.file.fileSize);

    if (transfer.confirmedBytes > fileSize) {
        failReading("文件传输进度异常，已停止发送");
        return;
    }

    if (transfer.inputFile->size() != transfer.file.fileSize) {
        failReading("文件大小发生变化，已停止发送");
        return;
    }

    if (transfer.confirmedBytes == fileSize) {
        transfer.state = OutgoingFileState::WaitingForCompletion;

        // 数据已经全部读取并得到确认，释放读取句柄。
        transfer.inputFile.reset();

        PROT_FILE_FINISH_RQ request{};
        request.transferId = transferId;

        m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);
        m_pMainWidget->setChatMsg(transfer.file.targetUserId,QStringLiteral("文件“%1”：数据已传输，等待对方校验并保存")
                                                                  .arg(transfer.file.fileName).toHtmlEscaped());

        qDebug() << "文件数据结束请求已发送，传输编号："<< transferId;
        return;
    }

    const std::uint64_t remaining = fileSize - transfer.confirmedBytes;

    const std::uint32_t chunkLength = static_cast<std::uint32_t>(
            remaining <static_cast<std::uint64_t>(FILE_CHUNK_BYTES) ? remaining : static_cast<std::uint64_t>(FILE_CHUNK_BYTES));

    if (!transfer.inputFile->seek(static_cast<qint64>(transfer.confirmedBytes))) {
        failReading("无法定位文件读取位置");
        return;
    }

    const QByteArray chunk = transfer.inputFile->read(static_cast<qint64>(chunkLength));

    if (transfer.inputFile->error() != QFileDevice::NoError || chunk.size() != static_cast<int>(chunkLength)) {
        failReading("读取文件失败，已停止发送");
        return;
    }

    PROT_FILE_CHUNK_RQ request{};
    request.transferId = transferId;
    request.offset = transfer.confirmedBytes;
    request.dataLength = chunkLength;

    std::memcpy(request.data,chunk.constData(),chunkLength);

    // 先记录等待状态，收到匹配确认后才能继续下一块。
    transfer.pendingOffset = request.offset;
    transfer.pendingLength = request.dataLength;
    transfer.waitingForChunkAck = true;

    m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);
}

void Kernel::deal_prot_file_chunk_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 ||
        len != static_cast<int>(sizeof(PROT_FILE_CHUNK_NOTIFY))) {
        return;
    }

    PROT_FILE_CHUNK_NOTIFY notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.transferId == 0) {
        return;
    }

    auto it = m_incomingFileTransfers.find(notification.transferId);

    if (it == m_incomingFileTransfers.end()) {
        qWarning() << "忽略已结束文件任务的数据块";
        return;
    }

    auto& transfer = it->second;

    auto sendAck = [&](std::int32_t code) {
        PROT_FILE_CHUNK_ACK ack{};
        ack.transferId = notification.transferId;
        ack.offset = notification.offset;
        ack.dataLength = notification.dataLength;
        ack.result = code;

        m_pMediator->sendData(reinterpret_cast<char*>(&ack),sizeof(ack),0);
    };

    auto failWriting = [&](std::int32_t code) {
        transfer.writeFailed = true;

        if (transfer.outputFile) {
            transfer.outputFile->cancelWriting();
        }

        // 服务端收到失败确认后结束任务并通知双方。
        sendAck(code);
    };

    if (!transfer.accepted || !transfer.decisionConfirmed || transfer.writeFailed ||
        !transfer.outputFile || !transfer.outputFile->isOpen() || !transfer.hash) {
        failWriting(FILE_RESULT_FAIL);
        return;
    }

    const std::uint64_t fileSize = transfer.offer.fileSize;

    if (transfer.receivedBytes > fileSize ||
        notification.offset != transfer.receivedBytes ||
        notification.dataLength == 0 ||
        notification.dataLength > FILE_CHUNK_BYTES) {
        failWriting(FILE_RESULT_INVALID);
        return;
    }

    const std::uint64_t remaining = fileSize - transfer.receivedBytes;

    if (notification.dataLength > remaining) {
        failWriting(FILE_RESULT_INVALID);
        return;
    }

    // 核对临时文件当前位置，防止误覆盖已经写入的数据。
    if (transfer.outputFile->pos() != static_cast<qint64>(transfer.receivedBytes)) {
        failWriting(FILE_RESULT_FAIL);
        return;
    }

    const char* data = reinterpret_cast<const char*>(notification.data);

    const qint64 written = transfer.outputFile->write(data,static_cast<qint64>(notification.dataLength));

    if (written != static_cast<qint64>(notification.dataLength)) {
        qWarning() << "写入接收文件失败：" << transfer.outputFile->errorString();
        failWriting(FILE_RESULT_FAIL);
        return;
    }

    transfer.hash->addData(data,static_cast<int>(notification.dataLength));
    transfer.receivedBytes += notification.dataLength;

    // 只有完整写入这一块后，才回复成功。
    sendAck(FILE_RESULT_SUCC);
}

void Kernel::deal_prot_file_chunk_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 ||
        len != static_cast<int>(sizeof(PROT_FILE_CHUNK_RS))) {
        return;
    }

    PROT_FILE_CHUNK_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_outgoingFileTransfers.find(reply.transferId);

    if (it == m_outgoingFileTransfers.end()) {
        qDebug() << "忽略已结束文件任务的块确认";
        return;
    }

    auto& transfer = it->second;

    auto stopSending = [&](const QString& reason) {
        transfer.inputFile.reset();
        transfer.state = OutgoingFileState::FailedWaitingForCleanup;

        m_pMainWidget->setChatMsg(transfer.file.targetUserId,
                                  QStringLiteral("文件“%1”：%2").arg(transfer.file.fileName, reason).toHtmlEscaped());
    };

    if (transfer.state != OutgoingFileState::Sending || !transfer.waitingForChunkAck) {
        qWarning() << "文件块确认到达时任务状态不匹配";
        return;
    }

    if (reply.offset != transfer.pendingOffset || reply.dataLength != transfer.pendingLength || reply.dataLength == 0) {
        stopSending("数据块确认不匹配，已停止发送");
        return;
    }

    if (reply.result != FILE_RESULT_SUCC) {
        stopSending("对方未能写入文件，已停止发送");
        return;
    }

    if (transfer.file.fileSize < 0) {
        stopSending("本地文件信息异常，已停止发送");
        return;
    }

    const std::uint64_t fileSize = static_cast<std::uint64_t>(transfer.file.fileSize);

    if (transfer.confirmedBytes > fileSize ||
        reply.offset != transfer.confirmedBytes ||
        reply.dataLength > fileSize - transfer.confirmedBytes) {
        stopSending("文件确认进度异常，已停止发送");
        return;
    }

    // 确认成功，才推进累计字节数。
    transfer.confirmedBytes += reply.dataLength;
    transfer.pendingOffset = 0;
    transfer.pendingLength = 0;
    transfer.waitingForChunkAck = false;

    const int percent = fileSize == 0 ? 100 : static_cast<int>(transfer.confirmedBytes * 100 / fileSize);
    qDebug() << "文件数据传输进度，编号：" << reply.transferId << "已确认字节：" << static_cast<qulonglong>(transfer.confirmedBytes) << "进度：" << percent << "%";
    const std::uint32_t transferId = reply.transferId;

    // 下一轮事件循环再发送，避免连续调用堆叠。
    QTimer::singleShot(0, this, [this, transferId] {sendNextFileChunk(transferId);});
}

void Kernel::deal_prot_file_finish_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_FILE_FINISH_NOTIFY))) {
        return;
    }

    PROT_FILE_FINISH_NOTIFY notification{};
    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.transferId == 0) {
        return;
    }

    auto it = m_incomingFileTransfers.find(notification.transferId);

    if (it == m_incomingFileTransfers.end()) {
        qWarning() << "最终校验通知没有对应的接收任务";
        return;
    }

    auto& transfer = it->second;

    // 同一任务不能重复提交保存或回复结果。
    if (transfer.finishResultSubmitted) {
        return;
    }

    auto sendResult = [&](std::int32_t code) {
        PROT_FILE_FINISH_RS reply{};
        reply.transferId = notification.transferId;
        reply.result = code;

        transfer.finishResultSubmitted = true;
        m_pMediator->sendData(reinterpret_cast<char*>(&reply),sizeof(reply),0);
    };

    auto failReceiving = [&](std::int32_t code) {
        transfer.writeFailed = true;

        if (transfer.outputFile) {
            transfer.outputFile->cancelWriting();
        }

        transfer.outputFile.reset();
        transfer.hash.reset();

        sendResult(code);
    };

    if (!transfer.accepted || !transfer.decisionConfirmed || transfer.writeFailed ||
        !transfer.outputFile || !transfer.outputFile->isOpen() || !transfer.hash) {
        failReceiving(FILE_RESULT_FAIL);
        return;
    }

    if (transfer.receivedBytes != transfer.offer.fileSize ||
        transfer.outputFile->size() != static_cast<qint64>(transfer.offer.fileSize)) {
        failReceiving(FILE_RESULT_INVALID);
        return;
    }

    const QByteArray actualHash = transfer.hash->result();
    const QByteArray expectedHash(reinterpret_cast<const char*>(transfer.offer.sha256),static_cast<int>(FILE_SHA256_BYTES));

    if (actualHash.size() != static_cast<int>(FILE_SHA256_BYTES) || actualHash != expectedHash) {
        qWarning() << "文件 SHA-256 校验失败，编号：" << notification.transferId;

        failReceiving(FILE_RESULT_HASH_MISMATCH);
        return;
    }

    // 校验通过之后，才把临时文件提交为目标文件。
    if (!transfer.outputFile->commit()) {
        qWarning() << "保存接收文件失败：" << transfer.outputFile->errorString();
        failReceiving(FILE_RESULT_FAIL);
        return;
    }

    transfer.locallyCommitted = true;
    transfer.outputFile.reset();
    transfer.hash.reset();

    qDebug() << "文件校验并保存成功，编号：" << notification.transferId;
    sendResult(FILE_RESULT_SUCC);
}

void Kernel::slots_deleteFriend(int friendId) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 ||
        friendId <= 0 || friendId == m_pMainWidget->getUserId()) {
        return;
    }

    for (const auto& entry : m_pendingFriendDeletions) {
        if (entry.second.friendId == friendId) {
            QMessageBox::information(m_pMainWidget,"删除好友","正在处理删除请求，请稍候");
            return;
        }
    }

    if (m_pendingFriendDeletions.size() >= 16) {
        QMessageBox::information(m_pMainWidget,"删除好友","等待处理的请求过多，请稍后再试");
        return;
    }

    std::uint32_t requestId = 0;

    do {
        requestId = m_nextDeleteFriendRequestId++;
    } while (requestId == 0 || m_pendingFriendDeletions.count(requestId) != 0);

    PendingFriendDeletion pending;
    pending.friendId = friendId;
    pending.nick = m_pMainWidget->friendDisplayName(friendId);

    PROT_DELETE_FRIEND_RQ request{};
    request.requestId = requestId;
    request.friendId = friendId;

    m_pendingFriendDeletions.emplace(requestId,std::move(pending));

    if (!m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0)) {
        m_pendingFriendDeletions.erase(requestId);

        QMessageBox::warning(m_pMainWidget,"删除好友","请求发送失败，无法确定服务端是否收到，请重新登录确认好友关系");
        return;
    }

    QTimer::singleShot(10000, this, [this, requestId] {
        auto it = m_pendingFriendDeletions.find(requestId);

        if (it == m_pendingFriendDeletions.end()) {
            return;
        }

        m_pendingFriendDeletions.erase(it);

        QMessageBox::warning(m_pMainWidget,"删除好友","删除结果确认超时，请重新登录确认好友关系");
    });
}

void Kernel::deal_prot_delete_friend_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_DELETE_FRIEND_RS))) {
        return;
    }

    PROT_DELETE_FRIEND_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingFriendDeletions.find(reply.requestId);

    if (it == m_pendingFriendDeletions.end()) {
        qWarning() << "删除好友回复已超时或没有对应请求";
        return;
    }

    if (reply.friendId != it->second.friendId) {
        qWarning() << "删除好友回复身份不匹配";
        return;
    }

    QString message;

    switch (reply.result) {
    case DELETE_FRIEND_SUCC:
        message = QStringLiteral("已删除好友【%1】").arg(it->second.nick);
        break;

    case DELETE_FRIEND_NOT_FRIEND:
        message = QStringLiteral("你与【%1】已不是好友，列表已更新").arg(it->second.nick);
        break;

    case DELETE_FRIEND_FAIL:
        message = QStringLiteral("删除好友失败，请稍后再试");
        break;

    default:
        qWarning() << "未知的删除好友结果";
        return;
    }

    m_pendingFriendDeletions.erase(it);

    if (reply.result == DELETE_FRIEND_SUCC || reply.result == DELETE_FRIEND_NOT_FRIEND) {
        m_pMainWidget->removeFriend(reply.friendId);
    }

    QMessageBox dialog(m_pMainWidget);
    dialog.setWindowTitle("删除好友");
    dialog.setIcon(QMessageBox::Information);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(message);
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}

void Kernel::deal_prot_delete_friend_notify(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 ||
        len != static_cast<int>(sizeof(PROT_DELETE_FRIEND_NOTIFY))) {
        return;
    }

    PROT_DELETE_FRIEND_NOTIFY notification{};
    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.friendId <= 0 || notification.friendId == m_pMainWidget->getUserId()) {
        return;
    }

    // 移除前先取昵称。
    const QString nick = m_pMainWidget->friendDisplayName(notification.friendId);

    m_pMainWidget->removeFriend(notification.friendId);

    // 双方同时操作时，清理对同一好友的待处理删除请求。
    for (auto it = m_pendingFriendDeletions.begin(); it != m_pendingFriendDeletions.end();) {
        if (it->second.friendId == notification.friendId) {
            it = m_pendingFriendDeletions.erase(it);
        }
        else {
            ++it;
        }
    }

    QMessageBox dialog(m_pMainWidget);
    dialog.setWindowTitle("好友关系变更");
    dialog.setIcon(QMessageBox::Information);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("【%1】已解除与你的好友关系，好友列表已更新").arg(nick));
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}

void Kernel::slots_leaveGroup(int groupId) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || groupId <= 0) {
        return;
    }

    for (const auto& entry : m_pendingGroupLeaves) {
        if (entry.second.groupId == groupId) {
            QMessageBox::information(m_pMainWidget,"退出群聊","正在处理退群请求，请稍候");
            return;
        }
    }

    if (m_pendingGroupLeaves.size() >= 16) {
        QMessageBox::information(m_pMainWidget,"退出群聊","等待处理的请求过多，请稍后再试");
        return;
    }

    std::uint32_t requestId = 0;

    do {
        requestId = m_nextGroupLeaveRequestId++;
    } while (requestId == 0 || m_pendingGroupLeaves.count(requestId) != 0);

    PendingGroupLeave pending;
    pending.groupId = groupId;
    pending.groupName = m_pMainWidget->groupDisplayName(groupId);

    PROT_GROUP_LEAVE_RQ request{};
    request.requestId = requestId;
    request.groupId = groupId;

    m_pendingGroupLeaves.emplace(requestId,std::move(pending));

    if (!m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0)) {
        m_pendingGroupLeaves.erase(requestId);
        QMessageBox::warning(m_pMainWidget,"退出群聊","请求发送失败，无法确定服务端是否收到，请重新登录确认群列表");
        return;
    }

    QTimer::singleShot(10000, this, [this, requestId] {
        auto it = m_pendingGroupLeaves.find(requestId);

        if (it == m_pendingGroupLeaves.end()) {
            return;
        }

        m_pendingGroupLeaves.erase(it);
        QMessageBox::warning(m_pMainWidget,"退出群聊","退群结果确认超时，请重新登录确认群列表");
    });
}

void Kernel::deal_prot_group_leave_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || len != static_cast<int>(sizeof(PROT_GROUP_LEAVE_RS))) {
        return;
    }

    PROT_GROUP_LEAVE_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    auto it = m_pendingGroupLeaves.find(reply.requestId);

    if (it == m_pendingGroupLeaves.end()) {
        qWarning() << "退群回复已超时或没有对应请求";
        return;
    }

    if (reply.groupId != it->second.groupId) {
        qWarning() << "退群回复与原请求不匹配";
        return;
    }

    const QString groupName = it->second.groupName;

    QString message;
    bool removeLocalGroup = false;

    switch (reply.result) {
    case GROUP_LEAVE_SUCC:
        message = QStringLiteral("已退出“%1”").arg(groupName);
        removeLocalGroup = true;
        break;

    case GROUP_LEAVE_NOT_MEMBER:
        message = QStringLiteral("你已不是“%1”的成员，群列表已更新").arg(groupName);
        removeLocalGroup = true;
        break;

    case GROUP_LEAVE_NOT_FOUND:
        message = QStringLiteral("“%1”已不存在，群列表已更新").arg(groupName);
        removeLocalGroup = true;
        break;

    case GROUP_LEAVE_OWNER:
        message = "你是群主，当前不支持群主直接退出";
        break;

    case GROUP_LEAVE_FAIL:
        message = "退群失败，请稍后再试";
        break;

    default:
        qWarning() << "未知的退群结果";
        return;
    }

    m_pendingGroupLeaves.erase(it);

    if (removeLocalGroup) {
        // 旧查询回复不再更新已经退出的群。
        m_pendingMemberQueries.erase(reply.groupId);

        // 清理本地尚未确认的该群消息。
        for (auto pending = m_pendingGroupMessages.begin();
             pending != m_pendingGroupMessages.end();) {
            if (pending->second.groupId == reply.groupId) {
                pending = m_pendingGroupMessages.erase(pending);
            }
            else {
                ++pending;
            }
        }

        m_pMainWidget->removeGroup(reply.groupId);
    }

    QMessageBox dialog(m_pMainWidget);
    dialog.setWindowTitle("退出群聊");
    dialog.setIcon(QMessageBox::Information);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(message);
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}

void Kernel::deal_prot_group_member_left(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 ||
        len != static_cast<int>(sizeof(PROT_GROUP_MEMBER_LEFT))) {
        return;
    }

    PROT_GROUP_MEMBER_LEFT notification{};

    std::memcpy(&notification,pbuf,sizeof(notification));

    if (notification.groupId <= 0 || notification.userId <= 0) {
        return;
    }

    // 让旧的在途查询失效，重新获取退群后的成员名单。
    m_pendingMemberQueries.erase(notification.groupId);
    m_pMainWidget->refreshOpenGroupMembers(notification.groupId);
}

void Kernel::slots_updateProfile(int iconId,const QString& nick,const QString& feeling) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0) {
        return;
    }

    if (!m_pendingProfileUpdates.empty()) {
        QMessageBox::information(m_pMainWidget,"修改资料","正在保存资料，请稍候");
        return;
    }

    if (iconId < 0 || iconId > 36 || nick.trimmed().isEmpty()) {
        QMessageBox::warning(m_pMainWidget,"修改资料","请选择有效头像并填写昵称");
        return;
    }

    PROT_UPDATE_PROFILE_RQ request{};

    if (!writeUtf8Field(request.nick, nick, "昵称") || !writeUtf8Field(request.feeling,feeling,"个性签名")) {
        return;
    }

    std::uint32_t requestId = m_nextProfileRequestId++;

    if (requestId == 0) {
        requestId = m_nextProfileRequestId++;
    }

    request.requestId = requestId;
    request.iconId = iconId;
    m_pendingProfileUpdates.emplace(requestId,PendingProfileUpdate{iconId, nick, feeling});

    if (!m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0)) {
        m_pendingProfileUpdates.erase(requestId);
        QMessageBox::warning(m_pMainWidget,"修改资料","请求发送失败，请重新登录确认资料是否更新");
        return;
    }

    QTimer::singleShot(10000, this, [this, requestId] {
        auto it = m_pendingProfileUpdates.find(requestId);
        if (it == m_pendingProfileUpdates.end()) {
            return;
        }
        m_pendingProfileUpdates.erase(it);
        if (m_pMainWidget) {
            QMessageBox::warning(m_pMainWidget,"修改资料","保存结果确认超时，请重新登录确认资料");
        }
    });
}

void Kernel::deal_prot_update_profile_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_UPDATE_PROFILE_RS))) {
        return;
    }

    PROT_UPDATE_PROFILE_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));
    auto it = m_pendingProfileUpdates.find(reply.requestId);

    if (it == m_pendingProfileUpdates.end()) {
        qWarning() << "资料修改回复已超时或没有对应请求";
        return;
    }

    QString message;

    switch (reply.result) {
    case UPDATE_PROFILE_SUCC: {
        if (reply.iconId < 0 || reply.iconId > 36 ||
            std::memchr(reply.nick, '\0', sizeof(reply.nick)) == nullptr ||
            std::memchr(reply.feeling, '\0', sizeof(reply.feeling)) == nullptr) {
            qWarning() << "服务端返回的资料字段不合法";
            return;
        }

        const QString nick = readUtf8Field(reply.nick);
        const QString feeling = readUtf8Field(reply.feeling);

        if (nick.isEmpty() || reply.iconId != it->second.iconId ||
            nick != it->second.nick || feeling != it->second.feeling) {
            qWarning() << "保存后的资料与本次请求不匹配";
            return;
        }

        m_pendingProfileUpdates.erase(it);
        m_pMainWidget->setMyInfo(reply.iconId,nick,feeling);
        m_pMainWidget->closeProfileEditor();
        QMessageBox::information(m_pMainWidget,"修改资料","个人资料已保存");
        return;
    }

    case UPDATE_PROFILE_NICK_EXISTS:
        message = "这个昵称已经有人使用，请换一个";
        break;

    case UPDATE_PROFILE_INVALID:
        message = "资料不符合要求，请检查昵称、签名和头像";
        break;

    case UPDATE_PROFILE_FAIL:
        message = "保存失败，请稍后再试";
        break;

    default:
        qWarning() << "未知的资料修改结果";
        return;
    }

    m_pendingProfileUpdates.erase(it);
    QMessageBox::warning(m_pMainWidget,"修改资料",message);
}

void Kernel::slots_changePassword(const QString& oldPassword,const QString& newPassword) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0) {
        return;
    }

    if (m_pendingPasswordRequestId != 0) {
        QMessageBox::information(m_pMainWidget,"修改密码","正在修改密码，请稍候");
        return;
    }

    const int oldLength = oldPassword.toUtf8().size();
    const int newLength = newPassword.toUtf8().size();

    if (oldLength == 0 || oldLength >= 15 ||
        newLength < 6 || newLength >= 15 ||
        oldPassword == newPassword) {
        QMessageBox::warning(m_pMainWidget,"修改密码","密码长度不符合要求，或新旧密码相同");
        return;
    }

    PROT_CHANGE_PASSWORD_RQ request{};

    if (!writeUtf8Field(request.oldPassword,oldPassword,"旧密码") ||
        !writeUtf8Field(request.newPassword,newPassword,"新密码")) {
        return;
    }

    std::uint32_t requestId = m_nextPasswordRequestId++;

    if (requestId == 0) {
        requestId = m_nextPasswordRequestId++;
    }

    request.requestId = requestId;
    m_pendingPasswordRequestId = requestId;

    const bool sent = m_pMediator->sendData(reinterpret_cast<char*>(&request),sizeof(request),0);

    // sendData() 返回后，清理这份请求中的密码数组。
    std::fill(std::begin(request.oldPassword),std::end(request.oldPassword),'\0');

    std::fill(std::begin(request.newPassword),std::end(request.newPassword),'\0');

    if (!sent) {
        m_pendingPasswordRequestId = 0;
        QMessageBox::warning(m_pMainWidget,"修改密码","请求发送失败，无法确定密码是否已修改，请重新登录确认");
        return;
    }

    QTimer::singleShot(10000, this, [this, requestId] {
        if (m_pendingPasswordRequestId != requestId) {
            return;
        }

        m_pendingPasswordRequestId = 0;

        if (m_pMainWidget) {
            QMessageBox::warning(m_pMainWidget,"修改密码","修改结果确认超时，请重新登录确认密码");
        }
    });
}

void Kernel::deal_prot_change_password_rs(char* pbuf,int len,unsigned long) {
    if (m_pMainWidget == nullptr || m_pMainWidget->getUserId() <= 0 || len != static_cast<int>(sizeof(PROT_CHANGE_PASSWORD_RS))) {
        return;
    }

    PROT_CHANGE_PASSWORD_RS reply{};
    std::memcpy(&reply, pbuf, sizeof(reply));

    if (reply.requestId == 0 || reply.requestId != m_pendingPasswordRequestId) {
        qWarning() << "修改密码回复已超时或没有对应请求";
        return;
    }

    QString message;
    bool success = false;

    switch (reply.result) {
    case CHANGE_PASSWORD_SUCC:
        message = "密码已修改，下次登录请使用新密码";
        success = true;
        break;

    case CHANGE_PASSWORD_OLD_WRONG:
        message = "旧密码不正确，请重新输入";
        break;

    case CHANGE_PASSWORD_INVALID:
        message = "密码格式不符合要求，新密码需要 6～14 个 UTF-8 字节";
        break;

    case CHANGE_PASSWORD_SAME:
        message = "新密码不能与旧密码相同";
        break;

    case CHANGE_PASSWORD_FAIL:
        message = "修改未确认成功，请重新登录确认密码后再试";
        break;

    default:
        qWarning() << "未知的修改密码结果";
        return;
    }

    m_pendingPasswordRequestId = 0;

    if (success) {
        m_pMainWidget->closePasswordEditor();

        QMessageBox::information(m_pMainWidget,"修改密码",message);
    }
    else {
        QMessageBox::warning(m_pMainWidget,"修改密码",message);
    }
}