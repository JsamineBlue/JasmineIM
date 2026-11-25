#include"TCP_ClientMediator.h"
#include"../net/TCP_Client.h"

TCP_ClientMediator::TCP_ClientMediator() {
	m_pNet = new TCP_Client(this);
}
TCP_ClientMediator::~TCP_ClientMediator() {
	if (m_pNet) {
		delete m_pNet;
		m_pNet = nullptr;
	}
}

//打开网络
bool TCP_ClientMediator::openNet() {
	return m_pNet->initNet();
}

//关闭网络
void TCP_ClientMediator::closeNet() {
	m_pNet->unInitNet();
}

//转发数据
void TCP_ClientMediator::transmateData(char* data, int len, unsigned long from) {
    //把数据传到 kernel
    Q_EMIT signals_recvServerData(data,len,from);
}

//发送数据
bool TCP_ClientMediator::sendData(char* data, int len, unsigned long to) {
	return m_pNet->sendData(data, len, to);
}