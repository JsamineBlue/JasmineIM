#ifndef KERNEL_H
#define KERNEL_H
#include <QObject>
#include<QTimer>
#include"loginwidget.h"
#include"mainwidget.h"
#include"mediator/INetMediator.h"
#include"net/group_protocol.h"
#include"net/file_protocol.h"
#include <unordered_map>
#include <cstdint>
#include <QString>
#include <QByteArray>
#include <memory>
#include <QSaveFile>
#include <QFile>
#include <QCryptographicHash>

class Kernel : public QObject
{
    Q_OBJECT
public:
    explicit Kernel(QObject *parent = nullptr);
    ~Kernel() override;

    //类成员函数指针的别名
    using P_DEALFUNC = void (Kernel::*)(char*,int,unsigned long);

    //处理函数和预期长度的映射
    struct ProtocolHandler{
        int expectedLength;
        P_DEALFUNC function;
    };

    std::unordered_map<unsigned int, ProtocolHandler> m_handlers;

    //群聊请求编号
    std::uint32_t m_nextGroupRequestId = 1;

    // 请求编号 → 本次提交的群名
    std::unordered_map<std::uint32_t, QString> m_pendingGroupCreates;

public slots:
    // 先接收登录框的注册信息，再发送给服务端
    void slots_sendRegisterInfo(QString,QString,QString);

    void slots_sendloginInfo(QString,QString);

    // 接收服务端的数据
    void slots_recvServerData(char*,int,unsigned long);

    // 接收聊天界面的内容和朋友id
    void slots_sendServerMsg(int,QString);

    // 接收添加朋友的昵称，并将添加好友请求发送到服务端
    void slots_addFriend(QString);

    //收到下线信号后，执行下线操作并回收资源
    void slots_sendUserOffLine();

    //收到关闭登录窗口后，执行回收资源操作
    void slots_delete();

    //创建群槽函数
    void slots_createGroup(QString groupName);

    //收到加载群成员信息的信号后，向服务端发送查询请求
    void slots_requestGroupMembers(int groupId);

    //收到邀请好友的信号后，向服务端发送请求
    void slots_inviteGroupFriend(int groupId,int targetUserId);

    //收到发送群聊消息的信号后，向服务端发送请求
    void slots_sendGroupMessage(int groupId,const QString& message);

    //收到发送文件的信号后，向服务端发送请求
    void slots_sendFile(int friendId,const QString& filePath);

    //收到删除好友的信号后，向服务端发送请求
    void slots_deleteFriend(int friendId);

    //收到退出群聊的信号后，向服务端发送请求
    void slots_leaveGroup(int groupId);

    //收到修改信息的信号后，向服务端发送请求
    void slots_updateProfile(int iconId,const QString& nick,const QString& feeling);

    void slots_changePassword(const QString& oldPassword,const QString& newPassword);

    void slots_logout();

signals:
    // true：返回登录界面；false：退出程序。
    void signals_sessionFinished(bool returnToLogin);

public:
    LoginWidget * m_pLogin;     //登录页面
    MainWidget* m_pMainWidget;  //主聊天窗口
    INetMediator* m_pMediator;  //网络中介者类对象

    //群成员加载请求编号
    std::uint32_t m_nextMemberRequestId = 1;

    // 群ID → 正在等待的请求编号。
    std::unordered_map<int, std::uint32_t> m_pendingMemberQueries;

    // 一次尚未收到回复的邀请请求
    struct PendingGroupInvite {
        int groupId = 0;
        int targetUserId = 0;
    };

    // 邀请请求编号
    std::uint32_t m_nextInviteRequestId = 1;

    // 请求编号 -> 邀请信息
    std::unordered_map<std::uint32_t, PendingGroupInvite> m_pendingGroupInvites;

    // 邀请进群回复
    struct PendingGroupDecision {
        std::uint32_t invitationId = 0;
        std::int32_t groupId = 0;
        std::int32_t decision = GROUP_INVITE_DECISION_REJECT;
    };

    // 邀请进群回复编号
    std::uint32_t m_nextGroupDecisionRequestId = 1;

    // 回复编号 → 回复信息
    std::unordered_map<std::uint32_t, PendingGroupDecision> m_pendingGroupDecisions;

    // 构造并发送决定请求
    void sendGroupInviteDecision(std::uint32_t invitationId,std::int32_t groupId,std::int32_t decision);

    // 群聊信息消息体
    struct PendingGroupMessage {
        std::int32_t groupId = 0;
        QString message;
    };

    // 群聊信息发送请求编号
    std::uint32_t m_nextGroupMessageRequestId = 1;

    // 信息编号 → 信息消息体
    std::unordered_map<std::uint32_t, PendingGroupMessage> m_pendingGroupMessages;

    // 文件发送信息体
    struct PreparedOutgoingFile {
        int targetUserId = 0;

        QString filePath;
        QString fileName;
        qint64 fileSize = 0;

        QByteArray sha256;
        QString error;
    };

    // 暂时一次只准备一个文件，避免并行读取过多文件。
    bool m_preparingFile = false;

    void submitFileOffer(const PreparedOutgoingFile& prepared);

    // 请求编号 -> 已准备好的本地文件信息
    std::unordered_map<std::uint32_t, PreparedOutgoingFile> m_pendingFileOffers;

    std::uint32_t m_nextFileOfferRequestId = 1;

    enum class OutgoingFileState {
        WaitingForDecision,
        Sending,
        WaitingForCompletion,
        FailedWaitingForCleanup
    };

    struct OutgoingFileTransfer {
        PreparedOutgoingFile file;
        OutgoingFileState state =OutgoingFileState::WaitingForDecision;

        std::unique_ptr<QFile> inputFile;

        std::uint64_t confirmedBytes = 0;
        std::uint64_t pendingOffset = 0;
        std::uint32_t pendingLength = 0;
        bool waitingForChunkAck = false;
    };

    // 服务端传输编号 -> 本地发送任务
    std::unordered_map<std::uint32_t, OutgoingFileTransfer> m_outgoingFileTransfers;

    // 极少数情况下，结束通知可能先于申请确认到达。
    // 暂存它，等申请回复建立本地任务后再处理。
    std::unordered_map<std::uint32_t, PROT_FILE_END_NOTIFY> m_earlyFileEndNotifications;

    //接收文件传输任务信息体
    struct IncomingFileTransfer {
        PROT_FILE_OFFER_NOTIFY offer{};
        QString fileName;
        QString savePath;
        std::uint32_t decisionRequestId = 0;
        bool accepted = false;
        bool decisionConfirmed = false;
        std::unique_ptr<QSaveFile> outputFile;
        std::uint64_t receivedBytes = 0;
        bool writeFailed = false;

        std::unique_ptr<QCryptographicHash> hash;

        bool finishResultSubmitted = false;
        bool locallyCommitted = false;
    };

    std::unordered_map<std::uint32_t, IncomingFileTransfer> m_incomingFileTransfers;

    std::uint32_t m_nextFileDecisionRequestId = 1;
    void sendFileDecision(std::uint32_t transferId,bool accepted);

    //发送一个数据块函数
    void sendNextFileChunk(std::uint32_t transferId);

    //删除好友信息体
    struct PendingFriendDeletion {
        int friendId = 0;
        QString nick;
    };

    std::uint32_t m_nextDeleteFriendRequestId = 1;
    std::unordered_map<std::uint32_t, PendingFriendDeletion> m_pendingFriendDeletions;

    struct PendingGroupLeave {
        int groupId = 0;
        QString groupName;
    };

    std::uint32_t m_nextGroupLeaveRequestId = 1;

    std::unordered_map<std::uint32_t, PendingGroupLeave> m_pendingGroupLeaves;

    struct PendingProfileUpdate {
        int iconId = 0;
        QString nick;
        QString feeling;
    };

    std::uint32_t m_nextProfileRequestId = 1;

    std::unordered_map<std::uint32_t, PendingProfileUpdate> m_pendingProfileUpdates;

    std::uint32_t m_nextPasswordRequestId = 1;
    std::uint32_t m_pendingPasswordRequestId = 0;

    bool m_endingSession = false;

public: //处理服务端返回的数据
    void deal_prot_register_rs(char*,int,unsigned long);
    void deal_prot_login_rs(char*,int,unsigned long);
    void deal_prot_friend_info(char*,int,unsigned long);
    void deal_prot_chat_info_rs(char*,int,unsigned long);
    void deal_prot_chat_info_rq(char*,int,unsigned long);
    void deal_prot_add_friend_rs(char*,int,unsigned long);
    void deal_prot_add_friend_rq(char*,int,unsigned long);
    void deal_prot_friend_offline(char*,int,unsigned long);
    void deal_prot_group_create_rs(char*, int, unsigned long);  //处理创建群聊回复
    void deal_prot_group_info(char*, int, unsigned long);   //加载群聊信息
    void deal_prot_group_members_rs(char*, int, unsigned long); //加载群成员信息
    void deal_prot_group_invite_rs(char* pbuf,int len,unsigned long);   //处理邀请进群回复
    void deal_prot_group_invite_notify(char* pbuf,int len,unsigned long);   //处理接收到的群邀请
    void deal_prot_group_invite_decision_rs(char* pbuf,int len,unsigned long);  //处理接收到的群邀请并进行回复
    void deal_prot_group_invite_result_notify(char* pbuf,int len,unsigned long);    //处理收到的发出去群邀请的回复
    void deal_prot_group_chat_notify(char* pbuf,int len,unsigned long); //接收群聊消息
    void deal_prot_group_chat_rs(char* pbuf,int len,unsigned long); //接收发送群聊消息回复
    void deal_prot_file_offer_rs(char* pbuf,int len,unsigned long); //接收文件发送回复
    void deal_prot_file_end_notify(char* pbuf,int len,unsigned long);   //接收文件发送结束通知
    void deal_prot_file_offer_notify(char* pbuf,int len,unsigned long); //接收文件通知
    void deal_prot_file_decision_rs(char* pbuf,int len,unsigned long);  //发送端接收接收方是否接收文件的决定
    void deal_prot_file_start_notify(char* pbuf,int len,unsigned long); //发送端收到开始发送文件协议
    void deal_prot_file_chunk_notify(char* pbuf,int len,unsigned long); //接收方收到文件块
    void deal_prot_file_chunk_rs(char* pbuf,int len,unsigned long); //发送端收到接收方接收文件情况
    void deal_prot_file_finish_notify(char* pbuf,int len,unsigned long);    //接收端收到文件传输结束消息
    void deal_prot_delete_friend_rs(char* pbuf,int len,unsigned long);	//处理删除好友回复（删除方）
    void deal_prot_delete_friend_notify(char* pbuf,int len,unsigned long);	//处理删除好友通知（被删方）
    void deal_prot_group_leave_rs(char* pbuf,int len,unsigned long);    //处理退群回复（退群者）
    void deal_prot_group_member_left(char* pbuf,int len,unsigned long); //刷新群成员列表（其他成员）
    void deal_prot_update_profile_rs(char* pbuf,int len,unsigned long);
    void deal_prot_change_password_rs(char* pbuf,int len,unsigned long);
};

#endif // KERNEL_H
