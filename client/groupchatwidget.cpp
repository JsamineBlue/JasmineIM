#include "groupchatwidget.h"
#include "ui_groupchatwidget.h"
#include "net/group_protocol.h"

#include <QIcon>
#include <QListWidget>
#include <QSet>
#include <QDebug>
#include <QPushButton>
#include <QMessageBox>
#include <QTime>
#include <QFont>
#include <QColor>
#include <QTextCursor>
#include <QTextCharFormat>

GroupChatWidget::GroupChatWidget(QWidget *parent)
    : QWidget(parent,Qt::Window)
    , ui(new Ui::GroupChatWidget)
{
    ui->setupUi(this);

    // 关闭时隐藏，供下次打开；由父窗口统一销毁。
    setAttribute(Qt::WA_DeleteOnClose, false);

    //通知：我要在这个群邀请好友
    connect(ui->pb_invite,&QPushButton::clicked,this,[this] {
            if (m_groupId > 0) {
                emit signals_inviteFriend(m_groupId);
            }
        }
        );

    //按下按钮时，触发发送信息槽函数
    connect(ui->pb_send,&QPushButton::clicked,this,&GroupChatWidget::slots_sendMessage);
    connect(ui->pb_leave,&QPushButton::clicked,this,&GroupChatWidget::slots_leaveGroup);

    // 与私聊窗口保持一致：记录默认 10 磅，输入框 12 磅。
    QFont recordFont = ui->tb_messages->font();
    recordFont.setPointSize(10);
    ui->tb_messages->setFont(recordFont);

    QFont inputFont = ui->te_message->font();
    inputFont.setPointSize(12);
    ui->te_message->setFont(inputFont);
}

GroupChatWidget::~GroupChatWidget()
{
    delete ui;
}

void GroupChatWidget::setGroupInfo(int groupId,const QString& groupName,int ownerId){
    m_groupId = groupId;
    m_groupName = groupName;
    m_ownerId = ownerId;

    ui->lb_groupName->setText(QString("%1（群号：%2）").arg(m_groupName).arg(m_groupId));

    setWindowTitle(QString("群聊：%1").arg(m_groupName));
}

void GroupChatWidget::setMembers(const QVector<GroupMemberView>& members){
    // 先检查整份数据，再替换原列表。
    QSet<int> seen;

    for (const auto& member : members) {
        if (member.userId <= 0 || member.nick.isEmpty() || seen.contains(member.userId)) {
            qWarning() << "群成员列表数据不合法";
            return;
        }

        seen.insert(member.userId);
    }

    ui->lw_members->clear();

    for (const auto& member : members) {
        auto* item = new QListWidgetItem(ui->lw_members);

        QString text = member.nick;

        if (member.userId == m_ownerId) {
            text += "（群主）";
        }

        item->setText(text);

        item->setIcon(QIcon(QString(":/tx/%1.png").arg(member.iconId)));

        item->setData(Qt::UserRole,member.userId);
    }

    ui->lb_membersTitle->setText(QString("群成员（%1）").arg(members.size()));
}

void GroupChatWidget::slots_sendMessage() {
    if (m_groupId <= 0) {
        return;
    }

    const QString message = ui->te_message->toPlainText();

    if (message.trimmed().isEmpty()) {
        QMessageBox::information(this,"发送消息","请输入消息内容");
        return;
    }

    if (message.contains(QChar('\0'))) {
        QMessageBox::warning(this,"发送消息","消息包含不支持的字符");
        return;
    }

    const QByteArray bytes = message.toUtf8();

    if (bytes.size() >= static_cast<int>(GROUP_MESSAGE_BYTES)) {
        QMessageBox::warning(this,"发送消息","消息过长，请分开发送");
        return;
    }

    emit signals_sendGroupMessage(m_groupId, message);
}

void GroupChatWidget::appendGroupMessage(const QString& senderName,const QString& message) {
    QTextCursor cursor(ui->tb_messages->document());
    cursor.movePosition(QTextCursor::End);

    if (!ui->tb_messages->document()->isEmpty()) {
        cursor.insertBlock();
    }

    // 昵称和时间：10 磅、灰色、不加粗。
    QFont headerFont = ui->tb_messages->font();
    headerFont.setPointSize(10);
    headerFont.setBold(false);

    QTextCharFormat headerFormat;
    headerFormat.setFont(headerFont);
    headerFormat.setForeground(QColor(Qt::gray));

    cursor.insertText(QStringLiteral("%1[%2]：").arg(senderName).arg(QTime::currentTime().toString("HH:mm:ss")),headerFormat);
    cursor.insertBlock();

    // 正文：16 磅、黑色、加粗。
    QFont messageFont = ui->tb_messages->font();
    messageFont.setPointSize(16);
    messageFont.setBold(true);

    QTextCharFormat messageFormat;
    messageFormat.setFont(messageFont);
    messageFormat.setForeground(QColor(Qt::black));

    cursor.insertText(message, messageFormat);
    cursor.insertBlock();

    ui->tb_messages->setTextCursor(cursor);
    ui->tb_messages->ensureCursorVisible();
}

void GroupChatWidget::applySendResult(const QString& message,const QString& status,bool success) {
    QTextCursor cursor(ui->tb_messages->document());
    cursor.movePosition(QTextCursor::End);

    QFont statusFont = ui->tb_messages->font();
    statusFont.setPointSize(10);
    statusFont.setBold(false);

    QTextCharFormat statusFormat;
    statusFormat.setFont(statusFont);
    statusFormat.setForeground(QColor(Qt::gray));

    cursor.insertText(status, statusFormat);
    cursor.insertBlock();

    ui->tb_messages->setTextCursor(cursor);
    ui->tb_messages->ensureCursorVisible();

    // 只清除仍然与本次发送内容相同的输入。
    // 等待回复期间用户新写的内容不能被清掉。
    if (success && ui->te_message->toPlainText() == message) {
        ui->te_message->clear();
    }
}

void GroupChatWidget::slots_leaveGroup() {
    if (m_groupId <= 0) {
        return;
    }

    QMessageBox dialog(this);
    dialog.setWindowTitle("退出群聊");
    dialog.setIcon(QMessageBox::Question);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("确定退出“%1”吗？\n退出后将不再接收该群的新消息。").arg(m_groupName));

    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dialog.setDefaultButton(QMessageBox::No);

    if (dialog.exec() == QMessageBox::Yes) {
        emit signals_leaveGroup(m_groupId);
    }
}