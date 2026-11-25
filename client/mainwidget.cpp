#include "mainwidget.h"
#include "ui_mainwidget.h"
#include <QCursor>
#include <QDebug>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QInputDialog>
#include <QStringList>
#include <QVector>
#include "groupchatwidget.h"


#define MENU_ADD_FRI    "添加好友"
#define MENU_SYS_SET    "系统设置"
#define MENU_CREATE_GROUP "创建群"
#define MENU_CHANGE_PASSWORD "修改密码"
#define MENU_LOGOUT "退出登录"

MainWidget::MainWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWidget),m_pMenu(new QMenu)
{
    ui->setupUi(this);
    connect(ui->lw_groupList,&QListWidget::itemDoubleClicked,this,&MainWidget::slots_openGroupChat);
    connect(ui->pb_menu,SIGNAL(clicked()),this,SLOT(slots_showMenu()));

    m_pMenu->addAction(MENU_ADD_FRI);
    m_pMenu->addAction(MENU_SYS_SET);
    m_pMenu->addAction(MENU_CREATE_GROUP);
    m_pMenu->addAction(MENU_CHANGE_PASSWORD);
    m_pMenu->addSeparator();
    m_pMenu->addAction(MENU_LOGOUT);

    connect(m_pMenu,SIGNAL(triggered(QAction*)),this,SLOT(slots_dealMenu(QAction*)));
    connect(ui->pb_img,&QPushButton::clicked,this,&MainWidget::slots_editProfile);
}

MainWidget::~MainWidget()
{
    qDebug()<<"~MainWidget";
    delete ui;

    //回收菜单
    if(m_pMenu){
        delete m_pMenu;
        m_pMenu = nullptr;
    }

    //遍历回收好友列表
    for(pair<int,FriendItem*>pr : m_friendLstMap){
        delete pr.second;
    }
}

void MainWidget::setMyInfo(int iconid,const QString& nick,const QString& feeling){
    ui->pb_img->setIcon(QIcon(QString(":/tx/%1.png").arg(iconid))); //类似printf("%d",iconid),%1相当于%d，可以被iconid替换
    ui->l_nick->setText(nick);
    ui->le_feeling->setText(feeling);

    //存储信息
    m_iconId = iconid;
    m_nick = nick;
    m_feeling = feeling;

    m_currentProfileIconId = iconid;
    m_currentProfileFeeling = feeling;
}

void MainWidget::setFriendInfo(int friendid,int iconid,int status,const QString& nick,const QString& feeling){
    //判断好友信息是否已经存在
    if(m_friendLstMap.count(friendid)){
        //更新朋友信息，id不变，不用重新设置
        m_friendLstMap[friendid]->setFriendInfo(iconid,status,nick,feeling);
    }
    else{
        //创建朋友
        FriendItem* pfriendItem = new FriendItem;

        //好友右键菜单
        pfriendItem->setContextMenuPolicy(Qt::CustomContextMenu);

        connect(pfriendItem,&QWidget::customContextMenuRequested,this,[this, friendid](const QPoint&) {
                QMenu menu(this);

                QAction* deleteAction = menu.addAction("删除好友");

                if (menu.exec(QCursor::pos()) == deleteAction) {
                    slots_requestDeleteFriend(friendid);
                }
            });

        //设置朋友信息到窗口上
        pfriendItem->setFriendInfo(iconid,status,nick,feeling);
        pfriendItem->getFriendId(friendid);     //存储朋友ID

        //创建列表项，并添加朋友窗口信息
        QListWidgetItem* pitem = new QListWidgetItem;
        ui->lw_friendlist->addItem(pitem);
        ui->lw_friendlist->setItemWidget(pitem,pfriendItem);

        //统一朋友标签列表和小窗口大小
        QRect rect = pfriendItem->geometry();
        pitem->setSizeHint(QSize(rect.width(),rect.height()));

        connect(pfriendItem,SIGNAL(signals_sendMsgAndId(int,QString)),this,SIGNAL(signals_sendMsgAndIdToKernel(int,QString)));
        connect(pfriendItem,&FriendItem::signals_sendFileAndId,this,&MainWidget::signals_sendFileToKernel);

        //将朋友信息存入朋友列表
        m_friendLstMap.insert(pair<int,FriendItem*>(friendid,pfriendItem));
    }

}

void MainWidget::setChatMsg(int friendid,QString text){
    //找到列表中对应的朋友
    if(m_friendLstMap.count(friendid)){ //朋友存在
        m_friendLstMap[friendid]->setFriendChatMsg(text);
    }
}

void MainWidget::slots_showMenu(){
    //获取光标位置
    QPoint po = QCursor::pos();
    po.setY(po.y() - m_pMenu->sizeHint().height());
    //指定位置显示菜单
    m_pMenu->exec(po);    //popup显示
}

void MainWidget::slots_dealMenu(QAction* p){

    if (p->text() == MENU_LOGOUT) {
        QMessageBox dialog(this);
        dialog.setWindowTitle("退出登录");
        dialog.setIcon(QMessageBox::Question);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setText("确定退出当前账号并返回登录界面吗？\n""正在进行的文件任务将结束。");
        dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        dialog.setDefaultButton(QMessageBox::No);

        if (dialog.exec() == QMessageBox::Yes) {
            emit signals_logoutRequested();
        }
        return;
    }

    if (p->text() == MENU_CHANGE_PASSWORD) {
        slots_openChangePassword();
        return;
    }

    if(p->text() == MENU_ADD_FRI){
        QString frinick = QInputDialog::getText(this,"添加好友","请输入好友昵称");

        frinick = frinick.trimmed();      //去除两边空白符
        if(frinick.isEmpty()){
            QMessageBox::warning(this,"警告","好友昵称不能为空");
            return;
        }

        //判断是否已经是好友
        //遍历map
        for(pair<int,FriendItem*> pr: m_friendLstMap){
            if(pr.second->getNick() == frinick){
                QMessageBox::warning(this,"警告",frinick + "已经是你的好友了");
                return;
            }
        }

        //判断是否是自己
        if(frinick == m_nick){
            QMessageBox::warning(this,"警告","不可添加自己");
            return;
        }

        //发送给 kernel
        emit signals_addFriend(frinick);
    }
    else if (p->text() == MENU_CREATE_GROUP) {
        bool accepted = false;

        QString groupName = QInputDialog::getText(
            this,
            "创建群",
            "请输入群名称（最多30个字符）",
            QLineEdit::Normal,
            QString(),
            &accepted
            );

        if (!accepted) {
            return;
        }

        groupName = groupName.trimmed();

        if (groupName.isEmpty()) {
            QMessageBox::warning(
                this,
                "提示",
                "群名称不能为空"
                );
            return;
        }

        // 按 Unicode 码点计数，避免把 emoji 算成两个字符。
        if (groupName.toUcs4().size() > 30) {
            QMessageBox::warning(
                this,
                "提示",
                "群名称最多30个字符"
                );
            return;
        }

        if (groupName.contains(QChar('\0'))) {
            QMessageBox::warning(
                this,
                "提示",
                "群名称不能包含空字符"
                );
            return;
        }

        emit signals_createGroup(groupName);
    }
    else if(p->text() == MENU_SYS_SET){
        qDebug()<<"系统设置未开发，敬请期待";
    }
}

void MainWidget::setFriendOffline(int friendid){
    //判断好友信息是否已经存在
    if(m_friendLstMap.count(friendid)){
        //朋友下线
        m_friendLstMap[friendid]->setFirendOff();
    }
}

//窗口关闭时会触发的事件
void MainWidget::closeEvent(QCloseEvent *event){
    emit signals_userOffLine();
}

//群聊窗口更新
void MainWidget::setGroupInfo(int groupId,const QString& groupName,int ownerId){
    if (groupId <= 0 || ownerId <= 0) {
        qWarning() << "群资料不合法";
        return;
    }

    QListWidgetItem* item = nullptr;

    auto existing = m_groupItems.find(groupId);

    if (existing == m_groupItems.end()) {
        item = new QListWidgetItem(ui->lw_groupList);
        m_groupItems.emplace(groupId, item);
    }
    else {
        item = existing->second;
    }

    item->setText(QString("%1\n群号：%2").arg(groupName).arg(groupId));

    item->setData(Qt::UserRole, groupId);
    item->setData(Qt::UserRole + 1, ownerId);
    item->setData(Qt::UserRole + 2, groupName);

    item->setSizeHint(QSize(200, 64));
}

void MainWidget::slots_openGroupChat(QListWidgetItem* item){
    if (item == nullptr) {
        return;
    }

    int groupId = item->data(Qt::UserRole).toInt();
    int ownerId = item->data(Qt::UserRole + 1).toInt();

    QString groupName =item->data(Qt::UserRole + 2).toString();

    if (groupId <= 0 || ownerId <= 0 || groupName.isEmpty()) {
        qWarning() << "群列表项数据不完整";
        return;
    }

    GroupChatWidget* window = nullptr;
    auto existing = m_groupWindows.find(groupId);

    if (existing == m_groupWindows.end()) {
        window = new GroupChatWidget(this);
        m_groupWindows.emplace(groupId, window);

        connect(window,&GroupChatWidget::signals_inviteFriend,this,&MainWidget::slots_chooseGroupInvitee);
        connect(window,&GroupChatWidget::signals_sendGroupMessage,this,&MainWidget::signals_sendGroupMessage);
        connect(window,&GroupChatWidget::signals_leaveGroup,this,&MainWidget::signals_leaveGroup);
    }
    else {
        window = existing->second;
    }

    window->setGroupInfo(groupId,groupName,ownerId);

    window->showNormal();
    window->raise();
    window->activateWindow();

    //发送加载群成员列表的信号
    emit signals_requestGroupMembers(groupId);
}

//群成员资料转交接口
void MainWidget::setGroupMembers(int groupId,const QVector<GroupMemberView>& members){
    auto window = m_groupWindows.find(groupId);

    if (window == m_groupWindows.end()) {
        return;
    }

    window->second->setMembers(members);
}

void MainWidget::slots_chooseGroupInvitee(int groupId){
    auto group = m_groupItems.find(groupId);

    if (group == m_groupItems.end()) {
        return;
    }

    int ownerId =group->second->data(Qt::UserRole + 1).toInt();

    if (ownerId != m_userid) {
        QMessageBox::warning(this,"邀请好友","仅支持群主邀请好友");
        return;
    }

    QStringList choices;
    QVector<int> userIds;

    for (const auto& entry : m_friendLstMap) {
        int friendId = entry.first;

        if (friendId <= 0 || friendId == m_userid) {
            continue;
        }

        choices.push_back(
            QString("%1（用户ID：%2）").arg(entry.second->getNick()).arg(friendId));

        userIds.push_back(friendId);
    }

    if (choices.isEmpty()) {
        QMessageBox::information(this,"邀请好友","当前没有可以选择的好友");
        return;
    }

    bool accepted = false;

    QString selected = QInputDialog::getItem(this,"邀请好友入群","请选择好友",choices,0,false,&accepted);

    if (!accepted) {
        return;
    }

    int index = choices.indexOf(selected);

    if (index < 0 || index >= userIds.size()) {
        return;
    }

    emit signals_inviteGroupFriend(groupId,userIds[index]);
}

QString MainWidget::friendDisplayName(int userId) const {
    auto it = m_friendLstMap.find(userId);

    if (it != m_friendLstMap.end() && it->second != nullptr) {
        const QString nick = it->second->getNick();

        if (!nick.isEmpty()) {
            return nick;
        }
    }

    return QStringLiteral("该好友");
}

QString MainWidget::groupDisplayName(int groupId) const {
    auto it = m_groupItems.find(groupId);

    if (it != m_groupItems.end() && it->second != nullptr) {
        const QString name =
            it->second->data(Qt::UserRole + 2).toString();

        if (!name.isEmpty()) {
            return name;
        }
    }

    return QStringLiteral("该群");
}

void MainWidget::refreshOpenGroupMembers(int groupId) {
    auto it = m_groupWindows.find(groupId);

    if (it == m_groupWindows.end() || it->second == nullptr) {
        return;
    }

    emit signals_requestGroupMembers(groupId);
}

void MainWidget::appendGroupMessage(int groupId,const QString& senderName,const QString& message) {
    auto group = m_groupItems.find(groupId);

    if (group == m_groupItems.end()) {
        qWarning() << "收到未知群的消息，群ID：" << groupId;
        return;
    }

    auto window = m_groupWindows.find(groupId);

    if (window == m_groupWindows.end()) {
        // 复用现有创建窗口、连接信号的逻辑。
        slots_openGroupChat(group->second);

        window = m_groupWindows.find(groupId);
    }

    if (window == m_groupWindows.end() || window->second == nullptr) {
        return;
    }

    window->second->appendGroupMessage(senderName,message);
}

void MainWidget::setGroupMessageResult(int groupId,const QString& message,const QString& status,bool success) {
    // 复用窗口创建与消息显示逻辑。
    appendGroupMessage(groupId, QStringLiteral("我"), message);

    auto window = m_groupWindows.find(groupId);

    if (window == m_groupWindows.end() || window->second == nullptr) {
        return;
    }

    window->second->applySendResult(message,status,success);
}

//确认删除好友函数
void MainWidget::slots_requestDeleteFriend(int friendId) {
    auto it = m_friendLstMap.find(friendId);

    if (it == m_friendLstMap.end() || it->second == nullptr) {
        return;
    }

    const QString nick = it->second->getNick();

    QMessageBox dialog(this);
    dialog.setWindowTitle("删除好友");
    dialog.setIcon(QMessageBox::Question);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("确定删除好友【%1】吗？\n\n删除后双方将解除好友关系，进行中的文件任务也会结束。").arg(nick));
    dialog.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dialog.setDefaultButton(QMessageBox::No);

    if (dialog.exec() != QMessageBox::Yes) {
        return;
    }

    // 弹窗期间好友列表可能变化，重新检查。
    if (m_friendLstMap.find(friendId) == m_friendLstMap.end()) {
        return;
    }

    emit signals_deleteFriend(friendId);
}

void MainWidget::removeFriend(int friendId) {
    auto it = m_friendLstMap.find(friendId);

    if (it == m_friendLstMap.end()) {
        return;
    }

    FriendItem* friendWidget = it->second;
    m_friendLstMap.erase(it);

    if (!friendWidget) {
        return;
    }

    friendWidget->closeChatWindow();
    friendWidget->hide();

    for (int row = 0;row < ui->lw_friendlist->count();++row) {
        QListWidgetItem* item = ui->lw_friendlist->item(row);

        if (ui->lw_friendlist->itemWidget(item) == friendWidget) {
            ui->lw_friendlist->removeItemWidget(item);
            delete ui->lw_friendlist->takeItem(row);
            break;
        }
    }

    // 延迟释放，避免在菜单或信号处理过程中直接销毁。
    // Qt 对同一对象重复调用 deleteLater() 是安全的。
    friendWidget->deleteLater();
}

void MainWidget::removeGroup(int groupId) {
    auto window = m_groupWindows.find(groupId);

    if (window != m_groupWindows.end()) {
        GroupChatWidget* widget = window->second;
        m_groupWindows.erase(window);

        if (widget) {
            widget->close();
            widget->deleteLater();
        }
    }

    auto group = m_groupItems.find(groupId);

    if (group != m_groupItems.end()) {
        QListWidgetItem* item = group->second;
        m_groupItems.erase(group);

        if (item) {
            const int row = ui->lw_groupList->row(item);

            if (row >= 0) {
                delete ui->lw_groupList->takeItem(row);
            }
        }
    }
}

void MainWidget::slots_editProfile() {
    if (m_userid <= 0) {
        return;
    }

    if (!m_profileDialog) {
        m_profileDialog = new ProfileDialog(this);
        m_profileDialog->setWindowFlag(Qt::Window);
        m_profileDialog->setWindowTitle("修改个人资料");
        connect(m_profileDialog,&ProfileDialog::signals_saveProfile,this,&MainWidget::signals_updateProfile);
    }

    m_profileDialog->setProfile(m_currentProfileIconId,m_nick,m_currentProfileFeeling);
    m_profileDialog->showNormal();
    m_profileDialog->raise();
    m_profileDialog->activateWindow();
}

void MainWidget::closeProfileEditor() {
    if (m_profileDialog) {
        m_profileDialog->close();
    }
}

void MainWidget::slots_openChangePassword() {
    if (m_userid <= 0) {
        return;
    }

    if (!m_passwordWidget) {
        m_passwordWidget = new ChangePasswordWidget(this);
        connect(m_passwordWidget,&ChangePasswordWidget::signals_changePassword,this,&MainWidget::signals_changePassword);
    }

    if (!m_passwordWidget->isVisible()) {
        m_passwordWidget->clearPasswords();
    }

    m_passwordWidget->showNormal();
    m_passwordWidget->raise();
    m_passwordWidget->activateWindow();
}

void MainWidget::closePasswordEditor() {
    if (m_passwordWidget) {
        m_passwordWidget->close();
    }
}