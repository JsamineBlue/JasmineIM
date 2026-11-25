#include "chatwidget.h"
#include "ui_chatwidget.h"
#include <QMessageBox>
#include <QDebug>
#include <QTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QPushButton>
#include "net/file_protocol.h"


ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatWidget)
{
    ui->setupUi(this);

    connect(ui->pb_send,SIGNAL(clicked()),this,SLOT(slots_sendMsg()));
    connect(ui->pb_clear,SIGNAL(clicked()),this,SLOT(slots_clearMsg()));
    connect(ui->pb_file,&QPushButton::clicked,this,&ChatWidget::slots_chooseFile);
}

ChatWidget::~ChatWidget()
{
    qDebug()<<"~ChatWidget";
    delete ui;
}

void ChatWidget::setChatTitle(const QString& title){
    setWindowTitle(title);
}

void ChatWidget::slots_sendMsg(){
    QString text = ui->te_msg->toPlainText();
    if(text.isEmpty()){
        QMessageBox::warning(this,"警告","发送文本不能为空");
        return;
    }

    //获取输入框中带有格式的文本，老师这么讲的，实际该行格式会把后面设置的格式给覆盖掉，所以不要
    //text = ui->te_msg->toHtml();

    //将发送内容显示到聊天框上
    //设置发送内容格式：我[时间]：换行以后，大号字体的、加粗的文本内容
    QString str = QString("<font color='gray'>我[%1]：</font>") + "<p><span style='font-size:16pt; font-weight:bold'>"+text+ "</span></p>";
    str = str.arg(QTime::currentTime().toString("hh:mm:ss"));

    //在聊天框上显示出来
    ui->tb_chatcord->append(str);

    //发送给服务端
    emit signals_sendMsg(text);

    //清空聊天内容
    ui->te_msg->clear();
}

void ChatWidget::slots_clearMsg(){
    ui->te_msg->clear();    //清空输入的聊天内容
}

void ChatWidget::setChatText(QString s){
    ui->tb_chatcord->append(s);
}

void ChatWidget::slots_chooseFile() {
    const QString filePath = QFileDialog::getOpenFileName(this,"选择要发送的文件");

    if (filePath.isEmpty()) {
        return;
    }

    const QFileInfo info(filePath);

    if (!info.isFile()) {
        QMessageBox::warning(this,"发送文件","请选择一个普通文件");
        return;
    }

    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,"发送文件","无法读取该文件，请检查文件是否存在以及访问权限");
        return;
    }

    const qint64 fileSize = file.size();

    if (fileSize < 0 || static_cast<quint64>(fileSize) > MAX_TRANSFER_FILE_BYTES) {
        QMessageBox::warning(this,"发送文件","当前支持发送最大 64 MiB 的文件");
        return;
    }

    const QString fileName = info.fileName();
    const QByteArray nameBytes = fileName.toUtf8();

    if (fileName.isEmpty() || fileName.contains(QChar('\0')) || nameBytes.size() >= static_cast<int>(FILE_NAME_BYTES)) {
        QMessageBox::warning(this,"发送文件","文件名过长或包含不支持的字符，请重命名后再试");
        return;
    }

    file.close();

    emit signals_sendFile(info.absoluteFilePath());
}