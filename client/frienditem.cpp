#include "frienditem.h"
#include "qdatetime.h"
#include "ui_frienditem.h"
#include "net/def.h"
#include <QBitmap>
#include <QDebug>

FriendItem::FriendItem(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FriendItem)
    ,m_pChat(new ChatWidget)
{
    ui->setupUi(this);
    connect(m_pChat,SIGNAL(signals_sendMsg(QString)),this,SLOT(slots_sendMsgAndId(QString)));

    connect(m_pChat,&ChatWidget::signals_sendFile,this,[this](const QString& filePath)
            {emit signals_sendFileAndId(m_friendId,filePath);});
}

FriendItem::~FriendItem()
{
    qDebug()<<"~FriendItem";
    delete ui;

    //回收聊天窗口
    if(m_pChat){
        delete m_pChat;
        m_pChat = nullptr;
    }
}

void FriendItem::setFriendInfo(int iconid,int status,const QString& nick,const QString& feeling){
    if(status == FRIEND_STATUS_ONLINE){
        ui->pb_friimg->setIcon(QIcon (QString(":/tx/%1.png").arg(iconid))); //显示彩色图片
    }
    else if(status == FRIEND_STATUS_OFFLINE){
        QBitmap bit(QString(":/tx/%1.png").arg(iconid));    //构建黑白位图
        ui->pb_friimg->setIcon(bit);        //显示黑白图
    }

    ui->l_frinick->setText(nick);
    ui->l_frifeeling->setText(feeling);

    // 存储朋友的各种信息
    m_iconId = iconid;
    m_status = status;
    m_feeling = feeling;
    m_nick = nick;
}

int FriendItem::getFriendId(int friendid){
    return m_friendId = friendid;
}

//点击头像会触发，显示聊天框
void FriendItem::on_pb_friimg_clicked()
{
    m_pChat->setChatTitle("与"+m_nick+"的聊天框");
    m_pChat->show();
}

void FriendItem::slots_sendMsgAndId(QString text){
    //qDebug()<<"friendItemReceved："<<text;

    //追加朋友的id，继续向上传递
    emit signals_sendMsgAndId(m_friendId,text);
}

//设置朋友返回的聊天内容
void FriendItem::setFriendChatMsg(QString text){
    QString s=m_nick+"[%1]:"+text;
    m_pChat->setChatText("<font color='gray'>" + s.arg(QTime::currentTime().toString("hh:mm:ss")) + "</font>");
}

void FriendItem::setNick(const QString& nick){m_nick = nick;}
QString FriendItem::getNick(){return m_nick;}

//朋友下线，头像变灰，状态下线
void FriendItem::setFirendOff(){
    QBitmap bit(QString(":/tx/%1.png").arg(m_iconId));    //构建黑白位图
    ui->pb_friimg->setIcon(bit);        //显示黑白图

    m_status = FRIEND_STATUS_OFFLINE;
}

void FriendItem::closeChatWindow() {
    if (m_pChat) {
        m_pChat->close();
    }
}