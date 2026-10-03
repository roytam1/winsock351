#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
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

#if defined(_DEBUG) || defined(DEBUG)
/* Same trace file as winsock2.cpp DebugLog: proves async messages arrive. */
static void EventLog(const char* fmt, ...) {
	va_list args;
	char buf[256];
	char tmp[MAX_PATH];
	char path[MAX_PATH];
	FILE* f;
	va_start(args, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, args);
	buf[sizeof(buf) - 1] = '\0';
	va_end(args);
	tmp[0] = '\0';
	if (GetTempPath(sizeof(tmp), tmp) == 0 || tmp[0] == '\0')
		strcpy(tmp, ".\\");
	_snprintf(path, sizeof(path) - 1, "%sWINSOCK351.LOG", tmp);
	path[sizeof(path) - 1] = '\0';
	f = fopen(path, "a");
	if (f != NULL) {
		fprintf(f, "[%lu] %s\n", GetTickCount(), buf);
		fclose(f);
	}
}
#else
#define EventLog (void)0
#endif

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
/* Set by CleanupEvents; polled by the helper loop. Plain LONG + aligned
 * access is atomic on x86; set uses InterlockedExchange. */
static volatile LONG events_stop = 0;
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

/* WSAAsyncSelect message ids. HANDLE values must not be baked into window
 * messages (WM_USER+HANDLE can leave the user-message range and be rejected,
 * e.g. NT 3.51 handle values). Instead each distinct WSAEVENT gets a small
 * dense id; the async message is WM_USER+id. Entries are never freed: apps
 * use few events, and freeing would risk id reuse against live selects. */
#define MAX_EVENT_MSGIDS 1024
static WSAEVENT event_msg_handles[MAX_EVENT_MSGIDS];
static UINT event_msg_count = 0; /* ids are index+1; 0 means "none" */

UINT EventMsgIdForHandle(WSAEVENT hEvent) {
	UINT i;
	UINT id = 0;
	if (hEvent == NULL)
		return 0;
	if (events_mutex == NULL)
		return 0;
	if (WaitForSingleObject(events_mutex, INFINITE) == WAIT_FAILED)
		return 0;
	for (i = 0; i < event_msg_count; i++) {
		if (event_msg_handles[i] == hEvent) {
			id = i + 1;
			break;
		}
	}
	if (id == 0) {
		if (event_msg_count >= MAX_EVENT_MSGIDS) {
			ReleaseMutex(events_mutex);
			return 0;
		}
		event_msg_handles[event_msg_count] = hEvent;
		event_msg_count++;
		id = event_msg_count;
	}
	ReleaseMutex(events_mutex);
	return id;
}

WSAEVENT EventHandleForMsgId(UINT id) {
	WSAEVENT h = NULL;
	if (id == 0 || id > MAX_EVENT_MSGIDS)
		return NULL;
	if (events_mutex == NULL)
		return NULL;
	if (WaitForSingleObject(events_mutex, INFINITE) == WAIT_FAILED)
		return NULL;
	if (id <= event_msg_count)
		h = event_msg_handles[id - 1];
	ReleaseMutex(events_mutex);
	return h;
}

LRESULT CALLBACK EventsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg > WM_USER) {
		WSAEVENT event = EventHandleForMsgId((UINT)(uMsg - WM_USER));
		WSAEventData data;
		long netEvent;
		int netError;
		int idx;
		SOCKET s = (SOCKET)wParam;

		if (event == NULL)
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		memset(&data, 0, sizeof(data));
		/* First event for this socket has no entry yet: don't drop it. */
		GetEventData(s, &data);

		netEvent = WSAGETSELECTEVENT(lParam);
		netError = WSAGETSELECTERROR(lParam);
		EventLog("EventsWndProc: sock=%d msg=0x%X ev=0x%X err=%d",
			s, uMsg, netEvent, netError);
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
 * until told to stop. Runs on its own thread so console apps that never call
 * GetMessage/DispatchMessage still get their WSAEVENTs signaled.
 *
 * Uses PeekMessage/WaitMessage (not GetMessage): every exit path is explicit
 * and logged, so a spontaneously-dying helper can't silently break event
 * delivery the way a GetMessage <= 0 return would. */
static DWORD WINAPI EventsThreadProc(LPVOID param) {
	MSG msg;
	(void)param;

	events_window = CreateWindow(events_window_name, "", WS_POPUP,
		0, 0, 0, 0, NULL, NULL, events_instance, NULL);
	if (events_window == NULL)
		events_startup_err = GetLastError();
	EventLog("EventsThread: window=0x%X err=%lu", events_window, events_startup_err);
	SetEvent(events_ready);

	if (events_window == NULL)
		return 1;

	for (;;) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT)
				goto done;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (events_stop)
			break;
		/* Sleep until a message arrives; never hot-spin. */
		if (!WaitMessage())
			Sleep(10);
	}

done:
	EventLog("EventsThread: exit stop=%d wnd=0x%X", (int)events_stop, events_window);
	/* Tear down our own window; class/mutex are handled by CleanupEvents. */
	DestroyWindow(events_window);
	events_window = NULL;
	return 0;
}

int StartupEvents() {
	HANDLE thread;
	DWORD tid;

	EventLog("StartupEvents: begin");
	/* Do not create owned mutex: first waiter must not inherit ownership. */
	events_mutex = CreateMutex(NULL, FALSE, NULL);
	if (events_mutex == NULL)
		return GetLastError();
	EventLog("StartupEvents: after CreateMutex");

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
	InterlockedExchange((LPLONG)&events_stop, 0);
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

	if (events_startup_err != 0 || events_window == NULL) {
		/* Never report success with no window: the app would proceed
		 * straight into WSANOTINITIALISED failures later. */
		if (events_startup_err == 0)
			events_startup_err = ERROR_GEN_FAILURE;
		EventLog("StartupEvents: FAILED err=%lu wnd=0x%X", events_startup_err, events_window);
		/* Helper is pumping an empty loop (or already gone); stop it. */
		PostThreadMessage(tid, WM_QUIT, 0, 0);
		WaitForSingleObject(thread, 5000);
		CloseHandle(thread);
		CloseHandle(events_mutex);
		events_mutex = NULL;
		{
			DWORD err = events_startup_err;
			events_startup_err = 0;
			return err;
		}
	}
	EventLog("StartupEvents: ok wnd=0x%X helper=%lu", events_window, tid);

	/* Hand over ownership; CleanupEvents() will stop the thread. */
	events_thread = thread;
	events_thread_id = tid;

	return 0;
}

int CleanupEvents() {
	EventLog("CleanupEvents: enter wnd=0x%X thr=0x%X", events_window, events_thread);
	/* Stop the helper thread first so no more messages arrive. */
	if (events_thread != NULL) {
		/* Signal stop first (owns the loop condition), then wake the
		 * thread in case it is parked inside WaitMessage. */
		InterlockedExchange((LPLONG)&events_stop, 1);
		PostThreadMessage(events_thread_id, WM_NULL, 0, 0);
		PostThreadMessage(events_thread_id, WM_QUIT, 0, 0);
		if (WaitForSingleObject(events_thread, 5000) != WAIT_OBJECT_0)
			EventLog("CleanupEvents: helper join TIMEOUT");
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
