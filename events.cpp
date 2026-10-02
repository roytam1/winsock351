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

/* The WSAAsyncSelect notifications must be dispatched on the thread that owns
 * the window. Console apps (e.g. plink) never pump messages, so the window
 * lives on a dedicated helper thread with its own message loop. */
static HANDLE events_thread = NULL;
static DWORD events_thread_id = 0;
static HANDLE events_ready = NULL; /* manual-reset: signaled when window up/failed */
static DWORD events_startup_err = 0;
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

/* Helper-thread entry: creates the notification window, then pumps messages
 * until WM_QUIT. Runs on its own thread so console apps that never call
 * GetMessage/DispatchMessage still get their WSAEVENTs signaled. */
static DWORD WINAPI EventsThreadProc(LPVOID param) {
	MSG msg;
	(void)param;

	events_window = CreateWindow(events_window_name, "", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, events_instance, NULL);
	if (events_window == NULL)
		events_startup_err = GetLastError();
	SetEvent(events_ready);

	if (events_window == NULL)
		return 1;

	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	/* Tear down our own window; class/mutex are handled by CleanupEvents. */
	DestroyWindow(events_window);
	events_window = NULL;
	return 0;
}

int StartupEvents() {
	HANDLE thread;
	DWORD tid;

	/* Do not create owned mutex: first waiter must not inherit ownership. */
	events_mutex = CreateMutex(NULL, FALSE, NULL);
	if (events_mutex == NULL)
		return GetLastError();

	// In order to receive WSAAsync events we need a window to receive those
	// messages. HWND_MESSAGE didn't exist on NT 3.51 so we create an
	// invisible WS_POPUP window. It must live on a thread with a message
	// loop, and console apps never pump messages, so use a helper thread.

	// Register the window class (process-wide, any thread may do this).
	{
		WNDCLASS wc;
		memset(&wc, 0, sizeof(wc));
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
	}

	events_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (events_ready == NULL) {
		DWORD err = GetLastError();
		CloseHandle(events_mutex);
		events_mutex = NULL;
		return err;
	}
	events_startup_err = 0;
	events_window = NULL;

	thread = CreateThread(NULL, 0, EventsThreadProc, NULL, 0, &tid);
	if (thread == NULL) {
		DWORD err = GetLastError();
		CloseHandle(events_ready);
		events_ready = NULL;
		CloseHandle(events_mutex);
		events_mutex = NULL;
		return err;
	}
	/* Wait until the helper thread has created the window (or failed). */
	WaitForSingleObject(events_ready, INFINITE);
	CloseHandle(events_ready);
	events_ready = NULL;

	if (events_startup_err != 0) {
		CloseHandle(thread);
		CloseHandle(events_mutex);
		events_mutex = NULL;
		return events_startup_err;
	}

	/* Hand over ownership; CleanupEvents() will stop the thread. */
	events_thread = thread;
	events_thread_id = tid;

	return 0;
}

int CleanupEvents() {
	/* Stop the helper thread first so no more messages arrive. */
	if (events_thread != NULL) {
		/* The thread owns the window and destroys it before exiting. */
		PostThreadMessage(events_thread_id, WM_QUIT, 0, 0);
		WaitForSingleObject(events_thread, 5000);
		CloseHandle(events_thread);
		events_thread = NULL;
		events_thread_id = 0;
		events_window = NULL;
	} else if (events_window != NULL) {
		/* Paranoia: window without thread (shouldn't happen). */
		DestroyWindow(events_window);
		events_window = NULL;
	}
	UnregisterClass(events_window_name, events_instance);
	DeleteEvents();
	if (events_ready != NULL) {
		CloseHandle(events_ready);
		events_ready = NULL;
	}
	if (events_mutex != NULL) {
		if (!CloseHandle(events_mutex))
			return GetLastError();
		events_mutex = NULL;
	}
	return 0;
}
