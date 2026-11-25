#pragma once
#include"INetMediator.h"

class TCP_ServerMediator :public INetMediator {
public:
	TCP_ServerMediator();
	~TCP_ServerMediator();

	//打开网络
	bool openNet();

	//关闭网络
	void closeNet();

	//转发数据
	void transmateData(char* data, int len, unsigned long from);

	//发送数据
	bool sendData(char* data, int len, unsigned long to);
};