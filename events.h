#ifndef WINSOCK351_EVENTS_H
#define WINSOCK351_EVENTS_H

#include <winsock.h>

#ifndef WSAEVENT
#define WSAEVENT HANDLE
#endif
#ifndef LPWSAEVENT
#define LPWSAEVENT LPHANDLE
#endif
#ifndef FD_MAX_EVENTS
#define FD_MAX_EVENTS 10
#endif

#ifndef _WSANETWORKEVENTS_DEFINED
#define _WSANETWORKEVENTS_DEFINED
typedef struct _WSANETWORKEVENTS {
  long lNetworkEvents;
  int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, *LPWSANETWORKEVENTS;
#endif

struct WSAEventData {
	long lNetworkEvents;
	int iErrorCode[FD_MAX_EVENTS];
};

void SetEventData(SOCKET socket, const WSAEventData& data);
int GetEventData(SOCKET socket, WSAEventData* data);
void ResetEventData(SOCKET socket);
void DeleteEventData(SOCKET socket);
/* Small-integer message IDs for WSAAsyncSelect. HANDLE values (hence
 * WM_USER+HANDLE) can exceed the user-message range on some systems;
 * these map each WSAEVENT to a small dense id instead. Ids are never
 * reused (table holds at most MAX_EVENT_MSGIDS distinct events). */
UINT EventMsgIdForHandle(WSAEVENT hEvent);
WSAEVENT EventHandleForMsgId(UINT id);
void SetEventsInstance(HINSTANCE hinstance);
HWND GetEventsWindow();
int StartupEvents();
int CleanupEvents();

#endif /* WINSOCK351_EVENTS_H */