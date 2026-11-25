#include"TCP_ServerMediator.h"
#include"../net/TCP_Server.h"
using namespace std;

TCP_ServerMediator::TCP_ServerMediator() {
	m_pNet = new TCP_Server(this);
}
TCP_ServerMediator::~TCP_ServerMediator() {
	if (m_pNet) {
		delete m_pNet;
		m_pNet = nullptr;
	}
}

//打开网络
bool TCP_ServerMediator::openNet() {
	return m_pNet->initNet();
}

//关闭网络
void TCP_ServerMediator::closeNet() {
	m_pNet->unInitNet();
}

//转发数据
void TCP_ServerMediator::transmateData(char* data, int len, unsigned long from) {
	//测试代码：打印接收到的数据，并给对应的客户端回复消息
	cout << "TCP_ServerMediator本次接收到的数据为：" << data << endl;
	char s[] = "TCP服务端中介者，竭诚为您服务";
	sendData(s, sizeof(s), from);
}

//发送数据
bool TCP_ServerMediator::sendData(char* data, int len, unsigned long to) {
	return m_pNet->sendData(data, len, to);
}