#ifndef CHANGEPASSWORDWIDGET_H
#define CHANGEPASSWORDWIDGET_H

#include <QWidget>
#include <QWidget>
#include <QString>

namespace Ui {
class ChangePasswordWidget;
}

class ChangePasswordWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChangePasswordWidget(QWidget *parent = nullptr);
    ~ChangePasswordWidget();

    void clearPasswords();

signals:
    void signals_changePassword(const QString& oldPassword,const QString& newPassword);

private slots:
    void slots_save();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    Ui::ChangePasswordWidget *ui;
};

#endif // CHANGEPASSWORDWIDGET_H
