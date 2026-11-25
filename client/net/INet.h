#pragma once
#include<iostream>
#include<WinSock2.h>
#include<process.h>
#include"def.h"

class INetMediator;

class INet {
protected:
	SOCKET m_sock;
	HANDLE m_handle;
	bool m_bRunning;
	INetMediator* m_pMediator;
public:
	INet() :m_sock(INVALID_SOCKET),m_handle(nullptr), m_bRunning(true),m_pMediator(nullptr){}
	virtual ~INet() {}

	//初始化网络
	virtual bool initNet() = 0;
	//关闭网络
	virtual void unInitNet() = 0;
	//接收数据
	virtual void recvData() = 0;
	//发送数据
	//data：要发送的数据内容
	//len：要发送的数据长度
	//to：发给谁
	virtual bool sendData(char* data,int len,unsigned long to) = 0;
};