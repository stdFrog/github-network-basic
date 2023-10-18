#define UNICODE
#define _WIN32_WINNT 0x0A00
#include "..\..\..\Common.h"

#define BUFSIZE 512
#define SERVERPORT 9000
#define WM_SOCKET (WM_USER+1)

/*
	이 모델은 단일 윈도우 프로시저에서 일반 윈도우 메시지와
	소켓 메시지를 처리하므로 성능 저하의 요인이 된다.

	GUI 환경에서 사용하기 알맞는 형태로(윈도우 메시지) 네트워크 이벤트가 전달되며
	MFC(Microsoft Foundation Class Library)의 소켓 클래스에서 내부적으로 사용된다.
 */


struct SOCKETINFO{
	SOCKET sock;
	char buf[BUFSIZE+1];
	int recvbytes;
	int sendbytes;
	bool recvdelayed;
	SOCKETINFO *next;
};

struct SOCKETINFO *SocketInfoList;
LPCTSTR lpszClass = TEXT("TCP Server Class");

bool AddSocketInfo(SOCKET sock){
	SOCKETINFO *ptr = new SOCKETINFO;
	if(ptr == NULL){
		printf("[Error] Memory Allocation Failure\n");
		return false;
	}
	
	ptr->sock = sock;
	ptr->recvbytes = 0;
	ptr->sendbytes = 0;
	ptr->recvdelayed = 0;
	ptr->next = SocketInfoList;
	SocketInfoList->next = ptr;

	return true;
}

void RemoveSocketInfo(SOCKET sock){
	/* Client Info Display */
	struct sockaddr_in clientaddr;
	int cbAddr = sizeof(clientaddr);
	int retval;
	getpeername(sock, (struct sockaddr*)&clientaddr, &cbAddr);

	printf("[TCP Server] Disconnected: IP Addr= %s, Port= %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));

	SOCKETINFO *ptr = SocketInfoList;
	SOCKETINFO *prev = NULL;

	while(ptr){
		if(ptr->sock == sock){
			if(prev){
				prev->next = ptr->next;
			}else{
				SocketInfoList = ptr->next;
			}
			closesocket(ptr->sock);
			delete ptr;
			return;
		}
		prev = ptr;
		ptr = ptr->next;
	}
}

SOCKETINFO *GetSocketInfo(SOCKET sock){
	SOCKETINFO *ptr = SocketInfoList;

	while(ptr){
		if(ptr->sock == sock){
			return ptr;
		}
		ptr = ptr->next;
	}
	return NULL;
}

void ProcessSocketMessage(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	int retval;
	SOCKET client_sock;
	struct sockaddr_in clientaddr;
	int cbAddr;
	SOCKETINFO *ptr;

	if(WSAGETSELECTERROR(lParam)){
		err_display(WSAGETSELECTERROR(lParam));
		RemoveSocketInfo(wParam);
		return;
	}
	
	switch(WSAGETSELECTEVENT(lParam)){
		case FD_ACCEPT:
			cbAddr = sizeof(clientaddr);
			client_sock = accept(wParam, (struct sockaddr*)&clientaddr, &cbAddr);
			if(client_sock == INVALID_SOCKET){err_display("accept()"); return;}

			printf("\n[TCP Server] Client Accept: IP Addr= %s, Port= %d\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port));
			
			AddSocketInfo(client_sock);
			retval = WSAAsyncSelect(client_sock, hWnd, WM_SOCKET, FD_READ | FD_WRITE | FD_CLOSE);
			if(retval == SOCKET_ERROR){
				err_display("WSAAsyncSelect()");
				RemoveSocketInfo(client_sock);
			}
			break;

		case FD_READ:
			ptr = GetSocketInfo(wParam);

			/* 이전에 받은 데이터를 아직 처리하지 못한 경우 0 이상의 값을 갖고 있다. */
			/* 이때는 대응 함수를 호출하지 않고 바로 리턴처리 하여 */
			/* FD_READ 메시지가 더이상 발생하지 않도록 해줘야 남은 데이터를 처리할 수 있다 */
			if(ptr->recvbytes > 0){ ptr->recvdelayed = true; return; }

			retval = recv(ptr->sock, ptr->buf, BUFSIZE, 0);
			if(retval == SOCKET_ERROR){err_display("recv()"); RemoveSocketInfo(wParam); return;}

			ptr->recvbytes = retval;
			ptr->buf[retval] = '\0';
			cbAddr = sizeof(clientaddr);
			getpeername(wParam, (struct sockaddr*)&clientaddr, &cbAddr);
			printf("[TCP /%s:%d] %s\n", inet_ntoa(clientaddr.sin_addr), ntohs(clientaddr.sin_port), ptr->buf);
			/* break; 하지 않는다. 에코 서버이므로 받은 데이터를 그대로 곧장 클라이언트에 전달한다. */

		case FD_WRITE:
			ptr = GetSocketInfo(wParam);
			if(ptr->recvbytes <= ptr->sendbytes){
				return;
			}

			retval = send(ptr->sock, ptr->buf + ptr->sendbytes, ptr->recvbytes - ptr->sendbytes, 0);
			if(retval == SOCKET_ERROR){err_display("send()"); RemoveSocketInfo(wParam); return;}

			ptr->sendbytes += retval;
			if(ptr->recvbytes == ptr->sendbytes){
				ptr->recvbytes = ptr->sendbytes = 0;
				if(ptr->recvdelayed){
					ptr->recvdelayed = false;
					PostMessage(hWnd, WM_SOCKET, wParam, FD_READ);
				}
			}
			break;

		case FD_CLOSE:
			RemoveSocketInfo(wParam);
			break;
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lParam){
	switch(iMessage){
		case WM_SOCKET:
			ProcessSocketMessage(hWnd, iMessage, wParam, lParam);
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}

	return (DefWindowProc(hWnd, iMessage, wParam, lParam));
}

int main()
{
	WNDCLASS wc={
		CS_HREDRAW | CS_VREDRAW,
		WndProc,
		0,0,
		GetModuleHandle(NULL),
		NULL, LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)(COLOR_WINDOW+1),
		NULL,
		lpszClass
	};
	RegisterClass(&wc);

	HWND hWnd = CreateWindow(
			lpszClass,
			lpszClass,
			WS_OVERLAPPEDWINDOW,
			0,0,600,220,
			NULL,
			NULL,
			GetModuleHandle(NULL),
			NULL);
	ShowWindow(hWnd, SW_SHOWNORMAL);

	WSADATA wsa;
	if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){return 1;}

	SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_sock == INVALID_SOCKET){err_quit("socket()");}

	struct sockaddr_in serveraddr={0};
	serveraddr.sin_port = SERVERPORT;
	serveraddr.sin_family = AF_INET;

	int retval = bind(listen_sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if(retval == SOCKET_ERROR){err_quit("bind()");}

	retval = listen(listen_sock, SOMAXCONN);
	if(retval == SOCKET_ERROR){err_quit("listen()");}

	/* Socket Mode Changed(NonBlock Socket) */
	retval = WSAAsyncSelect(listen_sock, hWnd, WM_SOCKET, FD_ACCEPT | FD_CLOSE);
	if(retval == SOCKET_ERROR){err_quit("WSAAsyncSelect()");}

	MSG msg;
	while(GetMessage(&msg, nullptr, 0,0)){
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	WSACleanup();
	return (int)msg.wParam;
}
