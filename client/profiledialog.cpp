#include "profiledialog.h"
#include "ui_profiledialog.h"

#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>

ProfileDialog::ProfileDialog(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ProfileDialog)
{
    ui->setupUi(this);

    ui->cb_avatar->setIconSize(QSize(40, 40));

    for (int id = 0; id <= 36; ++id) {
        ui->cb_avatar->addItem(QIcon(QString(":/tx/%1.png").arg(id)),QString("头像 %1").arg(id + 1),id);
    }
    connect(ui->pb_save,&QPushButton::clicked,this,&ProfileDialog::slots_saveProfile);
    connect(ui->pb_cancel,&QPushButton::clicked,this,&QWidget::close);
}

ProfileDialog::~ProfileDialog()
{
    delete ui;
}

void ProfileDialog::setProfile(int iconId,const QString& nick,const QString& feeling) {
    ui->le_nick->setText(nick);
    ui->le_feeling->setText(feeling);

    const int index = ui->cb_avatar->findData(iconId);

    ui->cb_avatar->setCurrentIndex(
        index >= 0 ? index : 0
        );
}

void ProfileDialog::slots_saveProfile() {
    const QString nick = ui->le_nick->text().trimmed();
    const QString feeling = ui->le_feeling->text();

    if (nick.isEmpty() || nick.contains(QChar('\0')) || nick.toUtf8().size() >= 30) {
        QMessageBox::warning(this,"修改资料","昵称不能为空，且最多允许 29 个 UTF-8 字节");
        return;
    }

    if (feeling.contains(QChar('\0')) ||
        feeling.toUtf8().size() >= 100) {
        QMessageBox::warning(this,"修改资料","个性签名最多允许 99 个 UTF-8 字节");
        return;
    }

    const int iconId = ui->cb_avatar->currentData().toInt();

    emit signals_saveProfile(iconId, nick, feeling);
}
