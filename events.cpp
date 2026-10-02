#include <windows.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "tree234.h"
#ifdef __cplusplus
}
#endif

#include "events.h"
#define snew(type) ((type *)malloc(sizeof(type)))
#define sfree(ptr) free(ptr)

struct sockdata {
    SOCKET sock;
    WSAEventData data;
};

static int sockdata_find(void *av, void *bv)
{
    SOCKET *a = (SOCKET *)av;
    struct sockdata *b = (struct sockdata *)bv;
    if (*a < b->sock)
        return -1;
    if (*a > b->sock)
        return +1;
    return 0;
}
static int sockdata_compare(void *av, void *bv)
{
    struct sockdata *a = (struct sockdata *)av;
    return sockdata_find(&a->sock, bv);
}

static tree234 *sockdatatree = NULL;

HANDLE events_mutex;
HINSTANCE events_instance;
HWND events_window;
static const char* events_window_name = "WINSOCK351";

void SetEventData(SOCKET socket, const WSAEventData& data) {
    struct sockdata *sd;
	if (events_mutex == NULL)
		return;
	if (WaitForSingleObject(events_mutex, INFINITE) == WAIT_FAILED)
		return;
    if (!sockdatatree)
        sockdatatree = newtree234(sockdata_compare);
	if (sockdatatree == NULL) {
		ReleaseMutex(events_mutex);
		return;
	}

	sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
    if (!sd) {
        sd = snew(struct sockdata);
        if (sd == NULL) {
            ReleaseMutex(events_mutex);
            return;
        }
        sd->sock = socket;
        sd->data = data;
        if (add234(sockdatatree, sd) != sd) {
            /* already existed (race) or failure: avoid leak */
            sfree(sd);
            /* re-find and update if it now exists */
            sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
            if (sd)
                sd->data = data;
        }
    } else {
        sd->data = data;
    }
	ReleaseMutex(events_mutex);
}

int GetEventData(SOCKET socket, WSAEventData* data) {
    struct sockdata *sd;
	DWORD wait_result;
	if (events_mutex == NULL || data == NULL)
		return -1;
	wait_result = WaitForSingleObject(events_mutex, INFINITE);
	if (wait_result == WAIT_FAILED)
		return -1;
    if (!sockdatatree) {
		ReleaseMutex(events_mutex);
		return -1;
	}

	/* WAIT_OBJECT_0 and WAIT_ABANDONED both mean we own the mutex */
	sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
    if (!sd) {
		ReleaseMutex(events_mutex);
		return -1;
	}

	*data = sd->data;
	ReleaseMutex(events_mutex);

	return 0;
}

void ResetEventData(SOCKET socket) {
	WSAEventData data;
	if (events_mutex == NULL)
		return;
	memset(&data, 0, sizeof(data));
	SetEventData(socket, data);
}

void DeleteEventData(SOCKET socket) {
    struct sockdata *sd;
	DWORD wait_result;
	if (events_mutex == NULL)
		return;
	wait_result = WaitForSingleObject(events_mutex, INFINITE);
	if (wait_result == WAIT_FAILED)
		return;
    if (!sockdatatree) {
		ReleaseMutex(events_mutex);
		return;
	}
	sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
    if (sd) {
		del234(sockdatatree, sd);
		sfree(sd);
	}
	ReleaseMutex(events_mutex);
}

void DeleteEvents() {
    struct sockdata *sd;
	DWORD wait_result;
	if (events_mutex == NULL)
		return;
	wait_result = WaitForSingleObject(events_mutex, INFINITE);
	if (wait_result == WAIT_FAILED)
		return;
	if (sockdatatree != NULL) {
		while ((sd = (struct sockdata *)delpos234(sockdatatree, 0)) != NULL)
		    sfree(sd);
		freetree234(sockdatatree);
		sockdatatree = NULL;
	}
	ReleaseMutex(events_mutex);
}

/* Map a single FD_* bit to an iErrorCode[] index (0..FD_MAX_EVENTS-1). */
static int EventBitToIndex(long eventBit) {
	switch (eventBit) {
	case FD_READ:   return 0;
	case FD_WRITE:  return 1;
	case FD_OOB:    return 2;
	case FD_ACCEPT:  return 3;
	case FD_CONNECT: return 4;
	case FD_CLOSE:   return 5;
#ifdef FD_QOS
	case FD_QOS:    return 6;
#endif
#ifdef FD_GROUP_QOS
	case FD_GROUP_QOS: return 7;
#endif
#ifdef FD_ROUTING_INTERFACE_CHANGE
	case FD_ROUTING_INTERFACE_CHANGE: return 8;
#endif
#ifdef FD_ADDRESS_LIST_CHANGE
	case FD_ADDRESS_LIST_CHANGE: return 9;
#endif
	default: return -1;
	}
}

LRESULT CALLBACK EventsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg > WM_USER) {
		const WSAEVENT event = (WSAEVENT)(uMsg - WM_USER);
		WSAEventData data;
		long netEvent;
		int netError;
		int idx;
		SOCKET s = (SOCKET)wParam;

		memset(&data, 0, sizeof(data));
		/* First event for this socket has no entry yet: don't drop it. */
		GetEventData(s, &data);

		netEvent = WSAGETSELECTEVENT(lParam);
		netError = WSAGETSELECTERROR(lParam);
		data.lNetworkEvents |= netEvent;
		/* lParam carries one event+error per message; store per-event error. */
		idx = EventBitToIndex(netEvent);
		if (idx >= 0 && idx < FD_MAX_EVENTS)
			data.iErrorCode[idx] = netError;
		SetEventData(s, data);

		if (!SetEvent(event))
			return FALSE;
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void SetEventsInstance(HINSTANCE hinstance) {
	events_instance = hinstance;
}

HWND GetEventsWindow() {
	return events_window;
}

int StartupEvents() {
	/* Do not create owned mutex: first waiter must not inherit ownership. */
	events_mutex = CreateMutex(NULL, FALSE, NULL);
	if (events_mutex == NULL)
		return GetLastError();

	// In order to receive WSAAsync events locally we will create an
	// invisible window to receive those messages
	// HWND_MESSAGE didn't exist on NT 3.51 so we have to do it this way
	WNDCLASS wc;

	memset(&wc, 0, sizeof(wc));

	// Register the main window class
	wc.style = 0;
	wc.lpfnWndProc = EventsWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = events_instance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName =  NULL;
	wc.lpszClassName = events_window_name;
	if (!RegisterClass(&wc)) {
		DWORD err = GetLastError();
		/* Already registered (e.g. second WSAStartup without cleanup)? */
		if (err != ERROR_CLASS_ALREADY_EXISTS) {
			CloseHandle(events_mutex);
			events_mutex = NULL;
			return err;
		}
	}
	events_window = CreateWindow(events_window_name, "", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, events_instance, NULL);
	if (events_window == NULL) {
		DWORD err = GetLastError();
		CloseHandle(events_mutex);
		events_mutex = NULL;
		return err;
	}

	return 0;
}

int CleanupEvents() {
	/* Order: window first (no more messages), then tree, then mutex. */
	if (events_window != NULL) {
		DestroyWindow(events_window);
		events_window = NULL;
	}
	UnregisterClass(events_window_name, events_instance);
	DeleteEvents();
	if (events_mutex != NULL) {
		if (!CloseHandle(events_mutex))
			return GetLastError();
		events_mutex = NULL;
	}
	return 0;
}
