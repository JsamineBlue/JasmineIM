#pragma once
#include"INet.h"

class UDP:public INet {
public:
	UDP(INetMediator* p);
	~UDP();

	//初始化网络
	bool initNet();
	//关闭网络
	void unInitNet();

	//线程函数（接收数据，调用recvData）
	static unsigned __stdcall recvThread(void* IpVoid);
	//接收数据
	void recvData();
	//发送数据
	bool sendData(char* data, int len, unsigned long to);
};