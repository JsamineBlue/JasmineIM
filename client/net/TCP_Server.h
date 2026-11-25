#pragma once
#include"INet.h"
#include<map>
#include<list>

class TCP_Server :public INet {
private:
    std::map<unsigned int,SOCKET> m_mapThreadIdToSocket;
    std::list<HANDLE> m_listHandle;
public:
	TCP_Server(INetMediator*);
	~TCP_Server();

	//初始化网络
	bool initNet();
	//关闭网络
	void unInitNet();
	//接受连接的线程函数
	static unsigned __stdcall acceptThread(void*);
	//接收数据的线程函数
	static unsigned __stdcall recvThread(void*);
	//接收数据
	void recvData();
	//发送数据
	bool sendData(char* data, int len, unsigned long to);
};