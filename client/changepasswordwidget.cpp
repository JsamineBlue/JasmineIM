#include "changepasswordwidget.h"
#include "ui_changepasswordwidget.h"

#include <QCloseEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

ChangePasswordWidget::ChangePasswordWidget(QWidget *parent)
    : QWidget(parent,Qt::Window)
    , ui(new Ui::ChangePasswordWidget)
{
    ui->setupUi(this);

    setWindowTitle("修改密码");

    ui->le_oldPassword->setEchoMode(QLineEdit::Password);
    ui->le_newPassword->setEchoMode(QLineEdit::Password);
    ui->le_confirmPassword->setEchoMode(QLineEdit::Password);

    connect(ui->pb_save,&QPushButton::clicked,this,&ChangePasswordWidget::slots_save);
    connect(ui->pb_cancel,&QPushButton::clicked,this,&QWidget::close);
}

ChangePasswordWidget::~ChangePasswordWidget()
{
    delete ui;
}

void ChangePasswordWidget::clearPasswords() {
    ui->le_oldPassword->clear();
    ui->le_newPassword->clear();
    ui->le_confirmPassword->clear();
}

void ChangePasswordWidget::closeEvent(QCloseEvent* event) {
    clearPasswords();
    QWidget::closeEvent(event);
}

void ChangePasswordWidget::slots_save() {
    const QString oldPassword = ui->le_oldPassword->text();
    const QString newPassword = ui->le_newPassword->text();
    const QString confirmation = ui->le_confirmPassword->text();

    const QByteArray oldBytes = oldPassword.toUtf8();
    const QByteArray newBytes = newPassword.toUtf8();

    if (oldBytes.isEmpty() || oldBytes.size() >= 15 || oldBytes.contains('\0')) {
        QMessageBox::warning(this,"修改密码","请填写旧密码，最多允许 14 个 UTF-8 字节");
        return;
    }

    if (newBytes.size() < 6 || newBytes.size() >= 15 || newBytes.contains('\0')) {
        QMessageBox::warning(this,"修改密码","新密码需要 6～14 个 UTF-8 字节");
        return;
    }

    if (newPassword != confirmation) {
        QMessageBox::warning(this,"修改密码","两次输入的新密码不一致");
        return;
    }

    if (oldPassword == newPassword) {
        QMessageBox::warning(this,"修改密码","新密码不能与旧密码相同");
        return;
    }

    emit signals_changePassword(oldPassword, newPassword);
    clearPasswords();
}