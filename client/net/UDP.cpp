#include"UDP.h"
#include"../mediator/UDPMediator.h"
using namespace std;

UDP::UDP(INetMediator* p) {
	m_pMediator = p;
}

UDP::~UDP() {

}

//初始化网络
bool UDP::initNet() {
	//加载库
	WORD version = MAKEWORD(2, 2);
	WSADATA data = {};
	int err = WSAStartup(version,&data);
	if (err != 0) {
		cout << "WSAStartup fail" << endl;
		return false;
	}

	if (HIBYTE(data.wVersion) == 2 && LOBYTE(data.wVersion) == 2) {
		cout << "WSAStartup success" << endl;
	}
	else {
		cout << "WSAStartup version error" << endl;
		return false;
	}

	//创建套接字
	m_sock = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if (m_sock == INVALID_SOCKET) {
		cout << "socket error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "socket success" << endl;
	}

	//绑定IP和端口
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	//魔鬼数字：数字会增加代码阅读的难度，把数字定义成宏
	addr.sin_port = htons(UDP_PORT);
	addr.sin_addr.S_un.S_addr = ADDR_ANY;
	err = bind(m_sock, (sockaddr*)&addr, sizeof(addr));
	if (err == SOCKET_ERROR) {
		cout << "bind error" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "bind success" << endl;
	}

	//创建接收数据的线程
	m_handle =(HANDLE)_beginthreadex(nullptr,0, &recvThread/*函数*/,this/*参数列表*/,0,nullptr);
	return true;
}

//线程函数（接收数据，调用recvData）
unsigned __stdcall UDP::recvThread(void* IpVoid) {
	UDP* pThis = (UDP*)IpVoid;
	pThis->recvData();
	return 1;
}

//关闭网络
void UDP::unInitNet(){
	if (m_sock && m_sock != INVALID_SOCKET) {
		closesocket(m_sock);
	}
	WSACleanup();

	//回收线程资源
	//线程结束工作
	m_bRunning = false;
	if (WAIT_TIMEOUT == WaitForSingleObject(m_handle, 5000)) {
		TerminateThread(m_handle, -1);
	}

	//回收句柄
	CloseHandle(m_handle);
	m_handle = nullptr;
}

//接收数据
void UDP::recvData(){
	int nRecvNum = 0;
	char recvBuf[4096] = "";
	sockaddr_in addrFrom = {};
	int addrSize = sizeof(addrFrom);
	while (m_bRunning) {
		nRecvNum = recvfrom(m_sock, recvBuf,sizeof(recvBuf),0,(sockaddr*)&addrFrom, &addrSize);
		if (nRecvNum <= 0) {
			cout << "UDP::recvData error" << WSAGetLastError() << endl;
			break;
		}
		else {
			//接收数据成功
			//申请空间
			char* packBuf = new char[nRecvNum];

			//拷贝数据
			memcpy(packBuf, recvBuf, nRecvNum);
			//将接收到的数据传给中介者
			m_pMediator->transmateData(packBuf, nRecvNum, addrFrom.sin_addr.S_un.S_addr);
		}
	}
}

//发送数据
bool UDP::sendData(char* data, int len, unsigned long to){
	//判断参数合法性
	if (!data || len <= 0) {
		cout << "UDP::sendData parameter error" << endl;
		return false;
	}

	//发送数据
	sockaddr_in addrTo = {};
	addrTo.sin_family = AF_INET;
	addrTo.sin_port = htons(UDP_PORT);
	addrTo.sin_addr.S_un.S_addr = to;
	int nSendNum = sendto(m_sock, data, len, 0, (sockaddr*)&addrTo, sizeof(addrTo));
	if (nSendNum == SOCKET_ERROR) {
		cout << "UDP::sendData error:" << WSAGetLastError() << endl;
		return false;
	}
	return true;
}
