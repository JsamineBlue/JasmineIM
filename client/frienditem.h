#ifndef FRIENDITEM_H
#define FRIENDITEM_H

#include <QWidget>
#include "chatwidget.h"

namespace Ui {
class FriendItem;
}

class FriendItem : public QWidget
{
    Q_OBJECT

public:
    explicit FriendItem(QWidget *parent = nullptr);
    ~FriendItem();

    void setFriendInfo(int iconid,int status,const QString& nick,const QString& feeling);
    int getFriendId(int friendid);
    void setFriendChatMsg(QString);

    void setNick(const QString&);
    QString getNick();
    void setFirendOff();
    void closeChatWindow();

private slots:
    void on_pb_friimg_clicked();
    //接收来自聊天页面的聊天内容
    void slots_sendMsgAndId(QString);

signals:
    void signals_sendMsgAndId(int,QString);
    void signals_sendFileAndId(int friendId,const QString& filePath);   //文件传输过程中传递朋友id

private:
    Ui::FriendItem *ui;
    int m_friendId;
    int m_iconId;
    int m_status;
    QString m_feeling;
    QString m_nick;

    //定义和朋友的聊天框
    ChatWidget* m_pChat;
};

#endif // FRIENDITEM_H
