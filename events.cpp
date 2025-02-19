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
    int *a = (int *)av;
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
	const DWORD wait_result = WaitForSingleObject(events_mutex, INFINITE);
    if (!sockdatatree)
        sockdatatree = newtree234(sockdata_compare);

	switch (wait_result) {
	case WAIT_OBJECT_0:
		sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
	    if (!sd) {
            sd = snew(struct sockdata);
            sd->sock = socket;
        }
        sd->data = data;
        add234(sockdatatree, sd);
		ReleaseMutex(events_mutex);
	case WAIT_ABANDONED:
		return;
	}
}

int GetEventData(SOCKET socket, WSAEventData* data) {
    struct sockdata *sd;
	const DWORD wait_result = WaitForSingleObject(events_mutex, INFINITE);
    if (!sockdatatree)
        sockdatatree = newtree234(sockdata_compare);

	switch (wait_result) {
	case WAIT_OBJECT_0:
		sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
	    if (!sd)
			return -1;

		*data = sd->data;
		ReleaseMutex(events_mutex);
	case WAIT_ABANDONED:
		return -1;
	}

	return 0;
}

void DeleteEventData(SOCKET socket) {
    struct sockdata *sd;
	const DWORD wait_result = WaitForSingleObject(events_mutex, INFINITE);
    if (!sockdatatree)
        sockdatatree = newtree234(sockdata_compare);
	switch (wait_result) {
	case WAIT_OBJECT_0:
		sd = (struct sockdata *)find234(sockdatatree, &socket, sockdata_find);
	    if (sd) del234(sockdatatree, sd);
		ReleaseMutex(events_mutex);
	case WAIT_ABANDONED:
		return;
	}
}

void DeleteEvents() {
    struct sockdata *sd;
	const DWORD wait_result = WaitForSingleObject(events_mutex, INFINITE);
	switch (wait_result) {
	case WAIT_OBJECT_0:
		while ((sd = (struct sockdata *)delpos234(sockdatatree, 0)) != NULL)
		    sfree(sd); /* or some more complicated free function */
		freetree234(sockdatatree);
		sockdatatree = NULL;
		ReleaseMutex(events_mutex);
	case WAIT_ABANDONED:
		return;
	}
}

LRESULT CALLBACK EventsWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	if (uMsg > WM_USER) {
		const WSAEVENT event = (WSAEVENT)(uMsg - WM_USER);
		WSAEventData data;
		int err = GetEventData(wParam, &data);
		if (err != 0)
			return FALSE;

		data.lNetworkEvents = WSAGETSELECTEVENT(lParam);
		SetEventData(wParam, data);

		return SetEvent(event);
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
	events_mutex = CreateMutex(NULL, TRUE, NULL);
	if (events_mutex == NULL)
		return GetLastError();

	// In order to receive WSAAsync events locally we will create an
	// invisible window to receive those messages
	// HWND_MESSAGE didn't exist on NT 3.51 so we have to do it this way
	WNDCLASS wc; 

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
	RegisterClass(&wc);
	events_window = CreateWindow(events_window_name, NULL, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL);

	return 0;
}

int CleanupEvents() {
	DeleteEvents();
	const int err = CloseHandle(events_mutex);
	if (err != 0) {
		return err;
	}

	DestroyWindow(events_window);
	UnregisterClass(events_window_name, NULL);
	return 0;
}
