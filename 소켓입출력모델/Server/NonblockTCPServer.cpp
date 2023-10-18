#define _WIN32_WINNT 0x0A00
#include "..\..\..\Common.h"
#include <ws2tcpip.h>
#include <windows.h>
#include <winsock2.h>

#define SERVERPORT 9000
#define BUFSIZE 512

int main()
{
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 1;

	int retval;

	SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock == INVALID_SOCKET){err_quit("socket()");}

	u_long ulflag = 1;
	retval = ioctlsocket(sock, FIONBIO, &ulflag);
	if(retval == SOCKET_ERROR){err_quit("ioctlsocket()");}

	struct sockaddr_in serveraddr={0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(SERVERPORT);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

	retval = bind(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(retval == SOCKET_ERROR){err_quit("bind()");}

	retval = listen(sock, SOMAXCONN);
	if(retval == SOCKET_ERROR){err_quit("listen()");}

	SOCKET client_sock;
	struct sockaddr_in clientaddr;
	int cbAddr;
	char buf[BUFSIZE+1];

	while(1){
		ACCEPT_AGAIN:
		cbAddr = sizeof(clientaddr);
		client_sock = accept(sock, (struct sockaddr*)&clientaddr, &cbAddr);
		if(client_sock == SOCKET_ERROR){
			if(WSAGetLastError() == WSAEWOULDBLOCK){
				goto ACCEPT_AGAIN;
			}
			err_display("accept()");
			break;
		}

		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
		printf("\n[TCP Server] Client Accept: IP Addr= %s, Port= %d\n", addr, ntohs(clientaddr.sin_port));

		while(1){
			RECEIVE_AGAIN:
			retval = recv(client_sock, buf, BUFSIZE, 0);
			if(retval == SOCKET_ERROR){
				if(WSAGetLastError() == WSAEWOULDBLOCK){
					goto RECEIVE_AGAIN;
				}
				err_display("recv()");
				break;
			}else if(retval == 0){
				break;
			}

			buf[retval] = '\0';
			printf("[TCP /%s:%d] %s\n", addr, ntohs(clientaddr.sin_port), buf);

			SEND_AGAIN:
			retval = send(client_sock, buf, retval, 0);
			if(retval == SOCKET_ERROR){
				if(WSAGetLastError() == WSAEWOULDBLOCK){
					goto SEND_AGAIN;
				}
				err_display("send()");
				break;
			}
		}

		closesocket(client_sock);
		printf("[TCP Server] Client Exit: IP Addr= %s, Port= %d\n", addr, ntohs(clientaddr.sin_port));
	}
	
	closesocket(sock);

	WSACleanup();

	return 0;
}
