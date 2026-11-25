#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include"TCP_Client.h"
#include"../mediator/TCP_ClientMediator.h"
#include <cstdint>
#include <memory>

using namespace std;

TCP_Client::TCP_Client(INetMediator* p) {
	m_pMediator = p;
}
TCP_Client::~TCP_Client() {
    unInitNet();
}

//初始化网络
bool TCP_Client::initNet() {
	//打开库
	WORD version = MAKEWORD(2, 2);
	WSADATA data = {};
	int err = WSAStartup(version,&data);
	if (err != 0) {
		cout << "WSAStartup fail" << endl;
		return false;
	}

    m_wsaStarted = true;

	if (HIBYTE(data.wVersion) == 2 && LOBYTE(data.wVersion) == 2) {
		cout << "WSAStartup success" << endl;
	}
	else {
		cout << "WSAStartup error" << endl;
		return false;
	}

	//创建套接字
	m_sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	if (m_sock == INVALID_SOCKET) {
		cout << "socket error:" << WSAGetLastError() << endl;
		return false;
	}
	else {
		cout << "socket success" << endl;
	}

	//连接服务端
	sockaddr_in addrServer = {};
	addrServer.sin_family = AF_INET;
	addrServer.sin_port = htons(TCP_PORT);
    addrServer.sin_addr.S_un.S_addr = inet_addr("192.168.219.148");
	err = connect(m_sock,(sockaddr*)&addrServer,sizeof(addrServer));
	if (err == SOCKET_ERROR) {
		cout << "connect error" << WSAGetLastError() << endl;
		return false;
	}

	//创建接收数据的线程
    m_handle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,&recvThread,this,0,nullptr));

    if (!m_handle) {
        unInitNet();
        return false;
    }

    return true;
}

//接收数据的线程函数
unsigned __stdcall TCP_Client::recvThread(void* IpVoid) {
	TCP_Client* pThis = (TCP_Client*)IpVoid;
	pThis->recvData();
	return 1;
}

//关闭网络
void TCP_Client::unInitNet() {
    // 唤醒正在阻塞的 recv()，先不修改 socket 成员。
    if (m_sock != INVALID_SOCKET) {
        shutdown(m_sock, SD_BOTH);
    }

    // 接收函数遇到断开会正常返回，不强制终止线程。
    if (m_handle) {
        WaitForSingleObject(m_handle, INFINITE);
        CloseHandle(m_handle);
        m_handle = nullptr;
    }

    // 等线程退出后，再关闭并重置 socket。
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    if (m_wsaStarted) {
        WSACleanup();
        m_wsaStarted = false;
    }
}

//接收数据
void TCP_Client::recvData() {
    // TCP 一次 recv() 不保证收到全部指定字节。
    auto readExactly = [this](char* buffer, int length) {
        int offset = 0;

        while (offset < length) {
            const int received = recv(m_sock,buffer + offset,length - offset,0);

            if (received <= 0) {
                return false;
            }

            offset += received;
        }

        return true;
    };

    while (true) {
        unsigned char header[4]{};

        if (!readExactly(reinterpret_cast<char*>(header),sizeof(header))) {
            return;
        }

        const std::uint32_t bodyLength = static_cast<std::uint32_t>(header[0]) |
            (static_cast<std::uint32_t>(header[1]) << 8) |
            (static_cast<std::uint32_t>(header[2]) << 16) |
            (static_cast<std::uint32_t>(header[3]) << 24);

        if (bodyLength < 4 || bodyLength > 64 * 1024) {
            std::cerr << "Invalid received packet length\n";
            return;
        }

        auto buffer = std::make_unique<char[]>(bodyLength);

        if (!readExactly(buffer.get(),static_cast<int>(bodyLength))) {
            // 未接收完整的缓冲区自动释放。
            return;
        }

        // 完整包的所有权仍按原流程交给 Kernel。
        m_pMediator->transmateData(buffer.release(),static_cast<int>(bodyLength),static_cast<unsigned long>(m_sock));
    }
}

//发送数据
bool TCP_Client::sendData(char* data, int len, unsigned long to) {
	//校验参数合法性
	if (!data || len <= 0) {
		cout << "TCP_Client::sendData parameter error" << endl;
		return false;
	}

	//发送数据
	//先发数据长度
	int nSendNum = send(m_sock,(char*)&len,sizeof(int),0);
	if (nSendNum == SOCKET_ERROR) {
		cout << "TCP_Client::sendData dataLength error:" << WSAGetLastError() << endl;
		return false;
	}
	//再发数据内容
	nSendNum = send(m_sock, data, len, 0);
	if (nSendNum == SOCKET_ERROR) {
		cout << "TCP_Client::sendData data error:" << WSAGetLastError() << endl;
		return false;
	}
	return true;
}