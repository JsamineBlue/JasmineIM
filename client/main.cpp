#include "kernel.h"

#include <QApplication>
#include <QPointer>
#include <QTimer>

#include <functional>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // 切换账号时会短暂没有窗口，不能自动退出程序。
    app.setQuitOnLastWindowClosed(false);
    QPointer<Kernel> session;

    bool ending = false;
    bool returnToLogin = false;

    std::function<void()> startSession;

    startSession = [&] {
        ending = false;
        returnToLogin = false;
        session = new Kernel;

        QObject::connect(session.data(),&Kernel::signals_sessionFinished,&app,
                         [&](bool restart) {
                             if (ending || !session) {
                                 return;
                             }

                             ending = true;
                             returnToLogin = restart;

                             // 等当前槽函数和窗口事件处理结束后再销毁。
                             session->deleteLater();
                         }
                         );

        QObject::connect(session.data(),&QObject::destroyed,&app,[&] {
            session = nullptr;

            if (returnToLogin) {
                QTimer::singleShot(0, &app, [&] {
                    startSession();
                });
            } else {
                app.quit();
            }
        });
    };

    QObject::connect(&app,&QCoreApplication::aboutToQuit,&app,[&] {
        if (session) {
            // 程序退出时不再触发重新创建会话。
            QObject::disconnect(session.data(),nullptr,&app,nullptr);

            delete session.data();
        }
    }
                     );

    startSession();
    return app.exec();
}