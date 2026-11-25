#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui {
class LoginWidget;
}
QT_END_NAMESPACE

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget() override;
    virtual void closeEvent(QCloseEvent *event) override;

private slots:
    void on_pb_register_clicked();

    void on_pb_login_clicked();

    void on_pb_clear_clicked();

    void on_pb_register_clear_clicked();

signals:
    void signals_register(QString,QString,QString);
    void signals_login(QString,QString);
    void signals_closeWindow();

private:
    Ui::LoginWidget *ui;
};
#endif // LOGINWIDGET_H
