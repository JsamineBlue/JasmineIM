#include"UDPMediator.h"
#include"../net/UDP.h"
using namespace std;

UDPMediator::UDPMediator(){
	m_pNet = new UDP(this);
}
UDPMediator::~UDPMediator(){
	if (m_pNet) {
		delete m_pNet;
		m_pNet = nullptr;
	}
}

//打开网络
bool UDPMediator::openNet(){
	return m_pNet->initNet();
}

//关闭网络
void UDPMediator::closeNet(){
	m_pNet->unInitNet();
}

//转发数据
void UDPMediator::transmateData(char* data, int len, unsigned long from){
	//测试代码：测试是否收到数据
	cout << "UDP中介者收到的来自UDP的数据为：" << data << endl;
}

//发送数据
bool UDPMediator::sendData(char* data, int len, unsigned long to){
	return m_pNet->sendData(data, len, to);
}