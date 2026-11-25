#ifndef GROUPCHATWIDGET_H
#define GROUPCHATWIDGET_H

#include <QWidget>
#include <QString>
#include <QVector>

struct GroupMemberView{
    int userId = 0;
    int iconId = 0;
    QString nick;
};

namespace Ui {
class GroupChatWidget;
}

class GroupChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GroupChatWidget(QWidget *parent = nullptr);
    ~GroupChatWidget();

    void setGroupInfo(int groupId,const QString& groupName,int ownerId);

    //群成员列表显示
    void setMembers(const QVector<GroupMemberView>& members);

    //聊天内容显示
    void appendGroupMessage(const QString& senderName,const QString& message);

    //发送消息结果显示
    void applySendResult(const QString& message,const QString& status,bool success);

private:
    Ui::GroupChatWidget *ui;
    int m_groupId = 0;
    int m_ownerId = 0;
    QString m_groupName;

signals:
    void signals_inviteFriend(int groupId); //邀请好友信号
    void signals_sendGroupMessage(int groupId,const QString& message);  //发送信息信号
    void signals_leaveGroup(int groupId);

private slots:
    void slots_sendMessage();   //发送信息槽函数
    void slots_leaveGroup();

};

#endif // GROUPCHATWIDGET_H
