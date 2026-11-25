#ifndef PROFILEDIALOG_H
#define PROFILEDIALOG_H

#include <QWidget>

namespace Ui {
class ProfileDialog;
}

class ProfileDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ProfileDialog(QWidget *parent = nullptr);
    ~ProfileDialog();

    void setProfile(int iconId,const QString& nick,const QString& feeling);

signals:
    void signals_saveProfile(int iconId,const QString& nick,const QString& feeling);

private slots:
    void slots_saveProfile();

private:
    Ui::ProfileDialog *ui;
};

#endif // PROFILEDIALOG_H
