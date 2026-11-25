#include "loginwidget.h"
#include "ui_loginwidget.h"
#include<QMessageBox>
#include<QDebug>

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
{
    ui->setupUi(this);
}

LoginWidget::~LoginWidget()
{
    qDebug()<<"~LoginWidget";
    delete ui;
}

void LoginWidget::on_pb_register_clicked()
{
    //获取注册的信息
    QString nick = ui->le_nick->text();
    QString tel = ui->le_register_tel->text();

    //获取密码
    QString pass = ui->le_register_pass->text();
    QString passAgain = ui->le_pass_again->text();

    //判断是否为空
    if(nick.isEmpty() || tel.isEmpty() || pass.isEmpty() || passAgain.isEmpty()){
        QMessageBox::warning(this,"警告","注册信息不能为空");
        return;
    }

    //校验手机号长度
    if(tel.size()!=11){
        QMessageBox::warning(this,"警告","手机号长度不匹配");
        return;
    }

    //校验手机号是否全是数字
    for(int i = 0; i<11; i++){
        if(tel[i]<'0' || tel[i]>'9'){
            QMessageBox::warning(this,"警告","手机号中包含其他字符");
            return;
        }
    }

    //校验两个密码是否相同
    if(pass!=passAgain){
        QMessageBox::warning(this,"警告","两次输入密码不一致");
        return;
    }

    //todo：发送给服务端
    emit signals_register(nick,tel,pass);       //给 kernel 发射信号
}

//注册情况
void LoginWidget::on_pb_register_clear_clicked()
{
    ui->le_nick->clear();
    ui->le_register_tel->clear();
    ui->le_register_pass->clear();
    ui->le_pass_again->clear();
}

//登录
void LoginWidget::on_pb_login_clicked()
{
   QString tel = ui->le_tel->text();
   QString pass = ui->le_pass->text();

   //校验是否为空
   if(tel.isEmpty() || pass.isEmpty()){
       QMessageBox::warning(this,"警告","登录信息不能为空");
       return;
   }

   //校验手机号是否规范
   //校验手机号长度
   if(tel.size()!=11){
       QMessageBox::warning(this,"警告","手机号长度不匹配");
       return;
   }

   //校验手机号是否全是数字
   for(int i = 0; i<11; i++){
       if(tel[i]<'0' || tel[i]>'9'){
           QMessageBox::warning(this,"警告","手机号中包含其他字符");
           return;
       }
   }

   //todo:发送到服务端
   emit signals_login(tel,pass);
}

//登录清空
void LoginWidget::on_pb_clear_clicked()
{
    ui->le_tel->clear();
    ui->le_pass->clear();
}

void LoginWidget::closeEvent(QCloseEvent *event){
    emit signals_closeWindow();
}