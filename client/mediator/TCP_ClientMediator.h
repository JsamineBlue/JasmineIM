#pragma once
#include"INetMediator.h"

class TCP_ClientMediator :public INetMediator {
    Q_OBJECT;
public:
	TCP_ClientMediator();
	~TCP_ClientMediator();

	//打开网络
	bool openNet();

	//关闭网络
	void closeNet();

	//转发数据
	void transmateData(char* data, int len, unsigned long from);

	//发送数据
	bool sendData(char* data, int len, unsigned long to);

signals:
    //把接收到的数据让kernel处理
    void signals_recvServerData(char*,int,unsigned long);
};