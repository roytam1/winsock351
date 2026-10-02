/* test_events.cpp: tests for WSAEventSelect / WSAEnumNetworkEvents
 *
 * Builds with Visual C++ 4.0 (and 6.0):
 *   Dynamic (uses local ws2_32.dll, for NT 3.51):
 *     cl /O2 test_events.cpp user32.lib kernel32.lib
 *   Static (links our objs directly, runs on modern Windows):
 *     cl /O2 /DTEST_STATIC_LINK test_events.cpp events.obj winsock2.obj tree234.obj wsock32.lib user32.lib kernel32.lib
 *
 * Dynamic run must start in the directory containing our ws2_32.dll. The test
 * loads .\ws2_32.dll explicitly (full path from exe dir) so the system
 * ws2_32.dll is never picked up by accident.
 *
 * Returns 0 if all tests pass, 1 otherwise.
 */
#include <windows.h>
#include <winsock.h>
#include <stdio.h>
#include <string.h>

#ifdef TEST_STATIC_LINK
#include "winsock2.h"
#endif

/* --- local compat (do NOT include winsock2.h/events.h: VC4 win1.1 lacks them) --- */
typedef HANDLE WSAEVENT_T;
#define WSA_INVALID_EVENT_T ((WSAEVENT_T)NULL)

typedef struct _TEST_WSANETWORKEVENTS {
    long lNetworkEvents;
    int  iErrorCode[10];
} TEST_WSANETWORKEVENTS;

#ifndef FD_READ
#define FD_READ    0x01
#define FD_WRITE   0x02
#define FD_OOB     0x04
#define FD_ACCEPT  0x08
#define FD_CONNECT 0x10
#define FD_CLOSE   0x20
#endif

/* FD_* bit -> iErrorCode[] index (must match events.cpp EventBitToIndex) */
static int TestEventBitToIndex(long bit)
{
    switch (bit) {
    case FD_READ: return 0;
    case FD_WRITE: return 1;
    case FD_OOB: return 2;
    case FD_ACCEPT: return 3;
    case FD_CONNECT: return 4;
    case FD_CLOSE: return 5;
    default: return -1;
    }
}

/* --- function pointers loaded from local ws2_32.dll --- */
typedef int  (__stdcall *PFN_WSAStartup)(WORD, LPWSADATA);
typedef int  (__stdcall *PFN_WSACleanup)(void);
typedef int  (__stdcall *PFN_WSAGetLastError)(void);
typedef void (__stdcall *PFN_WSASetLastError)(int);
typedef WSAEVENT_T (__stdcall *PFN_WSACreateEvent)(void);
typedef BOOL (__stdcall *PFN_WSACloseEvent)(WSAEVENT_T);
typedef BOOL (__stdcall *PFN_WSAResetEvent)(WSAEVENT_T);
typedef int  (__stdcall *PFN_WSAEventSelect)(SOCKET, WSAEVENT_T, long);
typedef int  (__stdcall *PFN_WSAEnumNetworkEvents)(SOCKET, WSAEVENT_T, TEST_WSANETWORKEVENTS*);
typedef SOCKET (__stdcall *PFN_socket)(int, int, int);
typedef int  (__stdcall *PFN_bind)(SOCKET, struct sockaddr*, int);
typedef int  (__stdcall *PFN_listen)(SOCKET, int);
typedef SOCKET (__stdcall *PFN_accept)(SOCKET, struct sockaddr*, int*);
typedef int  (__stdcall *PFN_connect)(SOCKET, struct sockaddr*, int);
typedef int  (__stdcall *PFN_closesocket)(SOCKET);
typedef int  (__stdcall *PFN_send)(SOCKET, const char*, int, int);
typedef int  (__stdcall *PFN_recv)(SOCKET, char*, int, int);
typedef int  (__stdcall *PFN_getsockname)(SOCKET, struct sockaddr*, int*);
typedef int  (__stdcall *PFN_gethostname)(char*, int);

static HMODULE g_dll = NULL;
static PFN_WSAStartup pWSAStartup = NULL;
static PFN_WSACleanup pWSACleanup = NULL;
static PFN_WSAGetLastError pWSAGetLastError = NULL;
static PFN_WSASetLastError pWSASetLastError = NULL;
static PFN_WSACreateEvent pWSACreateEvent = NULL;
static PFN_WSACloseEvent pWSACloseEvent = NULL;
static PFN_WSAResetEvent pWSAResetEvent = NULL;
static PFN_WSAEventSelect pWSAEventSelect = NULL;
static PFN_WSAEnumNetworkEvents pWSAEnumNetworkEvents = NULL;
static PFN_socket p_socket = NULL;
static PFN_bind p_bind = NULL;
static PFN_listen p_listen = NULL;
static PFN_accept p_accept = NULL;
static PFN_connect p_connect = NULL;
static PFN_closesocket p_closesocket = NULL;
static PFN_send p_send = NULL;
static PFN_recv p_recv = NULL;
static PFN_getsockname p_getsockname = NULL;
static PFN_gethostname p_gethostname = NULL;

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, fmt) do { \
    if (cond) { g_pass++; printf("PASS: " fmt "\n"); } \
    else { g_fail++; printf("FAIL: " fmt " (line %d, lastErr=%d)\n", __LINE__, pWSAGetLastError ? pWSAGetLastError() : -1); } \
} while (0)

/* portable htons/htonl without linking wsock32 */
static unsigned short my_htons(unsigned short x)
{
    return (unsigned short)(((x >> 8) & 0xFF) | ((x & 0xFF) << 8));
}
static unsigned long my_htonl(unsigned long x)
{
    return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
           ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000UL);
}

/* Wait for event HANDLE while pumping window messages so our hidden
 * WSAAsyncSelect window (owned by this thread) can dispatch. */
static int PumpWaitForEvent(WSAEVENT_T hEvent, DWORD timeoutMs)
{
    DWORD start = GetTickCount();
    DWORD remaining = timeoutMs;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, (HANDLE*)&hEvent, FALSE,
                                            remaining, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0)
            return 1; /* signaled */
        if (r == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            /* recompute remaining */
            if (timeoutMs != INFINITE) {
                DWORD now = GetTickCount();
                DWORD elapsed = now - start;
                if (elapsed >= timeoutMs)
                    return 0;
                remaining = timeoutMs - elapsed;
            }
            continue;
        }
        if (r == WAIT_TIMEOUT)
            return 0;
        /* WAIT_FAILED or abandoned: treat as not signaled */
        return 0;
    }
}

static void PumpMessagesBriefly(DWORD ms)
{
    DWORD start = GetTickCount();
    MSG msg;
    while (GetTickCount() - start < ms) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }
}

/* create loopback listener, port 0 -> ephemeral; returns SOCKET or INVALID_SOCKET,
 * fills outAddr with bound address (with real port). */
static SOCKET CreateListener(struct sockaddr_in* outAddr)
{
    SOCKET ls;
    struct sockaddr_in sa;
    int len;
    int r;

    ls = p_socket(AF_INET, SOCK_STREAM, 0);
    if (ls == INVALID_SOCKET)
        return INVALID_SOCKET;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = my_htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    r = p_bind(ls, (struct sockaddr*)&sa, sizeof(sa));
    if (r == SOCKET_ERROR) {
        p_closesocket(ls);
        return INVALID_SOCKET;
    }
    r = p_listen(ls, 5);
    if (r == SOCKET_ERROR) {
        p_closesocket(ls);
        return INVALID_SOCKET;
    }
    len = sizeof(sa);
    if (p_getsockname(ls, (struct sockaddr*)&sa, &len) == SOCKET_ERROR) {
        p_closesocket(ls);
        return INVALID_SOCKET;
    }
    if (outAddr)
        *outAddr = sa;
    return ls;
}

static int LoadLocalDll(void)
{
#ifdef TEST_STATIC_LINK
    /* Static build: bind directly. WSA* from our objs, BSD from system wsock32.
     * No DllMain runs, so set the events instance to our module. */
    SetEventsInstance(GetModuleHandle(NULL));
    pWSAStartup = (PFN_WSAStartup)WINSOCK351_WSAStartup;
    pWSACleanup = (PFN_WSACleanup)WINSOCK351_WSACleanup;
    pWSAGetLastError = (PFN_WSAGetLastError)WSAGetLastError;
    pWSASetLastError = (PFN_WSASetLastError)WSASetLastError;
    pWSACreateEvent = (PFN_WSACreateEvent)WINSOCK351_WSACreateEvent;
    pWSACloseEvent = (PFN_WSACloseEvent)WINSOCK351_WSACloseEvent;
    pWSAResetEvent = (PFN_WSAResetEvent)WINSOCK351_WSAResetEvent;
    pWSAEventSelect = (PFN_WSAEventSelect)WINSOCK351_WSAEventSelect;
    pWSAEnumNetworkEvents = (PFN_WSAEnumNetworkEvents)WINSOCK351_WSAEnumNetworkEvents;
    p_socket = (PFN_socket)socket;
    p_bind = (PFN_bind)bind;
    p_listen = (PFN_listen)listen;
    p_accept = (PFN_accept)accept;
    p_connect = (PFN_connect)connect;
    p_closesocket = (PFN_closesocket)WINSOCK351_closesocket;
    p_send = (PFN_send)send;
    p_recv = (PFN_recv)recv;
    p_getsockname = (PFN_getsockname)getsockname;
    p_gethostname = (PFN_gethostname)gethostname;
    printf("Bound static (objs + system wsock32).\n");
    return 1;
#else
    char exePath[MAX_PATH];
    char dllPath[MAX_PATH];
    char* slash;
    DWORD n;

    n = GetModuleFileName(NULL, exePath, sizeof(exePath));
    if (n == 0 || n >= sizeof(exePath))
        return 0;
    strcpy(dllPath, exePath);
    slash = strrchr(dllPath, '\\');
    if (slash)
        strcpy(slash + 1, "ws2_32.dll");
    else
        strcpy(dllPath, "ws2_32.dll");

    g_dll = LoadLibrary(dllPath);
    if (!g_dll) {
        printf("FAIL: LoadLibrary(%s) failed, err=%lu\n", dllPath, GetLastError());
        return 0;
    }
    printf("Loaded: %s\n", dllPath);

#define LOAD(fn) do { \
    p##fn = (PFN_##fn)GetProcAddress(g_dll, #fn); \
    if (!p##fn) { printf("FAIL: GetProcAddress(%s) failed\n", #fn); return 0; } \
} while (0)
#define LOADB(fn) do { \
    p_##fn = (PFN_##fn)GetProcAddress(g_dll, #fn); \
    if (!p_##fn) { printf("FAIL: GetProcAddress(%s) failed\n", #fn); return 0; } \
} while (0)

    LOAD(WSAStartup);
    LOAD(WSACleanup);
    LOAD(WSAGetLastError);
    LOAD(WSASetLastError);
    LOAD(WSACreateEvent);
    LOAD(WSACloseEvent);
    LOAD(WSAResetEvent);
    LOAD(WSAEventSelect);
    LOAD(WSAEnumNetworkEvents);
    LOADB(socket);
    LOADB(bind);
    LOADB(listen);
    LOADB(accept);
    LOADB(connect);
    LOADB(closesocket);
    LOADB(send);
    LOADB(recv);
    LOADB(getsockname);
    LOADB(gethostname);
#undef LOAD
#undef LOADB
    return 1;
#endif
}

/* ---------------- tests ---------------- */

static void T01_StartupSpoof(void)
{
    WSADATA wd;
    int r;
    memset(&wd, 0, sizeof(wd));
    r = pWSAStartup(MAKEWORD(2, 2), &wd);
    CHECK(r == 0, "T01 WSAStartup(2.2) returns 0");
    CHECK(wd.wVersion == MAKEWORD(2, 2), "T01 wVersion spoofed to 2.2");
}

static void T02_CreateCloseEvent(void)
{
    WSAEVENT_T e1, e2;
    BOOL ok;
    e1 = pWSACreateEvent();
    CHECK(e1 != NULL && e1 != WSA_INVALID_EVENT_T, "T02 WSACreateEvent returns handle");
    e2 = pWSACreateEvent();
    CHECK(e2 != NULL && e2 != WSA_INVALID_EVENT_T, "T02 second WSACreateEvent returns handle");
    CHECK(e1 != e2, "T02 two events are distinct");
    ok = pWSACloseEvent(e1);
    CHECK(ok == TRUE, "T02 WSACloseEvent(e1) TRUE");
    ok = pWSACloseEvent(e2);
    CHECK(ok == TRUE, "T02 WSACloseEvent(e2) TRUE");
    /* double close must fail */
    ok = pWSACloseEvent(e2);
    CHECK(ok == FALSE, "T02 double WSACloseEvent fails");
    /* NULL must fail */
    ok = pWSACloseEvent(NULL);
    CHECK(ok == FALSE, "T02 WSACloseEvent(NULL) fails");
    CHECK(pWSAGetLastError() == WSAEINVAL, "T02 WSACloseEvent(NULL) sets WSAEINVAL");
}

static void T03_EnumNullArgs(void)
{
    SOCKET s;
    TEST_WSANETWORKEVENTS nev;
    int r;
    s = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(s != INVALID_SOCKET, "T03 socket() for null-arg test");
    if (s == INVALID_SOCKET) return;
    r = pWSAEnumNetworkEvents(s, NULL, NULL);
    CHECK(r == SOCKET_ERROR, "T03 Enum(NULL out) returns SOCKET_ERROR");
    CHECK(pWSAGetLastError() == WSAEFAULT, "T03 Enum(NULL out) sets WSAEFAULT");
    memset(&nev, 0xAA, sizeof(nev));
    r = pWSAEnumNetworkEvents(s, NULL, &nev);
    /* no selection yet -> WSAENOTSOCK (no entry) */
    CHECK(r == SOCKET_ERROR, "T03 Enum without WSAEventSelect fails");
    CHECK(pWSAGetLastError() == WSAENOTSOCK, "T03 Enum without select sets WSAENOTSOCK");
    p_closesocket(s);
}

static void T04_EnumInvalidSocket(void)
{
    TEST_WSANETWORKEVENTS nev;
    int r;
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents((SOCKET)0x12345678, NULL, &nev);
    CHECK(r == SOCKET_ERROR, "T04 Enum(bad socket) returns SOCKET_ERROR");
    CHECK(pWSAGetLastError() == WSAENOTSOCK, "T04 Enum(bad socket) sets WSAENOTSOCK");
}

static void T05_SelectInvalidSocket(void)
{
    WSAEVENT_T ev;
    int r;
    ev = pWSACreateEvent();
    CHECK(ev != NULL, "T05 create event");
    if (!ev) return;
    r = pWSAEventSelect((SOCKET)INVALID_SOCKET, ev, FD_READ);
    CHECK(r == SOCKET_ERROR, "T05 EventSelect(INVALID_SOCKET) fails");
    pWSACloseEvent(ev);
}

static void T06_SelectCancel(void)
{
    SOCKET s;
    WSAEVENT_T ev;
    TEST_WSANETWORKEVENTS nev;
    int r;
    s = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(s != INVALID_SOCKET, "T06 socket()");
    if (s == INVALID_SOCKET) return;
    ev = pWSACreateEvent();
    CHECK(ev != NULL, "T06 create event");
    if (!ev) { p_closesocket(s); return; }

    r = pWSAEventSelect(s, ev, FD_READ | FD_WRITE);
    CHECK(r == 0, "T06 EventSelect(FD_READ|FD_WRITE) returns 0");

    /* cancel via (s, NULL, 0) */
    r = pWSAEventSelect(s, NULL, 0);
    CHECK(r == 0, "T06 EventSelect(s,NULL,0) cancels");
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(s, NULL, &nev);
    CHECK(r == SOCKET_ERROR, "T06 Enum after cancel fails (entry deleted)");

    /* re-arm then cancel via (s, ev, 0) */
    r = pWSAEventSelect(s, ev, FD_READ);
    CHECK(r == 0, "T06 re-arm EventSelect(FD_READ)");
    r = pWSAEventSelect(s, ev, 0);
    CHECK(r == 0, "T06 EventSelect(s,ev,0) cancels");
    r = pWSAEnumNetworkEvents(s, NULL, &nev);
    CHECK(r == SOCKET_ERROR, "T06 Enum after (ev,0) cancel fails");

    /* NULL event with nonzero mask is invalid */
    r = pWSAEventSelect(s, NULL, FD_READ);
    CHECK(r == SOCKET_ERROR, "T06 EventSelect(NULL,FD_READ) fails");
    CHECK(pWSAGetLastError() == WSAEINVAL, "T06 EventSelect(NULL,FD_READ) sets WSAEINVAL");

    pWSACloseEvent(ev);
    p_closesocket(s);
}

/* Full FD_ACCEPT + auto-reset + event-reset test. Leaves listener+client
 * connected for later tests via out params (caller closes). */
static SOCKET g_listen = INVALID_SOCKET;
static SOCKET g_serverAccepted = INVALID_SOCKET;
static SOCKET g_client = INVALID_SOCKET;
static struct sockaddr_in g_listenAddr;

static void T07_AcceptFlow(void)
{
    WSAEVENT_T ev;
    TEST_WSANETWORKEVENTS nev;
    int r, idx;
    int addrlen;
    struct sockaddr_in peer;

    g_listen = CreateListener(&g_listenAddr);
    CHECK(g_listen != INVALID_SOCKET, "T07 listener created");
    if (g_listen == INVALID_SOCKET) return;

    ev = pWSACreateEvent();
    CHECK(ev != NULL, "T07 create accept event");
    if (!ev) return;

    r = pWSAEventSelect(g_listen, ev, FD_ACCEPT | FD_CLOSE);
    CHECK(r == 0, "T07 EventSelect(listen, FD_ACCEPT|FD_CLOSE)");

    /* connect a client (blocking connect is fine; listener side is async) */
    g_client = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(g_client != INVALID_SOCKET, "T07 client socket()");
    if (g_client == INVALID_SOCKET) { pWSACloseEvent(ev); return; }
    r = p_connect(g_client, (struct sockaddr*)&g_listenAddr, sizeof(g_listenAddr));
    CHECK(r == 0, "T07 client connect() to listener");

    CHECK(PumpWaitForEvent(ev, 5000), "T07 FD_ACCEPT event signaled within 5s");

    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(g_listen, ev, &nev);
    CHECK(r == 0, "T07 Enum(listen) returns 0");
    CHECK((nev.lNetworkEvents & FD_ACCEPT) != 0, "T07 Enum shows FD_ACCEPT");
    idx = TestEventBitToIndex(FD_ACCEPT);
    CHECK(idx >= 0 && nev.iErrorCode[idx] == 0, "T07 FD_ACCEPT iErrorCode==0");

    /* auto-reset: second Enum must show no events */
    memset(&nev, 0xAA, sizeof(nev));
    /* re-arm first so entry exists: Enum above reset it to zero but kept entry.
     * Our ResetEventData keeps entry with zeros, so second Enum succeeds w/ 0. */
    r = pWSAEnumNetworkEvents(g_listen, NULL, &nev);
    CHECK(r == 0, "T07 second Enum succeeds (reset, not deleted)");
    CHECK(nev.lNetworkEvents == 0, "T07 second Enum shows 0 events (auto-reset)");

    /* event object itself must have been reset by first Enum(ev) */
    CHECK(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT, "T07 event object auto-reset to nonsignaled");

    /* accept the connection for later READ/CLOSE tests */
    addrlen = sizeof(peer);
    g_serverAccepted = p_accept(g_listen, (struct sockaddr*)&peer, &addrlen);
    CHECK(g_serverAccepted != INVALID_SOCKET, "T07 accept() succeeds");

    /* re-arm listener for CLOSE later; keep ev open for T11 isolation? close it */
    pWSACloseEvent(ev);
}

static void T08_ConnectFlow(void)
{
    SOCKET cs;
    WSAEVENT_T ev;
    TEST_WSANETWORKEVENTS nev;
    int r, idx;

    cs = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(cs != INVALID_SOCKET, "T08 socket()");
    if (cs == INVALID_SOCKET) return;
    ev = pWSACreateEvent();
    CHECK(ev != NULL, "T08 create event");
    if (!ev) { p_closesocket(cs); return; }

    r = pWSAEventSelect(cs, ev, FD_CONNECT | FD_CLOSE);
    CHECK(r == 0, "T08 EventSelect(FD_CONNECT|FD_CLOSE)");

    r = p_connect(cs, (struct sockaddr*)&g_listenAddr, sizeof(g_listenAddr));
    /* async socket: expect 0 (instant loopback) or SOCKET_ERROR/WSAEWOULDBLOCK */
    CHECK(r == 0 || pWSAGetLastError() == WSAEWOULDBLOCK,
          "T08 async connect returns 0 or WSAEWOULDBLOCK");

    CHECK(PumpWaitForEvent(ev, 5000), "T08 FD_CONNECT signaled within 5s");
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(cs, ev, &nev);
    CHECK(r == 0, "T08 Enum returns 0");
    CHECK((nev.lNetworkEvents & FD_CONNECT) != 0, "T08 Enum shows FD_CONNECT");
    idx = TestEventBitToIndex(FD_CONNECT);
    CHECK(idx >= 0 && nev.iErrorCode[idx] == 0, "T08 FD_CONNECT iErrorCode==0");

    /* cleanup: this extra client is not needed further; accept+close server side */
    {
        struct sockaddr_in peer;
        int len = sizeof(peer);
        SOCKET tmp;
        /* listener still has FD_ACCEPT pending from this connect; accept it */
        PumpMessagesBriefly(200);
        tmp = p_accept(g_listen, (struct sockaddr*)&peer, &len);
        if (tmp != INVALID_SOCKET)
            p_closesocket(tmp);
    }
    pWSACloseEvent(ev);
    p_closesocket(cs);
}

static void T09_ReadFlow(void)
{
    WSAEVENT_T evS;
    TEST_WSANETWORKEVENTS nev;
    int r, idx;
    const char* msg = "hello-winsock351";
    char buf[64];

    CHECK(g_serverAccepted != INVALID_SOCKET && g_client != INVALID_SOCKET,
          "T09 fixtures from T07 alive");
    if (g_serverAccepted == INVALID_SOCKET || g_client == INVALID_SOCKET) return;

    evS = pWSACreateEvent();
    CHECK(evS != NULL, "T09 create server READ event");
    if (!evS) return;

    r = pWSAEventSelect(g_serverAccepted, evS, FD_READ | FD_CLOSE);
    CHECK(r == 0, "T09 EventSelect(accepted, FD_READ|FD_CLOSE)");

    r = p_send(g_client, msg, (int)strlen(msg), 0);
    CHECK(r == (int)strlen(msg), "T09 client send()");

    CHECK(PumpWaitForEvent(evS, 5000), "T09 FD_READ signaled within 5s");
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(g_serverAccepted, evS, &nev);
    CHECK(r == 0, "T09 Enum returns 0");
    CHECK((nev.lNetworkEvents & FD_READ) != 0, "T09 Enum shows FD_READ");
    idx = TestEventBitToIndex(FD_READ);
    CHECK(idx >= 0 && nev.iErrorCode[idx] == 0, "T09 FD_READ iErrorCode==0");

    memset(buf, 0, sizeof(buf));
    r = p_recv(g_serverAccepted, buf, (int)sizeof(buf) - 1, 0);
    CHECK(r == (int)strlen(msg), "T09 recv() got full message");
    CHECK(strcmp(buf, msg) == 0, "T09 recv() content matches");

    pWSACloseEvent(evS);
}

static void T10_WriteFlow(void)
{
    /* FD_WRITE is level-triggered: after connect with empty send buffer,
     * Enum should report it. Tolerant: pass if signaled, skip-fail otherwise. */
    SOCKET cs;
    WSAEVENT_T ev;
    TEST_WSANETWORKEVENTS nev;
    int r;

    cs = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(cs != INVALID_SOCKET, "T10 socket()");
    if (cs == INVALID_SOCKET) return;
    ev = pWSACreateEvent();
    if (!ev) { p_closesocket(cs); return; }
    r = pWSAEventSelect(cs, ev, FD_CONNECT | FD_WRITE | FD_CLOSE);
    CHECK(r == 0, "T10 EventSelect(FD_CONNECT|FD_WRITE|FD_CLOSE)");
    r = p_connect(cs, (struct sockaddr*)&g_listenAddr, sizeof(g_listenAddr));
    CHECK(r == 0 || pWSAGetLastError() == WSAEWOULDBLOCK, "T10 connect");
    if (PumpWaitForEvent(ev, 5000)) {
        memset(&nev, 0, sizeof(nev));
        r = pWSAEnumNetworkEvents(cs, NULL, &nev);
        CHECK(r == 0, "T10 Enum returns 0");
        CHECK((nev.lNetworkEvents & (FD_CONNECT | FD_WRITE)) != 0,
              "T10 Enum shows CONNECT and/or WRITE");
    } else {
        CHECK(0, "T10 event signaled within 5s");
    }
    {
        struct sockaddr_in peer;
        int len = sizeof(peer);
        SOCKET tmp;
        PumpMessagesBriefly(200);
        tmp = p_accept(g_listen, (struct sockaddr*)&peer, &len);
        if (tmp != INVALID_SOCKET)
            p_closesocket(tmp);
    }
    pWSACloseEvent(ev);
    p_closesocket(cs);
}

static void T11_CloseFlow(void)
{
    WSAEVENT_T evS;
    TEST_WSANETWORKEVENTS nev;
    int r, idx;

    CHECK(g_serverAccepted != INVALID_SOCKET && g_client != INVALID_SOCKET,
          "T11 fixtures alive");
    if (g_serverAccepted == INVALID_SOCKET || g_client == INVALID_SOCKET) return;

    evS = pWSACreateEvent();
    CHECK(evS != NULL, "T11 create server CLOSE event");
    if (!evS) return;
    r = pWSAEventSelect(g_serverAccepted, evS, FD_READ | FD_CLOSE);
    CHECK(r == 0, "T11 EventSelect(accepted, FD_READ|FD_CLOSE)");

    /* graceful close from client */
    p_closesocket(g_client);
    g_client = INVALID_SOCKET;

    CHECK(PumpWaitForEvent(evS, 5000), "T11 FD_CLOSE signaled within 5s");
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(g_serverAccepted, evS, &nev);
    CHECK(r == 0, "T11 Enum returns 0");
    CHECK((nev.lNetworkEvents & FD_CLOSE) != 0, "T11 Enum shows FD_CLOSE");
    idx = TestEventBitToIndex(FD_CLOSE);
    CHECK(idx >= 0 && nev.iErrorCode[idx] == 0, "T11 FD_CLOSE iErrorCode==0");

    pWSACloseEvent(evS);
    p_closesocket(g_serverAccepted);
    g_serverAccepted = INVALID_SOCKET;
}

static void T12_ClosesocketCleanup(void)
{
    SOCKET s;
    WSAEVENT_T ev;
    TEST_WSANETWORKEVENTS nev;
    int r;
    s = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(s != INVALID_SOCKET, "T12 socket()");
    if (s == INVALID_SOCKET) return;
    ev = pWSACreateEvent();
    if (!ev) { p_closesocket(s); return; }
    r = pWSAEventSelect(s, ev, FD_READ | FD_CLOSE);
    CHECK(r == 0, "T12 EventSelect");
    /* our closesocket wrapper must DeleteEventData */
    p_closesocket(s);
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(s, NULL, &nev);
    CHECK(r == SOCKET_ERROR, "T12 Enum after closesocket fails");
    pWSACloseEvent(ev);
}

static void T13_Isolation(void)
{
    struct sockaddr_in a1, a2;
    SOCKET l1, l2;
    WSAEVENT_T e1, e2;
    SOCKET c1;
    int r;
    TEST_WSANETWORKEVENTS nev;

    l1 = CreateListener(&a1);
    l2 = CreateListener(&a2);
    CHECK(l1 != INVALID_SOCKET && l2 != INVALID_SOCKET, "T13 two listeners");
    if (l1 == INVALID_SOCKET || l2 == INVALID_SOCKET) {
        if (l1 != INVALID_SOCKET) p_closesocket(l1);
        if (l2 != INVALID_SOCKET) p_closesocket(l2);
        return;
    }
    e1 = pWSACreateEvent();
    e2 = pWSACreateEvent();
    CHECK(e1 != NULL && e2 != NULL, "T13 two events");
    if (!e1 || !e2) goto done;
    CHECK(pWSAEventSelect(l1, e1, FD_ACCEPT) == 0, "T13 select l1 ACCEPT");
    CHECK(pWSAEventSelect(l2, e2, FD_ACCEPT) == 0, "T13 select l2 ACCEPT");

    c1 = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(c1 != INVALID_SOCKET, "T13 client");
    if (c1 == INVALID_SOCKET) goto done;
    r = p_connect(c1, (struct sockaddr*)&a1, sizeof(a1));
    CHECK(r == 0, "T13 connect to l1 only");
    CHECK(PumpWaitForEvent(e1, 5000), "T13 l1 ACCEPT signaled");
    CHECK(!PumpWaitForEvent(e2, 500), "T13 l2 NOT signaled (isolation)");
    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(l1, e1, &nev);
    CHECK(r == 0 && (nev.lNetworkEvents & FD_ACCEPT), "T13 l1 Enum shows ACCEPT");
    p_closesocket(c1);
    {
        struct sockaddr_in peer;
        int len = sizeof(peer);
        SOCKET tmp = p_accept(l1, (struct sockaddr*)&peer, &len);
        if (tmp != INVALID_SOCKET) p_closesocket(tmp);
    }
done:
    if (e1) pWSACloseEvent(e1);
    if (e2) pWSACloseEvent(e2);
    p_closesocket(l1);
    p_closesocket(l2);
    /* drain listener backlog on g_listen from stray connects (none expected) */
    PumpMessagesBriefly(100);
}

/* T14: plink regression - event must signal with NO message pumping at all.
 * Old design created the WSAAsyncSelect window on the app thread, so a
 * console app doing plain WaitForSingleObject would stall forever. The
 * helper thread owns the window and pumps it, so this must succeed. */
static void T14_NoPumpWait(void)
{
    struct sockaddr_in a;
    SOCKET l, c;
    WSAEVENT_T e;
    TEST_WSANETWORKEVENTS nev;
    DWORD w;
    int r;

    l = CreateListener(&a);
    CHECK(l != INVALID_SOCKET, "T14 listener");
    if (l == INVALID_SOCKET) return;
    e = pWSACreateEvent();
    CHECK(e != NULL, "T14 create event");
    if (!e) { p_closesocket(l); return; }
    CHECK(pWSAEventSelect(l, e, FD_ACCEPT) == 0, "T14 EventSelect ACCEPT");

    c = p_socket(AF_INET, SOCK_STREAM, 0);
    CHECK(c != INVALID_SOCKET, "T14 client");
    if (c == INVALID_SOCKET) { pWSACloseEvent(e); p_closesocket(l); return; }
    r = p_connect(c, (struct sockaddr*)&a, sizeof(a));
    CHECK(r == 0, "T14 connect");

    /* Deliberately NO PeekMessage/DispatchMessage here. */
    w = WaitForSingleObject(e, 5000);
    CHECK(w == WAIT_OBJECT_0, "T14 FD_ACCEPT signaled without pumping");

    memset(&nev, 0, sizeof(nev));
    r = pWSAEnumNetworkEvents(l, e, &nev);
    CHECK(r == 0 && (nev.lNetworkEvents & FD_ACCEPT), "T14 Enum shows ACCEPT");

    p_closesocket(c);
    {
        struct sockaddr_in peer;
        int len = sizeof(peer);
        SOCKET tmp = p_accept(l, (struct sockaddr*)&peer, &len);
        if (tmp != INVALID_SOCKET) p_closesocket(tmp);
    }
    pWSACloseEvent(e);
    p_closesocket(l);
}

int main(void)
{
    if (!LoadLocalDll())
        return 1;

    T01_StartupSpoof();
    T02_CreateCloseEvent();
    T03_EnumNullArgs();
    T04_EnumInvalidSocket();
    T05_SelectInvalidSocket();
    T06_SelectCancel();
    T07_AcceptFlow();
    T08_ConnectFlow();
    T09_ReadFlow();
    T10_WriteFlow();
    T11_CloseFlow();
    T12_ClosesocketCleanup();
    T13_Isolation();
    T14_NoPumpWait();

    /* final listener cleanup from T07 */
    if (g_listen != INVALID_SOCKET) {
        p_closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }

    pWSACleanup();
    if (g_dll)
        FreeLibrary(g_dll);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
