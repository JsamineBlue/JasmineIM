#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <QWidget>
#include "frienditem.h"
#include "groupchatwidget.h"
#include "profiledialog.h"
#include "changepasswordwidget.h"
#include<map>
#include<QMenu>
#include <QListWidget>

using namespace std;

namespace Ui {
class MainWidget;
}

class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QWidget *parent = nullptr);
    ~MainWidget();

    void setMyInfo(int iconid,const QString& nick,const QString& feeling);
    void setFriendInfo(int friendid,int iconid,int status,const QString& nick,const QString& feeling);

    void setChatMsg(int,QString);
    void setFriendOffline(int);

public: //event handler
    virtual void closeEvent(QCloseEvent *event) override;
    void setGroupInfo(int groupId,const QString& groupName,int ownerId);
    void setGroupMembers(int groupId,const QVector<GroupMemberView>& members);  //群成员资料转交接口
    QString friendDisplayName(int userId) const;    //查询用户id
    QString groupDisplayName(int groupId) const;    //查询群id
    void refreshOpenGroupMembers(int groupId);      //刷新已打开的群窗口
    void appendGroupMessage(int groupId,const QString& senderName,const QString& message);  //转发显示接口
    void setGroupMessageResult(int groupId,const QString& message,const QString& status,bool success);  //发送群聊消息结果显示接口

public slots:
    void slots_showMenu();
    void slots_dealMenu(QAction*);
    void slots_openGroupChat(QListWidgetItem* item);
    void slots_chooseGroupInvitee(int groupId);     //收到邀请好友信号，打开可邀请好友列表
    void slots_requestDeleteFriend(int friendId);   //收到删除好友信号
    void slots_editProfile();
    void slots_openChangePassword();

signals:
    void signals_sendMsgAndIdToKernel(int,QString);
    void signals_addFriend(QString);
    void signals_userOffLine();     //通知 kernel 当前用户下线
    void signals_createGroup(QString groupName);    //创建群信号
    void signals_requestGroupMembers(int groupId);  //加载群成员列表信号
    void signals_inviteGroupFriend(int groupId,int targetUserId);   //通知 kernel 加载可邀请好友列表
    void signals_sendGroupMessage(int groupId,const QString& message);  //通知 kernel 发送群聊消息
    void signals_sendFileToKernel(int friendId,const QString& filePath);    //通知 kernel 文件传输
    void signals_deleteFriend(int friendId);    //通知 kernel 删除好友
    void signals_leaveGroup(int groupId);
    void signals_updateProfile(int iconId,const QString& nick,const QString& feeling);
    void signals_changePassword(const QString& oldPassword,const QString& newPassword);
    void signals_logoutRequested();

private:
    Ui::MainWidget *ui;
    int m_userid = 0;   //当前登录用户的id
    int m_iconId;   //头像id
    QString m_nick;     //昵称
    QString m_feeling;  //签名
    QMenu* m_pMenu;    //菜单
    map<int,FriendItem*> m_friendLstMap;
    std::map<int, QListWidgetItem*> m_groupItems;
    std::map<int, GroupChatWidget*> m_groupWindows; //群聊聊天窗口
    ProfileDialog* m_profileDialog = nullptr;

    int m_currentProfileIconId = 0;
    QString m_currentProfileFeeling;
    ChangePasswordWidget* m_passwordWidget = nullptr;

public: //提供get、set接口
    int getUserId(){ return m_userid;}
    void setUserId(int userid){m_userid = userid;}

    QString getUserNick(){return m_nick;}
    void setUserNick(const QString& nick){m_nick == nick;}

    //移除好友接口
    void removeFriend(int friendId);
    void removeGroup(int groupId);
    void closeProfileEditor();
    void closePasswordEditor();
};

#endif // MAINWIDGET_H
