#include <winsock.h>

#define WSAEVENT HANDLE
#define LPWSAEVENT LPHANDLE
#ifndef FD_MAX_EVENTS
#define FD_MAX_EVENTS 10
#endif

typedef struct _WSANETWORKEVENTS {
  long lNetworkEvents;
  int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;

struct WSAEventData {
	long lNetworkEvents;
	int iErrorCode[FD_MAX_EVENTS];
};

void SetEventData(SOCKET socket, const WSAEventData& data);
int GetEventData(SOCKET socket, WSAEventData* data);
void ResetEventData(SOCKET socket);
void DeleteEventData(SOCKET socket);
void SetEventsInstance(HINSTANCE hinstance);
HWND GetEventsWindow();
int StartupEvents();
int CleanupEvents();