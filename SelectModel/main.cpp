#define _WIN32_WINNT 0x0A00
#include "..\..\Common.h"

/*
	이 모델은 모든 운영체제에서 사용할 수 있다는 장점이 있다.
	범용/이식성이 높으며 두 운영체제를 모두 지원해야하는 서버가 필요하다면
	유일하게(?) 사용할 수 있는 모델이다.

	소켓이 64개로 제한되어 있다.
	또, 하위 호환성을 위해 존재하는데 현재는 더 향상된 기능을 제공하는
	좋은 모델이 사용된다.
 */

#define SERVERPORT 9000
#define BUFSIZE 512

struct SOCKETINFO{
	SOCKET sock;
	char buf[BUFSIZE+1];
	int recvbytes;
	int sendbytes;
};

int nTotalSockets;
struct SOCKETINFO *SocketInfoArray[FD_SETSIZE];

bool AddSocketInfo(SOCKET sock);
void RemoveSocketInfo(int nIndex);

int main()
{
	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){ return 1; }

	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_sock == INVALID_SOCKET){err_quit("socket()");}

	struct sockaddr_in serveraddr={0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(SERVERPORT);

	int retval = bind(listen_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(retval == SOCKET_ERROR){err_quit("bind()");}

	u_long on = 1;
	retval = ioctlsocket(listen_sock, FIONBIO, &on);
	if(retval == SOCKET_ERROR){err_quit("ioctlsocket()");}

	retval = listen(listen_sock, SOMAXCONN);
	if(retval == SOCKET_ERROR){err_quit("listen()");}

	fd_set wset, rset;
	int cbAddr, nReady;
	struct sockaddr_in clientaddr;
	SOCKET client_sock;

	while(1){
		/* 셋 초기화 후 listen 대기 소켓 readset에 넣음 */
		FD_ZERO(&wset);
		FD_ZERO(&rset);
		FD_SET(listen_sock, &rset);

		/* 전체 탐색해서 알맞는 소켓에 넣음 */
		for(int i=0; i<nTotalSockets; i++){
			if(SocketInfoArray[i]->recvbytes > SocketInfoArray[i]->sendbytes){
				FD_SET(SocketInfoArray[i]->sock, &wset);
			}else{
				FD_SET(SocketInfoArray[i]->sock, &rset);
			}
		}

		/* 최초 실행시 이 구문부터 시작됨 */
		/* select 함수는 맨 뒤의 인수값에 따라 동작이 바뀐다. */
		/* NULL을 주면 소켓셋에 준비된 소켓이 하나라도 있을 때 리턴한다. */
		nReady = select(0, &rset, &wset, NULL, NULL);
		if(nReady == SOCKET_ERROR){err_quit("select()");}

		// 바로 실행됨
		if(FD_ISSET(listen_sock, &rset)){
			cbAddr = sizeof(clientaddr);
			client_sock = accept(listen_sock, (struct sockaddr*)&clientaddr, &cbAddr);
			if(client_sock == INVALID_SOCKET){err_display("accept()"); break;}
			else{
				char addr[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &clientaddr.sin_addr, addr, sizeof(addr));
				printf("\n[TCP Server] Accept: IP Addr= %s, Port= %d\n", addr, ntohs(clientaddr.sin_port));

				/* 클라이언트 소켓 생성 후 알림 구문과 함께 배열에 해당 소켓을 추가한다. */
				if(!AddSocketInfo(client_sock)){
					closesocket(client_sock);
				}
			}

			if(--nReady <= 0){continue;}
		}
		for(int i=0; i<nTotalSockets; i++){
			SOCKETINFO *ptr = SocketInfoArray[i];

			if(FD_ISSET(ptr->sock, &rset)){
				retval = recv(ptr->sock, ptr->buf, BUFSIZE, 0);

				if(retval == SOCKET_ERROR){
					err_display("recv()");
					RemoveSocketInfo(i);
				}else if(retval == 0){
					RemoveSocketInfo(i);
				}else{
					ptr->recvbytes = retval;
					cbAddr = sizeof(clientaddr);
					getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &cbAddr);

					ptr->buf[ptr->recvbytes] = '\0';
					char IPAddr[INET_ADDRSTRLEN];
					inet_ntop(AF_INET, &clientaddr.sin_addr, IPAddr, sizeof(IPAddr));
					printf("[TCP /%s:%d] %s\n", IPAddr, ntohs(clientaddr.sin_port), ptr->buf);
				}
			}

			if(FD_ISSET(ptr->sock, &wset)){
				retval = send(ptr->sock, ptr->buf + ptr->sendbytes, ptr->recvbytes - ptr->sendbytes, 0);
				if(retval == SOCKET_ERROR){
					err_display("send()");
					RemoveSocketInfo(i);
				}else{
					ptr->sendbytes += retval;
					if(ptr->recvbytes == ptr->sendbytes){
						ptr->recvbytes = ptr->sendbytes = 0;
					}
				}
			}
		}
	}

	closesocket(listen_sock);

	WSACleanup();
	
	return 0;
}

bool AddSocketInfo(SOCKET sock){
	if(nTotalSockets > FD_SETSIZE){
		printf("[Error] Can not be added\n");
		return false;
	}

	SOCKETINFO *ptr = new SOCKETINFO;

	if(ptr==NULL){
		printf("[Error] Allocation Failure\n");
		return false;
	}

	ptr->sock = sock;
	ptr->recvbytes = 0;
	ptr->sendbytes = 0;

	SocketInfoArray[nTotalSockets++] = ptr;

	return true;
}

void RemoveSocketInfo(int nIndex){
	SOCKETINFO *ptr = SocketInfoArray[nIndex];

	char IPAddr[INET_ADDRSTRLEN];
	struct sockaddr_in clientaddr;
	int cbAddr = sizeof(clientaddr);

	getpeername(ptr->sock, (struct sockaddr*)&clientaddr, &cbAddr);
	inet_ntop(AF_INET, &clientaddr.sin_addr, IPAddr, sizeof(IPAddr));
	
	printf("[TCP Server] Client Exit: IP Addr= %s, Port= %d\n", IPAddr, ntohs(clientaddr.sin_port));

	closesocket(ptr->sock);
	delete ptr;

	if(nIndex != (nTotalSockets-1)){
		SocketInfoArray[nIndex] = SocketInfoArray[nTotalSockets-1];
	}
	--nTotalSockets;
}
