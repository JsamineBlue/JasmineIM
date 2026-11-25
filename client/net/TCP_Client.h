#pragma once
#include"INet.h"


class TCP_Client :public INet {
private:
    bool m_wsaStarted = false;

public:
	TCP_Client(INetMediator*);
	virtual ~TCP_Client();

	//初始化网络
	bool initNet();
	//关闭网络
	void unInitNet();
	//接收数据的线程函数
	static unsigned __stdcall recvThread(void*);
	//接收数据
	void recvData();
	//发送数据
	bool sendData(char* data, int len, unsigned long to);
};