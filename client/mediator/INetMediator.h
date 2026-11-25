#pragma once
#include<iostream>
#include<QObject>

class INet;

class INetMediator: public QObject {
    Q_OBJECT;
protected:
	INet* m_pNet;
public:
    INetMediator();
    virtual ~INetMediator();

	//打开网络
	virtual bool openNet() = 0;

	//关闭网络
	virtual void closeNet() = 0;

	//转发数据（把接收到的数据传给kernel）
	virtual void transmateData(char* data,int len,unsigned long from) = 0;

	//发送数据
	virtual bool sendData(char* data, int len, unsigned long to) = 0;
};