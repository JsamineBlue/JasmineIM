#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include"TCP_Server.h"
#include"../mediator/TCP_ServerMediator.h"
using namespace std;

TCP_Server::TCP_Server(INetMediator* p){
	m_pMediator = p;
}
TCP_Server::~TCP_Server() {}

//初始化网络
bool TCP_Server::initNet() {
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
		cout << "WSAStartup error" << endl;
		return false;
	}

	m_sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (m_sock == INVALID_SOCKET) {
		cout << "socket error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "socket success" << endl;
	}
	
	//绑定端口和IP地址
	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(TCP_PORT);
	addr.sin_addr.S_un.S_addr = ADDR_ANY;
	err = bind(m_sock,(sockaddr*)&addr,sizeof(addr));
	if (err == SOCKET_ERROR) {
		cout << "bind error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "bind success" << endl;
	}

	//监听端口
	err = listen(m_sock, TCP_LISTEN_MAX);
	if (err == SOCKET_ERROR) {
		cout << "listen error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "listen success" << endl;
	}

	//创建接受连接的线程
	m_handle = (HANDLE)_beginthreadex(nullptr,0,&acceptThread,this,0,nullptr);

	return true;
}

// 接受连接的线程函数（循环接受连接）
unsigned __stdcall TCP_Server::acceptThread(void* IpVoid) {
	TCP_Server* pThis = (TCP_Server*)IpVoid;
	sockaddr_in addrClient = {};
	int size = sizeof(addrClient);
	SOCKET sock;
	HANDLE handle;
	unsigned int threadId = 0;
	while (pThis->m_bRunning) {
		// 循环，每次连接成功都会产生一个socket，用来专门跟当前当前客户端通信
		sock = accept(pThis->m_sock, (sockaddr*)&addrClient, &size);
		if (sock == INVALID_SOCKET) {
			cout << "TCP_Server::acceptThread error:" << WSAGetLastError() << endl;
		}
		else {
			cout << "Client IP:" << inet_ntoa(addrClient.sin_addr) << endl;

			// 给当前客户端创建一个接收数据的线程
			handle = (HANDLE)_beginthreadex(nullptr, 0, &recvThread, pThis, 0, &threadId);

			// 保存线程id和socket
			pThis->m_mapThreadIdToSocket[threadId] = sock;

			// 保存线程句柄
			pThis->m_listHandle.push_back(handle);
		}
	}
	return 1;
}

// 接收数据的线程函数
unsigned __stdcall TCP_Server::recvThread(void* IpVoid) {
	TCP_Server* pThis = (TCP_Server*)IpVoid;
	pThis->recvData();
	return 1;
}

//关闭网络
void TCP_Server::unInitNet() {
	if (m_sock != INVALID_SOCKET) {
		closesocket(m_sock);
	}
	WSACleanup();

	//关闭接受连接线程
	m_bRunning = false;
	if (WAIT_TIMEOUT == WaitForSingleObject(m_handle, 5000)) {
		TerminateThread(m_handle, -1);
	}
	CloseHandle(m_handle);
}

//接收数据
void TCP_Server::recvData() {
	Sleep(5);

	//从 map 中取出当前线程对应的 socket
	//获取当前线程的id
	unsigned int threadId = GetCurrentThreadId();
	//根据线程id取出对应socket
	SOCKET sock = INVALID_SOCKET;
	if (m_mapThreadIdToSocket.count(threadId)>0) {
		sock = m_mapThreadIdToSocket[threadId];
	}
	else {
		cout << "TCP_Server::recvData error: No such socket" << endl;
		return;
	}

	int packSize = 0;
	int nRecvNum = 0;
	int nOffset = 0;
	//循环接收数据内容
	while (m_bRunning) {
        nOffset = 0;
		//先接收数据长度
		nRecvNum = recv(sock,(char*)&packSize,sizeof(int),0);
        if (nRecvNum <= 0) {
			cout << "TCP_Server::recvData dataLength error:" <<WSAGetLastError()<< endl;
			break;
		}
		else {
			//再循环接收数据内容
			char* packBuf = new char[nRecvNum];
			while (packSize > 0) {
				nRecvNum = recv(sock, packBuf + nOffset, packSize, 0);
                if (nRecvNum <= 0) {
					cout << "TCP_Server::recvData data error:" << WSAGetLastError() << endl;
				}
				else {
					nOffset += nRecvNum;
					packSize -= nRecvNum;
				}
			}
			//接收完数据包的时候，把数据传给中介者（nOffset 变成包的总长度）
			m_pMediator->transmateData(packBuf,nOffset,sock);
			
		}
	}
}

//发送数据
bool TCP_Server::sendData(char* data, int len, unsigned long to) {

	//校验参数合法性
	if (!data || len <= 0) {
		cout << "TCP_Server::sendData parameter error" << endl;
		return false;
	}
	
	//先发送数据长度
	int nSendNum = 0;
	nSendNum = send(to,(char*)&len,sizeof(int),0);
	if (nSendNum == SOCKET_ERROR) {
		cout << "TCP_Server::sendData dataLength error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		//再发送数据内容
		nSendNum = send(to, data, len, 0);
		if (nSendNum == SOCKET_ERROR) {
			cout << "TCP_Server::sendData data error:" << WSAGetLastError() << endl;
			return false;
		}
	}
	return true;
}