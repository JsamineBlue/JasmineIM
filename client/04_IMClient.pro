QT += widgets
QT += concurrent

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

LIBS += -lWs2_32

SOURCES += \
    changepasswordwidget.cpp \
    chatwidget.cpp \
    frienditem.cpp \
    groupchatwidget.cpp \
    kernel.cpp \
    main.cpp \
    loginwidget.cpp \
    mainwidget.cpp \
    mediator/INetMediator.cpp \
    mediator/TCP_ClientMediator.cpp \
    mediator/TCP_ServerMediator.cpp \
    mediator/UDPMediator.cpp \
    net/TCP_Client.cpp \
    net/TCP_Server.cpp \
    net/UDP.cpp \
    profiledialog.cpp

HEADERS += \
    changepasswordwidget.h \
    chatwidget.h \
    frienditem.h \
    groupchatwidget.h \
    kernel.h \
    loginwidget.h \
    mainwidget.h \
    mediator/INetMediator.h \
    mediator/TCP_ClientMediator.h \
    mediator/TCP_ServerMediator.h \
    mediator/UDPMediator.h \
    net/INet.h \
    net/TCP_Client.h \
    net/TCP_Server.h \
    net/UDP.h \
    net/def.h \
    net/file_protocol.h \
    net/group_protocol.h \
    profiledialog.h

FORMS += \
    changepasswordwidget.ui \
    chatwidget.ui \
    frienditem.ui \
    groupchatwidget.ui \
    loginwidget.ui \
    mainwidget.ui \
    profiledialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res.qrc
