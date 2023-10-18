#define _WIN32_WINNT 0x0A00
#include <ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>
#include <stdio.h>
#define SERVERPORT 9000
#define BUFSIZE 512

struct SOCKETINFO
{
	OVERLAPPED ov;
	SOCKET sock;
	char buf[BUFSIZE + 1];
	int rb;
	int sb;
	WSABUF wsabuf;
};

DWORD WINAPI Thread(LPVOID lpArg);

int main()
{
	int ret;

	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){return -1;}

	HANDLE hComplete = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,0);
	if(hComplete == NULL) {return -1;}

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	for(int i=0; i<si.dwNumberOfProcessors * 2; i++)
	{
		DWORD dwThread;
		HANDLE hThread = CreateThread(NULL, 0, Thread, hComplete, 0, &dwThread);
		if(hThread == NULL){return -1;}
		CloseHandle(hThread);
	}

	SOCKET Listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(Listen == INVALID_SOCKET){printf("error : socket()\n"); return 0;}

	struct sockaddr_in serveraddr = {0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);
	ret = bind(Listen, (struct sockaddr*)&serveraddr, sizeof(serveraddr));

	if(ret == SOCKET_ERROR){printf("error: bind()\n"); return -1;}

	ret = listen(Listen, SOMAXCONN);
	if(ret == SOCKET_ERROR){printf("error: listen()\n"); return -1;}

	SOCKET client;
	struct sockaddr_in clientaddr;
	int length;
	DWORD recvbytes, flags;

	while(1)
	{
		length = sizeof(clientaddr);
		client = accept(Listen, (struct sockaddr*)&clientaddr, &length);

		if(client == INVALID_SOCKET)
		{
			printf("error: accept()\n");
			break;
		}

		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
		printf("\n[TCP Server] Accept: IP Addr = %s, Port = %d\n", addr, ntohs(clientaddr.sin_port));

		// 소켓과 입출력 완료 포트를 연결한다.
		CreateIoCompletionPort((HANDLE)client, hComplete, client, 0);

		// 정보 구조 할당하고 관리
		SOCKETINFO *ptr = new SOCKETINFO;
		if(ptr == NULL){break;}

		memset(&ptr->ov, 0, sizeof(ptr->ov));
		ptr->sock = client;
		ptr->rb = ptr->sb = 0;
		ptr->wsabuf.buf = ptr->buf;
		ptr->wsabuf.len = BUFSIZE;

		flags = 0;
		ret = WSARecv(client, &ptr->wsabuf, 1, &recvbytes, &flags, &ptr->ov, NULL);
		if(ret == SOCKET_ERROR)
		{
			if(WSAGetLastError() != WSA_IO_PENDING)
			{
				printf("error: WSARecv()");
			}
			continue;
		}
	}

	WSACleanup();

	return 0;
}

DWORD WINAPI Thread(LPVOID lpArg)
{
	int ret;
	HANDLE hComplete = (HANDLE)lpArg;

	while(1)
	{
		DWORD dwTrans;
		SOCKET client;
		SOCKETINFO *ptr;

		// 완료 대기
		ret = GetQueuedCompletionStatus(hComplete, &dwTrans, (PULONG_PTR)&client, (LPOVERLAPPED*)&ptr, INFINITE);

		// 클라 정보(IP 주소, 포트) 얻어옴
		struct sockaddr_in clientaddr;
		int size = sizeof(clientaddr);
		getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &size);
		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));

		if(ret == 0 || dwTrans == 0)
		{
			printf("[TCP Server] Client Exit : IP Addr = %s, Port = %d\n", addr, ntohs(clientaddr.sin_port));
			closesocket(ptr->sock);
			delete ptr;
			continue;
		}

		if(ptr->rb == 0)
		{
			ptr->rb = dwTrans;
			ptr->sb = 0;

			ptr->buf[ptr->rb] = 0;
			printf("[TCP /%s : %d] %s\n", addr, ntohs(clientaddr.sin_port), ptr->buf);
		}else
		{
			ptr->sb += dwTrans;
		}

		if(ptr->rb > ptr->sb)
		{
			memset(&ptr->ov, 0, sizeof(ptr->ov));
			ptr->wsabuf.buf = ptr->buf + ptr->sb;
			ptr->wsabuf.len = ptr->rb - ptr->sb;

			DWORD sendbytes;
			ret = WSASend(ptr->sock, &ptr->wsabuf, 1, &sendbytes, 0, &ptr->ov, NULL);
			if(ret == SOCKET_ERROR)
			{
				if(WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("error: WSASend()\n");
				}
				continue;
			}
		}else
		{
			ptr->rb = 0;
			memset(&ptr->ov, 0, sizeof(ptr->ov));
			ptr->wsabuf.buf = ptr->buf;
			ptr->wsabuf.len = BUFSIZE;

			DWORD recvbytes;
			DWORD flags = 0;

			ret = WSARecv(ptr->sock, &ptr->wsabuf, 1, &recvbytes, &flags, &ptr->ov, NULL);

			if(ret == SOCKET_ERROR)
			{
				if(WSAGetLastError() != WSA_IO_PENDING)
				{
					printf("error: WSARecv()\n");
				}
				continue;
			}
		}
	}

	return 0;
}
