#include <windows.h>
#include <winsock.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "winsock2.h"
#include "events.h"

#ifdef _DEBUG
void DebugLog(const char* fmt, ...) {
	va_list args;
	char* buf;
	va_start(args, fmt);
	buf = new char[256];
	_vsnprintf(buf, 255, fmt, args);
	buf[255] = '\0';
	va_end(args);
	MessageBox(NULL, buf, "WINSOCK351", 0);
	delete[] buf;
}
#else
#define DebugLog (void)0
#endif

/* Refcount for Startup/Cleanup pairing (wsock32 already refcounts itself). */
static LONG g_startup_ref = 0;

/* ---------- small helpers ---------- */

static int OverlappedNotSupported(LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
	if (ov != NULL || cr != NULL) {
		WSASetLastError(WSAEOPNOTSUPP);
		return 1;
	}
	return 0;
}

static u_short ResolveServicePort(const char* service, const char* proto) {
	struct servent FAR *se;
	int port;
	char* endp;
	if (service == NULL || service[0] == '\0')
		return 0;
	port = atoi(service);
	if (port > 0 && port < 65536) {
		/* atoi("0") == 0; make sure numeric form really numeric */
		for (endp = (char*)service; *endp; endp++) {
			if (*endp < '0' || *endp > '9')
				break;
		}
		if (*endp == '\0')
			return htons((u_short)port);
	}
	se = getservbyname((char*)service, (char*)proto);
	if (se != NULL)
		return se->s_port; /* already network order */
	return 0xFFFF; /* invalid marker */
}

static char* StrDupN(const char* s) {
	char* d;
	size_t n;
	if (s == NULL)
		return NULL;
	n = strlen(s) + 1;
	d = (char*)malloc(n);
	if (d != NULL)
		memcpy(d, s, n);
	return d;
}

static int AnsiToWide(const char* a, LPWSTR w, int wlen) {
	int r;
	if (a == NULL || w == NULL || wlen <= 0)
		return 0;
	r = MultiByteToWideChar(CP_ACP, 0, a, -1, w, wlen);
	return r;
}

static int WideToAnsi(const WCHAR* w, char* a, int alen) {
	int r;
	if (w == NULL || a == NULL || alen <= 0)
		return 0;
	r = WideCharToMultiByte(CP_ACP, 0, w, -1, a, alen, NULL, NULL);
	return r;
}

/* ---------- BSD wrappers ---------- */

int WSAAPI WINSOCK351_closesocket(SOCKET s) {
	DebugLog("closesocket: socket: %d", s);
	DeleteEventData(s);
	return closesocket(s);
}

int WSAAPI WINSOCK351_getsockopt(SOCKET s, int level, int optname, char* optval, int* optlen) {
	DebugLog("getsockopt: socket: %d, level: 0x%X, optname: 0x%X", s, level, optname);
	/* Winsock2-only opts have no NT 3.51 equivalent; fail fast. */
	switch (optname) {
	case SO_GROUP_ID:
	case SO_GROUP_PRIORITY:
	case SO_PROTOCOL_INFOA:
#ifdef SO_PROTOCOL_INFOW
	case SO_PROTOCOL_INFOW:
#endif
		/* SO_PROTOCOL_INFOA == 0x2004 overlaps; avoid double case */
		break;
	default:
		break;
	}
	if ((optname == SO_GROUP_ID || optname == SO_GROUP_PRIORITY)
#ifdef SO_PROTOCOL_INFOW
		|| optname == SO_PROTOCOL_INFOW
#endif
		) {
		WSASetLastError(WSAENOPROTOOPT);
		return SOCKET_ERROR;
	}
	return getsockopt(s, level, optname, optval, optlen);
}

int WSAAPI WINSOCK351_ioctlsocket(SOCKET s, long cmd, u_long* argp) {
	DebugLog("ioctlsocket: socket: %d, cmd: 0x%X", s, cmd);
	return ioctlsocket(s, cmd, argp);
}

int WSAAPI WINSOCK351_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const struct timeval* timeout) {
	(void)nfds; /* ignored on Windows */
	DebugLog("select");
	return select(nfds, readfds, writefds, exceptfds, timeout);
}

int WSAAPI WINSOCK351_setsockopt(SOCKET s, int level, int optname, const char* optval, int optlen) {
	DebugLog("setsockopt: socket: %d, level: 0x%X, optname: 0x%X", s, level, optname);
	if (optname == SO_GROUP_ID || optname == SO_GROUP_PRIORITY) {
		WSASetLastError(WSAENOPROTOOPT);
		return SOCKET_ERROR;
	}
	return setsockopt(s, level, optname, (char*)optval, optlen);
}

/* ---------- events ---------- */

BOOL WSAAPI WINSOCK351_WSACloseEvent(WSAEVENT hEvent) {
	DebugLog("WSACloseEvent");
	if (hEvent == NULL || hEvent == WSA_INVALID_EVENT) {
		WSASetLastError(WSAEINVAL);
		return FALSE;
	}
	if (!CloseHandle(hEvent)) {
		WSASetLastError(GetLastError());
		return FALSE;
	}
	return TRUE;
}

WSAEVENT WSAAPI WINSOCK351_WSACreateEvent() {
	WSAEVENT event;
	DebugLog("WSACreateEvent");
	event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (event == NULL) {
		WSASetLastError(GetLastError());
		return WSA_INVALID_EVENT;
	}
	return event;
}

int WSAAPI WINSOCK351_WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents) {
	WSAEventData data;
	int i;
	DebugLog("WSAEnumNetworkEvents: socket: %d", s);
	if (lpNetworkEvents == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (GetEventData(s, &data) != 0) {
		WSASetLastError(WSAENOTSOCK);
		return SOCKET_ERROR;
	}
	lpNetworkEvents->lNetworkEvents = data.lNetworkEvents;
	for (i = 0; i < FD_MAX_EVENTS; i++)
		lpNetworkEvents->iErrorCode[i] = data.iErrorCode[i];
	/* Per spec: atomically reset internal record... */
	ResetEventData(s);
	/* ...and reset the event object if supplied. */
	if (hEventObject != NULL && hEventObject != WSA_INVALID_EVENT)
		ResetEvent(hEventObject);
	return 0;
}

int WSAAPI WINSOCK351_WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents) {
	UINT msg;
	WSAEventData zero;
	DebugLog("WSAEventSelect: socket: %d, lNetworkEvents: 0x%X", s, lNetworkEvents);
	if (GetEventsWindow() == NULL) {
		WSASetLastError(WSANOTINITIALISED);
		return SOCKET_ERROR;
	}
	/* Cancel selection. */
	if (hEventObject == NULL || hEventObject == WSA_INVALID_EVENT) {
		if (lNetworkEvents != 0) {
			WSASetLastError(WSAEINVAL);
			return SOCKET_ERROR;
		}
		DeleteEventData(s);
		return WSAAsyncSelect(s, GetEventsWindow(), 0, 0);
	}
	if (lNetworkEvents == 0) {
		/* Still need valid message for WSAAsyncSelect cancel path. */
		DeleteEventData(s);
		return WSAAsyncSelect(s, GetEventsWindow(), 0, 0);
	}
	/* HANDLE values must fit in a user message number. */
	msg = WM_USER + ((UINT)hEventObject);
	if (msg <= (UINT)WM_USER || msg >= 0x7FFF) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	memset(&zero, 0, sizeof(zero));
	SetEventData(s, zero);
	return WSAAsyncSelect(s, GetEventsWindow(), msg, lNetworkEvents);
}

int WSAAPI WINSOCK351_WSAIoctl(
	SOCKET s,
	DWORD dwIoControlCode,
	LPVOID lpvInBuffer,
	DWORD cbInBuffer,
	LPVOID lpvOutBuffer,
	DWORD cbOutBuffer,
	LPDWORD lpcbBytesReturned,
	LPWSAOVERLAPPED lpOverlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	(void)s; (void)dwIoControlCode; (void)lpvInBuffer; (void)cbInBuffer;
	(void)lpvOutBuffer; (void)cbOutBuffer; (void)lpcbBytesReturned;
	(void)lpOverlapped; (void)lpCompletionRoutine;
	DebugLog("WSAIoctl: NOTIMPL 0x%X", dwIoControlCode);
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData) {
	int err;
	LONG ref;
	DebugLog("WSAStartup: 0x%X", wVersionRequested);
	if (lpWSAData == NULL) {
		WSASetLastError(WSAEFAULT);
		return WSAEFAULT;
	}
	err = WSAStartup(wVersionRequested, lpWSAData);
	if (err != 0)
		return err;
	ref = InterlockedIncrement(&g_startup_ref);
	if (ref == 1) {
		err = StartupEvents();
		if (err != 0) {
			InterlockedDecrement(&g_startup_ref);
			WSACleanup();
			WSASetLastError(err);
			return err;
		}
	}
	/* Spoof the version returned so 2.x apps don't bail out. */
	lpWSAData->wVersion = wVersionRequested;
	return 0;
}

int WSAAPI WINSOCK351_WSACleanup() {
	int err;
	LONG ref;
	DebugLog("WSACleanup");
	err = WSACleanup();
	if (err != 0)
		return err;
	ref = InterlockedDecrement(&g_startup_ref);
	if (ref <= 0) {
		if (ref < 0)
			InterlockedIncrement(&g_startup_ref);
		else {
			CleanupEvents();
			g_startup_ref = 0;
		}
	}
	return 0;
}

BOOL WSAAPI WINSOCK351_WSAResetEvent(WSAEVENT hEvent) {
	DebugLog("WSAResetEvent");
	if (hEvent == NULL || hEvent == WSA_INVALID_EVENT) {
		WSASetLastError(WSAEINVAL);
		return FALSE;
	}
	if (!ResetEvent(hEvent)) {
		WSASetLastError(GetLastError());
		return FALSE;
	}
	return TRUE;
}

BOOL WSAAPI WINSOCK351_WSASetEvent(WSAEVENT hEvent) {
	DebugLog("WSASetEvent");
	if (hEvent == NULL || hEvent == WSA_INVALID_EVENT) {
		WSASetLastError(WSAEINVAL);
		return FALSE;
	}
	if (!SetEvent(hEvent)) {
		WSASetLastError(GetLastError());
		return FALSE;
	}
	return TRUE;
}

DWORD WSAAPI WINSOCK351_WSAWaitForMultipleEvents(
	DWORD cEvents,
	const WSAEVENT FAR * lphEvents,
	BOOL fWaitAll,
	DWORD dwTimeout,
	BOOL fAlertable) {
	DWORD r;
	DebugLog("WSAWaitForMultipleEvents: %d", cEvents);
	if (cEvents == 0 || lphEvents == NULL) {
		WSASetLastError(WSAEINVAL);
		return WSA_WAIT_FAILED;
	}
	r = WaitForMultipleObjectsEx(cEvents, (HANDLE*)lphEvents, fWaitAll, dwTimeout, fAlertable);
	if (r == 0xFFFFFFFF)
		WSASetLastError(GetLastError());
	return r;
}

/* ---------- WSASocket / connect / accept ---------- */

SOCKET WSAAPI WINSOCK351_WSASocketA(int af, int type, int protocol, LPWSAPROTOCOL_INFOA lpProtocolInfo, GROUP g, DWORD dwFlags) {
	int useAf = af, useType = type, useProto = protocol;
	SOCKET s;
	(void)g; (void)dwFlags;
	DebugLog("WSASocketA: %d %d %d", af, type, protocol);
	if (lpProtocolInfo != NULL) {
		if (af == FROM_PROTOCOL_INFO)
			useAf = lpProtocolInfo->iAddressFamily;
		if (type == FROM_PROTOCOL_INFO)
			useType = lpProtocolInfo->iSocketType;
		if (protocol == FROM_PROTOCOL_INFO)
			useProto = lpProtocolInfo->iProtocol;
	}
	if (useAf == AF_INET6) {
		/* No IPv6 stack on NT 3.51 / wsock32 1.1 */
		WSASetLastError(WSAEAFNOSUPPORT);
		return INVALID_SOCKET;
	}
	s = socket(useAf, useType, useProto);
	return s;
}

SOCKET WSAAPI WINSOCK351_WSASocketW(int af, int type, int protocol, LPWSAPROTOCOL_INFOW lpProtocolInfo, GROUP g, DWORD dwFlags) {
	int useAf = af, useType = type, useProto = protocol;
	SOCKET s;
	(void)g; (void)dwFlags;
	DebugLog("WSASocketW");
	if (lpProtocolInfo != NULL) {
		if (af == FROM_PROTOCOL_INFO)
			useAf = lpProtocolInfo->iAddressFamily;
		if (type == FROM_PROTOCOL_INFO)
			useType = lpProtocolInfo->iSocketType;
		if (protocol == FROM_PROTOCOL_INFO)
			useProto = lpProtocolInfo->iProtocol;
	}
	if (useAf == AF_INET6) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return INVALID_SOCKET;
	}
	s = socket(useAf, useType, useProto);
	return s;
}

int WSAAPI WINSOCK351_WSAConnect(SOCKET s, const struct sockaddr FAR * name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS) {
	(void)lpCallerData; (void)lpCalleeData; (void)lpSQOS; (void)lpGQOS;
	DebugLog("WSAConnect");
	if (name == NULL || namelen < (int)sizeof(struct sockaddr)) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	return connect(s, (struct sockaddr*)name, namelen);
}

int WSAAPI WINSOCK351_WSAConnectByList(SOCKET s, LPWSABUF Slice, LPDWORD SliceLength, LPWSABUF Local, LPDWORD LocalLength, LPWSABUF Remote, LPDWORD RemoteLength, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved) {
	(void)Local; (void)LocalLength; (void)Remote; (void)RemoteLength;
	(void)timeout; (void)Reserved;
	DebugLog("WSAConnectByList");
	if (Slice == NULL || SliceLength == NULL || Slice->buf == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	/* Best effort: Slice->buf holds first sockaddr; try it. */
	/* Caller packs SOCKET_ADDRESS_LIST; first entry offset is 4 bytes in? */
	/* Fall back to treating buf as sockaddr_in. */
	{
		struct sockaddr FAR * sa = (struct sockaddr FAR *)Slice->buf;
		int len = (int)Slice->len;
		if (len <= 0)
			len = sizeof(struct sockaddr_in);
		if (sa->sa_family == AF_INET6) {
			WSASetLastError(WSAEAFNOSUPPORT);
			return SOCKET_ERROR;
		}
		return connect(s, sa, len);
	}
}

int WSAAPI WINSOCK351_WSAConnectByNameA(SOCKET s, LPCSTR nodename, LPCSTR servicename, LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress, LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved) {
	LPADDRINFOA res = NULL, ai;
	int err;
	(void)LocalAddressLength; (void)LocalAddress; (void)timeout; (void)Reserved;
	DebugLog("WSAConnectByNameA: %s %s", nodename ? nodename : "(null)", servicename ? servicename : "(null)");
	err = WINSOCK351_getaddrinfo(nodename, servicename, NULL, &res);
	if (err != 0) {
		WSASetLastError(err);
		return SOCKET_ERROR;
	}
	for (ai = res; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET6)
			continue;
		if (connect(s, ai->ai_addr, (int)ai->ai_addrlen) == 0) {
			if (RemoteAddress != NULL && RemoteAddressLength != NULL && *RemoteAddressLength >= (DWORD)ai->ai_addrlen) {
				memcpy(RemoteAddress, ai->ai_addr, ai->ai_addrlen);
				*RemoteAddressLength = (DWORD)ai->ai_addrlen;
			}
			if (LocalAddress != NULL && LocalAddressLength != NULL && *LocalAddressLength >= sizeof(struct sockaddr_in)) {
				int llen = (int)*LocalAddressLength;
				getsockname(s, LocalAddress, &llen);
				*LocalAddressLength = (DWORD)llen;
			}
			WINSOCK351_freeaddrinfo(res);
			return 0;
		}
	}
	err = WSAGetLastError();
	WINSOCK351_freeaddrinfo(res);
	if (err == 0)
		err = WSAECONNREFUSED;
	WSASetLastError(err);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSAConnectByNameW(SOCKET s, LPCWSTR nodename, LPCWSTR servicename, LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress, LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved) {
	char nodeA[256], servA[64];
	const char* pNode = NULL;
	const char* pServ = NULL;
	DebugLog("WSAConnectByNameW");
	if (nodename != NULL) {
		if (!WideToAnsi(nodename, nodeA, sizeof(nodeA))) {
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		pNode = nodeA;
	}
	if (servicename != NULL) {
		if (!WideToAnsi(servicename, servA, sizeof(servA))) {
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		pServ = servA;
	}
	return WINSOCK351_WSAConnectByNameA(s, pNode, pServ, LocalAddressLength, LocalAddress, RemoteAddressLength, RemoteAddress, timeout, Reserved);
}

SOCKET WSAAPI WINSOCK351_WSAAccept(SOCKET s, struct sockaddr FAR * addr, LPINT addrlen, LPCONDITIONPROC lpfnCondition, DWORD dwCallbackData) {
	SOCKET ns;
	(void)lpfnCondition; (void)dwCallbackData;
	DebugLog("WSAAccept");
	/* Condition callback ignored: wsock32 has no QoS/group support. */
	ns = accept(s, addr, addrlen);
	return ns;
}

int WSAAPI WINSOCK351_WSAJoinLeaf(SOCKET s, const struct sockaddr FAR * name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS, DWORD dwFlags) {
	(void)lpCallerData; (void)lpCalleeData; (void)lpSQOS; (void)lpGQOS; (void)dwFlags;
	DebugLog("WSAJoinLeaf");
	if (name == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	return connect(s, (struct sockaddr*)name, namelen);
}

int WSAAPI WINSOCK351_WSADuplicateSocketA(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOA lpProtocolInfo) {
	(void)s; (void)dwProcessId; (void)lpProtocolInfo;
	DebugLog("WSADuplicateSocketA: NOTIMPL");
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSADuplicateSocketW(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOW lpProtocolInfo) {
	(void)s; (void)dwProcessId; (void)lpProtocolInfo;
	DebugLog("WSADuplicateSocketW: NOTIMPL");
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

/* ---------- WSARecv / WSASend family ---------- */

int WSAAPI WINSOCK351_WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	DWORD total = 0;
	DWORD i;
	int flags;
	DebugLog("WSARecv: %d bufs", dwBufferCount);
	if (OverlappedNotSupported(lpOverlapped, lpCompletionRoutine))
		return SOCKET_ERROR;
	if (lpBuffers == NULL || lpNumberOfBytesRecvd == NULL || lpFlags == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (dwBufferCount == 0) {
		*lpNumberOfBytesRecvd = 0;
		return 0;
	}
	flags = (int)*lpFlags;
	*lpNumberOfBytesRecvd = 0;
	for (i = 0; i < dwBufferCount; i++) {
		int r;
		if (lpBuffers[i].buf == NULL && lpBuffers[i].len != 0) {
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		if (lpBuffers[i].len == 0)
			continue;
		r = recv(s, lpBuffers[i].buf, (int)lpBuffers[i].len, flags);
		if (r == SOCKET_ERROR) {
			if (total > 0) {
				*lpNumberOfBytesRecvd = total;
				return 0;
			}
			return SOCKET_ERROR;
		}
		total += (DWORD)r;
		*lpNumberOfBytesRecvd = total;
		if (r < (int)lpBuffers[i].len)
			break; /* no more data available now */
		flags = 0; /* only first recv uses MSG_PEEK/OOB etc. */
		if (r == 0)
			break; /* graceful close */
	}
	return 0;
}

int WSAAPI WINSOCK351_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, struct sockaddr FAR * lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	DWORD total = 0;
	int flags;
	DebugLog("WSARecvFrom");
	if (OverlappedNotSupported(lpOverlapped, lpCompletionRoutine))
		return SOCKET_ERROR;
	if (lpBuffers == NULL || lpNumberOfBytesRecvd == NULL || lpFlags == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (dwBufferCount == 0) {
		*lpNumberOfBytesRecvd = 0;
		return 0;
	}
	flags = (int)*lpFlags;
	/* Multi-buffer datagrams can't be scattered without MSG_TRUNC tricks;
	   support single buffer directly, else coalesce via temp not needed for DGRAM
	   (datagram fits first buffer or truncates). Use first buffer. */
	if (dwBufferCount == 1) {
		int r = recvfrom(s, lpBuffers[0].buf, (int)lpBuffers[0].len, flags, lpFrom, lpFromlen);
		if (r == SOCKET_ERROR)
			return SOCKET_ERROR;
		*lpNumberOfBytesRecvd = (DWORD)r;
		return 0;
	}
	/* Stream fallback: same as WSARecv but preserve `from' on first chunk. */
	{
		DWORD i;
		*lpNumberOfBytesRecvd = 0;
		for (i = 0; i < dwBufferCount; i++) {
			int r;
			if (i == 0)
				r = recvfrom(s, lpBuffers[i].buf, (int)lpBuffers[i].len, flags, lpFrom, lpFromlen);
			else
				r = recv(s, lpBuffers[i].buf, (int)lpBuffers[i].len, 0);
			if (r == SOCKET_ERROR) {
				if (total > 0) {
					*lpNumberOfBytesRecvd = total;
					return 0;
				}
				return SOCKET_ERROR;
			}
			total += (DWORD)r;
			*lpNumberOfBytesRecvd = total;
			if (r < (int)lpBuffers[i].len)
				break;
			if (r == 0)
				break;
		}
	}
	return 0;
}

int WSAAPI WINSOCK351_WSARecvDisconnect(SOCKET s, LPWSABUF lpInboundDisconnectData) {
	(void)lpInboundDisconnectData;
	DebugLog("WSARecvDisconnect");
	if (shutdown(s, 0 /* SD_RECEIVE */) == SOCKET_ERROR)
		return SOCKET_ERROR;
	return 0;
}

int WSAAPI WINSOCK351_WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	DWORD total = 0;
	DWORD i;
	DebugLog("WSASend: %d bufs", dwBufferCount);
	if (OverlappedNotSupported(lpOverlapped, lpCompletionRoutine))
		return SOCKET_ERROR;
	if (lpBuffers == NULL || lpNumberOfBytesSent == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (dwBufferCount == 0) {
		*lpNumberOfBytesSent = 0;
		return 0;
	}
	*lpNumberOfBytesSent = 0;
	for (i = 0; i < dwBufferCount; i++) {
		int r;
		u_long left = lpBuffers[i].len;
		char FAR * p = lpBuffers[i].buf;
		if (p == NULL && left != 0) {
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		while (left > 0) {
			r = send(s, p, (int)left, (int)dwFlags);
			if (r == SOCKET_ERROR) {
				if (total > 0) {
					*lpNumberOfBytesSent = total;
					return 0;
				}
				return SOCKET_ERROR;
			}
			total += (DWORD)r;
			*lpNumberOfBytesSent = total;
			p += r;
			left -= (u_long)r;
			if (r == 0)
				break;
		}
	}
	return 0;
}

int WSAAPI WINSOCK351_WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags, const struct sockaddr FAR * lpTo, int iTolen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	DWORD total = 0;
	DWORD i;
	DebugLog("WSASendTo");
	if (OverlappedNotSupported(lpOverlapped, lpCompletionRoutine))
		return SOCKET_ERROR;
	if (lpBuffers == NULL || lpNumberOfBytesSent == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (dwBufferCount == 0) {
		*lpNumberOfBytesSent = 0;
		return 0;
	}
	*lpNumberOfBytesSent = 0;
	for (i = 0; i < dwBufferCount; i++) {
		int r;
		if (lpBuffers[i].buf == NULL && lpBuffers[i].len != 0) {
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		if (lpBuffers[i].len == 0)
			continue;
		/* Only first datagram chunk carries the destination. */
		if (i == 0)
			r = sendto(s, lpBuffers[i].buf, (int)lpBuffers[i].len, (int)dwFlags, (struct sockaddr*)lpTo, iTolen);
		else
			r = send(s, lpBuffers[i].buf, (int)lpBuffers[i].len, (int)dwFlags);
		if (r == SOCKET_ERROR) {
			if (total > 0) {
				*lpNumberOfBytesSent = total;
				return 0;
			}
			return SOCKET_ERROR;
		}
		total += (DWORD)r;
		*lpNumberOfBytesSent = total;
	}
	return 0;
}

int WSAAPI WINSOCK351_WSASendDisconnect(SOCKET s, LPWSABUF lpOutboundDisconnectData) {
	(void)lpOutboundDisconnectData;
	DebugLog("WSASendDisconnect");
	if (shutdown(s, 1 /* SD_SEND */) == SOCKET_ERROR)
		return SOCKET_ERROR;
	return 0;
}

int WSAAPI WINSOCK351_WSASendMsg(SOCKET s, LPWSAMSG lpMsg, DWORD dwFlags, LPDWORD lpNumberOfBytesSent, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
	DebugLog("WSASendMsg");
	if (lpMsg == NULL || lpNumberOfBytesSent == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	/* Control data (ancillary) ignored on wsock32 1.1. */
	return WINSOCK351_WSASendTo(s, lpMsg->lpBuffers, lpMsg->dwBufferCount, lpNumberOfBytesSent, dwFlags, lpMsg->name, lpMsg->namelen, lpOverlapped, lpCompletionRoutine);
}

BOOL WSAAPI WINSOCK351_WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED lpOverlapped, LPDWORD lpcbTransfer, BOOL fWait, LPDWORD lpdwFlags) {
	(void)s; (void)lpOverlapped; (void)lpcbTransfer; (void)fWait; (void)lpdwFlags;
	DebugLog("WSAGetOverlappedResult: NOTIMPL (no overlapped I/O)");
	WSASetLastError(WSAEOPNOTSUPP);
	return FALSE;
}

/* ---------- byte order ---------- */

int WSAAPI WINSOCK351_WSAHtonl(SOCKET s, u_long hostlong, u_long FAR * lpnetlong) {
	(void)s;
	if (lpnetlong == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lpnetlong = htonl(hostlong);
	return 0;
}

int WSAAPI WINSOCK351_WSAHtons(SOCKET s, u_short hostshort, u_short FAR * lpnetshort) {
	(void)s;
	if (lpnetshort == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lpnetshort = htons(hostshort);
	return 0;
}

int WSAAPI WINSOCK351_WSANtohl(SOCKET s, u_long netlong, u_long FAR * lphostlong) {
	(void)s;
	if (lphostlong == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lphostlong = ntohl(netlong);
	return 0;
}

int WSAAPI WINSOCK351_WSANtohs(SOCKET s, u_short netshort, u_short FAR * lphostshort) {
	(void)s;
	if (lphostshort == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lphostshort = ntohs(netshort);
	return 0;
}

/* ---------- WSAPoll via select ---------- */

int WSAAPI WINSOCK351_WSAPoll(LPWSAPOLLFD fdarray, ULONG nfds, INT timeout) {
	ULONG i;
	int nready = 0;
	fd_set rfds, wfds, efds;
	struct timeval tv, *ptv = NULL;
	int r;
	if (fdarray == NULL && nfds != 0) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (nfds == 0) {
		if (timeout > 0)
			Sleep((DWORD)timeout);
		return 0;
	}
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_ZERO(&efds);
	for (i = 0; i < nfds; i++) {
		fdarray[i].revents = 0;
		if (fdarray[i].fd == INVALID_SOCKET) {
			fdarray[i].revents = POLLNVAL;
			nready++;
			continue;
		}
		if (fdarray[i].events & POLLIN)
			FD_SET(fdarray[i].fd, &rfds);
		if (fdarray[i].events & POLLOUT)
			FD_SET(fdarray[i].fd, &wfds);
		if (fdarray[i].events & POLLPRI)
			FD_SET(fdarray[i].fd, &efds);
		/* Always poll errors. */
		FD_SET(fdarray[i].fd, &efds);
	}
	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		ptv = &tv;
	}
	r = select(0, &rfds, &wfds, &efds, ptv);
	if (r == SOCKET_ERROR)
		return SOCKET_ERROR;
	nready = 0;
	for (i = 0; i < nfds; i++) {
		SHORT rev = 0;
		if (fdarray[i].fd == INVALID_SOCKET) {
			nready++;
			continue;
		}
		if (FD_ISSET(fdarray[i].fd, &rfds))
			rev |= (SHORT)(fdarray[i].events & POLLIN);
		if (FD_ISSET(fdarray[i].fd, &wfds))
			rev |= (SHORT)(fdarray[i].events & POLLOUT);
		if (FD_ISSET(fdarray[i].fd, &efds)) {
			/* Distinguish real OOB from generic error: report PRI if asked,
			   else ERR so caller wakes up. */
			if (fdarray[i].events & POLLPRI)
				rev |= (SHORT)(fdarray[i].events & POLLPRI);
			else
				rev |= POLLERR;
		}
		fdarray[i].revents = rev;
		if (rev != 0)
			nready++;
	}
	return nready;
}

/* ---------- getaddrinfo / freeaddrinfo / getnameinfo (IPv4) ---------- */

void WSAAPI WINSOCK351_freeaddrinfo(LPADDRINFOA pAddrInfo) {
	LPADDRINFOA cur = pAddrInfo, nxt;
	while (cur != NULL) {
		nxt = cur->ai_next;
		if (cur->ai_addr != NULL)
			free(cur->ai_addr);
		if (cur->ai_canonname != NULL)
			free(cur->ai_canonname);
		free(cur);
		cur = nxt;
	}
}

static int NewAddrInfoEntry(LPADDRINFOA hints, struct sockaddr_in* sin, const char* canon, LPADDRINFOA* out) {
	LPADDRINFOA ai;
	struct sockaddr_in* copy;
	ai = (LPADDRINFOA)malloc(sizeof(*ai));
	if (ai == NULL)
		return EAI_MEMORY;
	copy = (struct sockaddr_in*)malloc(sizeof(*copy));
	if (copy == NULL) {
		free(ai);
		return EAI_MEMORY;
	}
	memcpy(copy, sin, sizeof(*copy));
	memset(ai, 0, sizeof(*ai));
	ai->ai_family = AF_INET;
	ai->ai_socktype = hints ? hints->ai_socktype : 0;
	ai->ai_protocol = hints ? hints->ai_protocol : 0;
	if (ai->ai_socktype == 0) {
		/* Default per protocol. */
		if (ai->ai_protocol == IPPROTO_TCP)
			ai->ai_socktype = SOCK_STREAM;
		else if (ai->ai_protocol == IPPROTO_UDP)
			ai->ai_socktype = SOCK_DGRAM;
	}
	if (ai->ai_protocol == 0) {
		if (ai->ai_socktype == SOCK_STREAM)
			ai->ai_protocol = IPPROTO_TCP;
		else if (ai->ai_socktype == SOCK_DGRAM)
			ai->ai_protocol = IPPROTO_UDP;
	}
	ai->ai_addrlen = sizeof(*copy);
	ai->ai_addr = (struct sockaddr*)copy;
	if (canon != NULL && hints != NULL && (hints->ai_flags & AI_CANONNAME)) {
		ai->ai_canonname = StrDupN(canon);
		if (ai->ai_canonname == NULL) {
			free(copy);
			free(ai);
			return EAI_MEMORY;
		}
		ai->ai_flags |= AI_CANONNAME;
	}
	*out = ai;
	return 0;
}

int WSAAPI WINSOCK351_getaddrinfo(const char FAR * pNodeName, const char FAR * pServiceName, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult) {
	LPADDRINFOA hints = (LPADDRINFOA)pHints;
	int family = AF_UNSPEC, socktype = 0, protocol = 0, flags = 0;
	u_short port = 0;
	const char* protoName = NULL;
	LPADDRINFOA head = NULL, tail = NULL;
	DebugLog("getaddrinfo: %s %s", pNodeName ? pNodeName : "(null)", pServiceName ? pServiceName : "(null)");
	if (ppResult == NULL) {
		WSASetLastError(WSAEFAULT);
		return EAI_FAIL;
	}
	*ppResult = NULL;
	if (hints != NULL) {
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		flags = hints->ai_flags;
		if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV))
			return EAI_BADFLAGS;
		if (family != AF_UNSPEC && family != AF_INET)
			return EAI_FAMILY;
		if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM && socktype != SOCK_RAW)
			return EAI_SOCKTYPE;
	}
	if (pServiceName != NULL && pServiceName[0] != '\0') {
		if (socktype == SOCK_DGRAM)
			protoName = "udp";
		else
			protoName = "tcp";
		if (flags & AI_NUMERICSERV) {
			int p = atoi(pServiceName);
			if (p <= 0 || p > 65535)
				return EAI_SERVICE;
			port = htons((u_short)p);
		} else {
			port = ResolveServicePort(pServiceName, protoName);
			if (port == 0xFFFF) {
				/* Try pure numeric before failing. */
				int p = atoi(pServiceName);
				if (p > 0 && p < 65536)
					port = htons((u_short)p);
				else
					return EAI_SERVICE;
			}
		}
	}
	/* No node: passive -> ANY, else loopback. */
	if (pNodeName == NULL || pNodeName[0] == '\0') {
		struct sockaddr_in sin;
		LPADDRINFOA ai = NULL;
		int err;
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = port;
		sin.sin_addr.s_addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
		err = NewAddrInfoEntry(hints, &sin, NULL, &ai);
		if (err != 0)
			return err;
		if (socktype == 0) {
			/* Return both TCP and UDP entries like stock getaddrinfo. */
			LPADDRINFOA ai2 = NULL;
			ADDRINFOA tmp = hints ? *hints : *(LPADDRINFOA)0;
			/* Build second entry manually to avoid touching hints==NULL. */
			struct sockaddr_in sin2 = sin;
			(void)tmp;
			/* ai already has protocol defaulted; clone and flip. */
			ai2 = (LPADDRINFOA)malloc(sizeof(*ai2));
			if (ai2 == NULL) {
				WINSOCK351_freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			memcpy(ai2, ai, sizeof(*ai2));
			ai2->ai_addr = (struct sockaddr*)malloc(sizeof(sin2));
			if (ai2->ai_addr == NULL) {
				free(ai2);
				WINSOCK351_freeaddrinfo(ai);
				return EAI_MEMORY;
			}
			memcpy(ai2->ai_addr, &sin2, sizeof(sin2));
			ai2->ai_next = NULL;
			ai->ai_next = NULL;
			if (ai->ai_socktype == 0) {
				ai->ai_socktype = SOCK_STREAM;
				ai->ai_protocol = IPPROTO_TCP;
				ai2->ai_socktype = SOCK_DGRAM;
				ai2->ai_protocol = IPPROTO_UDP;
			} else if (ai->ai_socktype == SOCK_STREAM) {
				ai2->ai_socktype = SOCK_DGRAM;
				ai2->ai_protocol = IPPROTO_UDP;
			} else {
				ai2->ai_socktype = SOCK_STREAM;
				ai2->ai_protocol = IPPROTO_TCP;
			}
			/* Only return both when caller didn't constrain type. */
			if (hints != NULL && hints->ai_socktype != 0) {
				free(ai2->ai_addr);
				free(ai2);
			} else {
				ai->ai_next = ai2;
			}
		}
		*ppResult = ai;
		(void)head; (void)tail;
		return 0;
	}
	/* Numeric IPv4 fast path. */
	{
		unsigned long addr = inet_addr(pNodeName);
		if (addr != INADDR_NONE || strcmp(pNodeName, "255.255.255.255") == 0) {
			struct sockaddr_in sin;
			LPADDRINFOA ai = NULL;
			int err;
			if ((flags & AI_NUMERICHOST) == 0) {
				/* still fine: numeric string always accepted */
			}
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = port;
			sin.sin_addr.s_addr = addr;
			err = NewAddrInfoEntry(hints, &sin, pNodeName, &ai);
			if (err != 0)
				return err;
			*ppResult = ai;
			return 0;
		}
		if (flags & AI_NUMERICHOST)
			return EAI_NONAME;
	}
	/* DNS via gethostbyname (IPv4 only). */
	{
		struct hostent FAR *he;
		int i;
		he = gethostbyname((char*)pNodeName);
		if (he == NULL || he->h_addrtype != AF_INET || he->h_addr_list == NULL || he->h_addr_list[0] == NULL) {
			int e = WSAGetLastError();
			if (e == WSAHOST_NOT_FOUND || e == WSATRY_AGAIN || e == WSANO_RECOVERY)
				return EAI_NONAME;
			return EAI_FAIL;
		}
		for (i = 0; he->h_addr_list[i] != NULL; i++) {
			struct sockaddr_in sin;
			LPADDRINFOA ai = NULL;
			int err;
			memset(&sin, 0, sizeof(sin));
			sin.sin_family = AF_INET;
			sin.sin_port = port;
			memcpy(&sin.sin_addr, he->h_addr_list[i], sizeof(sin.sin_addr));
			err = NewAddrInfoEntry(hints, &sin, (he->h_name && i == 0) ? he->h_name : NULL, &ai);
			if (err != 0) {
				WINSOCK351_freeaddrinfo(head);
				return err;
			}
			if (head == NULL)
				head = tail = ai;
			else {
				tail->ai_next = ai;
				tail = ai;
			}
		}
		*ppResult = head;
		return 0;
	}
}

int WSAAPI WINSOCK351_getnameinfo(const struct sockaddr FAR * sa, int salen, char FAR * host, DWORD hostlen, char FAR * serv, DWORD servlen, int flags) {
	const struct sockaddr_in FAR * sin;
	DebugLog("getnameinfo");
	if (sa == NULL || salen < (int)sizeof(struct sockaddr)) {
		WSASetLastError(WSAEFAULT);
		return EAI_FAIL;
	}
	if (sa->sa_family != AF_INET) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return EAI_FAMILY;
	}
	if (salen < (int)sizeof(struct sockaddr_in)) {
		WSASetLastError(WSAEFAULT);
		return EAI_FAIL;
	}
	sin = (const struct sockaddr_in FAR *)sa;
	if (host != NULL && hostlen > 0) {
		if (flags & NI_NUMERICHOST) {
			const char* dotted = inet_ntoa(sin->sin_addr);
			if (dotted == NULL) {
				WSASetLastError(WSAEFAULT);
				return EAI_FAIL;
			}
			if (strlen(dotted) + 1 > hostlen) {
				WSASetLastError(WSAEFAULT);
				return EAI_FAIL;
			}
			strcpy(host, dotted);
		} else {
			struct hostent FAR *he = gethostbyaddr((const char*)&sin->sin_addr, sizeof(sin->sin_addr), AF_INET);
			if (he != NULL && he->h_name != NULL) {
				if (flags & NI_NOFQDN) {
					/* Strip domain: up to first dot. */
					const char* dot = strchr(he->h_name, '.');
					size_t n = dot ? (size_t)(dot - he->h_name) : strlen(he->h_name);
					if (n + 1 > hostlen) {
						WSASetLastError(WSAEFAULT);
						return EAI_FAIL;
					}
					memcpy(host, he->h_name, n);
					host[n] = '\0';
				} else {
					if (strlen(he->h_name) + 1 > hostlen) {
						WSASetLastError(WSAEFAULT);
						return EAI_FAIL;
					}
					strcpy(host, he->h_name);
				}
			} else {
				if (flags & NI_NAMEREQD)
					return EAI_NONAME;
				{
					const char* dotted = inet_ntoa(sin->sin_addr);
					if (dotted == NULL)
						return EAI_FAIL;
					if (strlen(dotted) + 1 > hostlen)
						return EAI_FAIL;
					strcpy(host, dotted);
				}
			}
		}
	}
	if (serv != NULL && servlen > 0) {
		int port = ntohs(sin->sin_port);
		if (flags & NI_NUMERICSERV) {
			char tmp[16];
			_snprintf(tmp, sizeof(tmp), "%d", port);
			tmp[sizeof(tmp)-1] = '\0';
			if (strlen(tmp) + 1 > servlen) {
				WSASetLastError(WSAEFAULT);
				return EAI_FAIL;
			}
			strcpy(serv, tmp);
		} else {
			struct servent FAR *se = getservbyport((int)sin->sin_port, (flags & NI_DGRAM) ? "udp" : "tcp");
			if (se != NULL && se->s_name != NULL) {
				if (strlen(se->s_name) + 1 > servlen) {
					WSASetLastError(WSAEFAULT);
					return EAI_FAIL;
				}
				strcpy(serv, se->s_name);
			} else {
				char tmp[16];
				_snprintf(tmp, sizeof(tmp), "%d", port);
				tmp[sizeof(tmp)-1] = '\0';
				if (strlen(tmp) + 1 > servlen) {
					WSASetLastError(WSAEFAULT);
					return EAI_FAIL;
				}
				strcpy(serv, tmp);
			}
		}
	}
	return 0;
}

/* Wide / Ex wrappers */

void WSAAPI WINSOCK351_FreeAddrInfoW(LPADDRINFOA pAddrInfo) {
	WINSOCK351_freeaddrinfo(pAddrInfo);
}

void WSAAPI WINSOCK351_FreeAddrInfoEx(LPADDRINFOA pAddrInfo) {
	WINSOCK351_freeaddrinfo(pAddrInfo);
}

void WSAAPI WINSOCK351_FreeAddrInfoExW(LPADDRINFOA pAddrInfo) {
	WINSOCK351_freeaddrinfo(pAddrInfo);
}

int WSAAPI WINSOCK351_GetAddrInfoW(LPCWSTR pNodeName, LPCWSTR pServiceName, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult) {
	char nodeA[256], servA[64];
	const char* pNode = NULL;
	const char* pServ = NULL;
	if (ppResult == NULL) {
		WSASetLastError(WSAEFAULT);
		return EAI_FAIL;
	}
	if (pNodeName != NULL) {
		if (!WideToAnsi(pNodeName, nodeA, sizeof(nodeA)))
			return EAI_MEMORY;
		pNode = nodeA;
	}
	if (pServiceName != NULL) {
		if (!WideToAnsi(pServiceName, servA, sizeof(servA)))
			return EAI_MEMORY;
		pServ = servA;
	}
	return WINSOCK351_getaddrinfo(pNode, pServ, pHints, ppResult);
}

int WSAAPI WINSOCK351_GetNameInfoW(const struct sockaddr FAR * sa, int salen, LPWSTR host, DWORD hostlen, LPWSTR serv, DWORD servlen, int flags) {
	char hA[256], sA[64];
	char* pH = NULL;
	char* pS = NULL;
	DWORD hlenA = 0, slenA = 0;
	int err;
	if (host != NULL) {
		if (hostlen == 0)
			return EAI_FAIL;
		/* hostlen is in WCHARs; ANSI buffer sized in bytes is fine. */
		pH = hA;
		hlenA = sizeof(hA);
	}
	if (serv != NULL) {
		if (servlen == 0)
			return EAI_FAIL;
		pS = sA;
		slenA = sizeof(sA);
	}
	err = WINSOCK351_getnameinfo(sa, salen, pH, hlenA, pS, slenA, flags);
	if (err != 0)
		return err;
	if (host != NULL) {
		if (!AnsiToWide(pH, host, (int)hostlen))
			return EAI_MEMORY;
	}
	if (serv != NULL) {
		if (!AnsiToWide(pS, serv, (int)servlen))
			return EAI_MEMORY;
	}
	return 0;
}

int WSAAPI WINSOCK351_GetHostNameW(LPWSTR name, int namelen) {
	char tmp[256];
	if (name == NULL || namelen <= 0) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (gethostname(tmp, sizeof(tmp)) == SOCKET_ERROR)
		return SOCKET_ERROR;
	if (!AnsiToWide(tmp, name, namelen)) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	return 0;
}

int WSAAPI WINSOCK351_GetAddrInfoExA(const char FAR * pNodeName, const char FAR * pServiceName, DWORD dwNameSpace, LPGUID lpNspId, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult, LPVOID lpCallback, LPVOID lpContext, HANDLE FAR * handle) {
	(void)dwNameSpace; (void)lpNspId; (void)lpCallback; (void)lpContext; (void)handle;
	if (lpCallback != NULL || handle != NULL) {
		/* Async not supported; sync only. */
		WSASetLastError(WSAEOPNOTSUPP);
		return WSAEOPNOTSUPP;
	}
	return WINSOCK351_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
}

int WSAAPI WINSOCK351_GetAddrInfoExW(LPCWSTR pNodeName, LPCWSTR pServiceName, DWORD dwNameSpace, LPGUID lpNspId, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult, LPVOID lpCallback, LPVOID lpContext, HANDLE FAR * handle) {
	(void)dwNameSpace; (void)lpNspId; (void)lpCallback; (void)lpContext; (void)handle;
	if (lpCallback != NULL || handle != NULL) {
		WSASetLastError(WSAEOPNOTSUPP);
		return WSAEOPNOTSUPP;
	}
	return WINSOCK351_GetAddrInfoW(pNodeName, pServiceName, pHints, ppResult);
}

int WSAAPI WINSOCK351_GetAddrInfoExCancel(HANDLE FAR * handle) {
	(void)handle;
	WSASetLastError(WSAEOPNOTSUPP);
	return WSAEOPNOTSUPP;
}

int WSAAPI WINSOCK351_GetAddrInfoExOverlappedResult(LPVOID lpOverlapped) {
	(void)lpOverlapped;
	WSASetLastError(WSAEOPNOTSUPP);
	return WSAEOPNOTSUPP;
}

int WSAAPI WINSOCK351_SetAddrInfoExA(const char FAR * pNodeName, const char FAR * pServiceName, const LPVOID pBlob, DWORD dwFlags, LPGUID lpNspId, DWORD dwTimeout, LPVOID lpContext, LPVOID lpCallback, HANDLE FAR * handle) {
	(void)pNodeName; (void)pServiceName; (void)pBlob; (void)dwFlags;
	(void)lpNspId; (void)dwTimeout; (void)lpContext; (void)lpCallback; (void)handle;
	WSASetLastError(WSAEOPNOTSUPP);
	return WSAEOPNOTSUPP;
}

int WSAAPI WINSOCK351_SetAddrInfoExW(LPCWSTR pNodeName, LPCWSTR pServiceName, const LPVOID pBlob, DWORD dwFlags, LPGUID lpNspId, DWORD dwTimeout, LPVOID lpContext, LPVOID lpCallback, HANDLE FAR * handle) {
	(void)pNodeName; (void)pServiceName; (void)pBlob; (void)dwFlags;
	(void)lpNspId; (void)dwTimeout; (void)lpContext; (void)lpCallback; (void)handle;
	WSASetLastError(WSAEOPNOTSUPP);
	return WSAEOPNOTSUPP;
}

/* ---------- string conversions ---------- */

INT WSAAPI WINSOCK351_WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSTR lpszAddressString, LPDWORD lpdwAddressStringLength) {
	const struct sockaddr_in FAR * sin;
	const char* dotted;
	char tmp[32];
	(void)lpProtocolInfo;
	if (lpsaAddress == NULL || lpdwAddressStringLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (dwAddressLength < sizeof(struct sockaddr_in) || lpsaAddress->sa_family != AF_INET) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return SOCKET_ERROR;
	}
	sin = (const struct sockaddr_in FAR *)lpsaAddress;
	dotted = inet_ntoa(sin->sin_addr);
	if (dotted == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	_snprintf(tmp, sizeof(tmp), "%s:%d", dotted, ntohs(sin->sin_port));
	tmp[sizeof(tmp)-1] = '\0';
	if (lpszAddressString == NULL || *lpdwAddressStringLength <= strlen(tmp)) {
		*lpdwAddressStringLength = (DWORD)strlen(tmp) + 1;
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	strcpy(lpszAddressString, tmp);
	*lpdwAddressStringLength = (DWORD)strlen(tmp) + 1;
	return 0;
}

INT WSAAPI WINSOCK351_WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, LPWSAPROTOCOL_INFOW lpProtocolInfo, LPWSTR lpszAddressString, LPDWORD lpdwAddressStringLength) {
	char tmpA[32];
	DWORD lenA = sizeof(tmpA);
	INT r;
	(void)lpProtocolInfo;
	/* Reuse ANSI logic with dummy protocol info. */
	r = WINSOCK351_WSAAddressToStringA(lpsaAddress, dwAddressLength, NULL, tmpA, &lenA);
	if (r == SOCKET_ERROR) {
		if (lpdwAddressStringLength != NULL)
			*lpdwAddressStringLength = lenA;
		return SOCKET_ERROR;
	}
	if (lpszAddressString == NULL || lpdwAddressStringLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (*lpdwAddressStringLength <= strlen(tmpA)) {
		*lpdwAddressStringLength = (DWORD)strlen(tmpA) + 1;
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (!AnsiToWide(tmpA, lpszAddressString, (int)*lpdwAddressStringLength)) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lpdwAddressStringLength = (DWORD)strlen(tmpA) + 1;
	return 0;
}

INT WSAAPI WINSOCK351_WSAStringToAddressA(LPSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSOCKADDR lpAddress, LPINT lpAddressLength) {
	char* colon;
	char ipPart[32];
	int port = 0;
	unsigned long addr;
	struct sockaddr_in FAR * sin;
	(void)lpProtocolInfo;
	if (AddressString == NULL || lpAddress == NULL || lpAddressLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (AddressFamily != AF_INET) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return SOCKET_ERROR;
	}
	if (*lpAddressLength < (INT)sizeof(struct sockaddr_in)) {
		*lpAddressLength = sizeof(struct sockaddr_in);
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	/* Split optional ":port". Use last colon (IPv4 has no colons otherwise). */
	colon = strchr(AddressString, ':');
	if (colon != NULL) {
		size_t n = (size_t)(colon - AddressString);
		if (n >= sizeof(ipPart)) {
			WSASetLastError(WSAEINVAL);
			return SOCKET_ERROR;
		}
		memcpy(ipPart, AddressString, n);
		ipPart[n] = '\0';
		port = atoi(colon + 1);
		if (port < 0 || port > 65535) {
			WSASetLastError(WSAEINVAL);
			return SOCKET_ERROR;
		}
	} else {
		if (strlen(AddressString) >= sizeof(ipPart)) {
			WSASetLastError(WSAEINVAL);
			return SOCKET_ERROR;
		}
		strcpy(ipPart, AddressString);
	}
	addr = inet_addr(ipPart);
	if (addr == INADDR_NONE && strcmp(ipPart, "255.255.255.255") != 0) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	sin = (struct sockaddr_in FAR *)lpAddress;
	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	sin->sin_port = htons((u_short)port);
	*lpAddressLength = sizeof(struct sockaddr_in);
	return 0;
}

INT WSAAPI WINSOCK351_WSAStringToAddressW(LPWSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOW lpProtocolInfo, LPSOCKADDR lpAddress, LPINT lpAddressLength) {
	char tmpA[64];
	(void)lpProtocolInfo;
	if (AddressString == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (!WideToAnsi(AddressString, tmpA, sizeof(tmpA))) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	return WINSOCK351_WSAStringToAddressA(tmpA, AddressFamily, NULL, lpAddress, lpAddressLength);
}

LPCSTR WSAAPI WINSOCK351_inet_ntop(INT Family, LPCVOID pAddr, LPSTR pStringBuf, size_t StringBufSize) {
	const struct in_addr FAR * in;
	const char* dotted;
	if (Family != AF_INET || pAddr == NULL || pStringBuf == NULL) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return NULL;
	}
	if (StringBufSize < 16) {
		WSASetLastError(WSAEFAULT);
		return NULL;
	}
	in = (const struct in_addr FAR *)pAddr;
	dotted = inet_ntoa(*in);
	if (dotted == NULL) {
		WSASetLastError(WSAEFAULT);
		return NULL;
	}
	strcpy(pStringBuf, dotted);
	return pStringBuf;
}

INT WSAAPI WINSOCK351_inet_pton(INT Family, LPCSTR pszAddrString, LPVOID pAddrBuf) {
	unsigned long addr;
	if (Family != AF_INET || pszAddrString == NULL || pAddrBuf == NULL) {
		WSASetLastError(WSAEAFNOSUPPORT);
		return SOCKET_ERROR;
	}
	addr = inet_addr(pszAddrString);
	if (addr == INADDR_NONE && strcmp(pszAddrString, "255.255.255.255") != 0) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	*(unsigned long*)pAddrBuf = addr;
	return 0;
}

INT WSAAPI WINSOCK351_InetNtopW(INT Family, LPCVOID pAddr, LPWSTR pStringBuf, size_t StringBufSize) {
	char tmp[32];
	if (WINSOCK351_inet_ntop(Family, pAddr, tmp, sizeof(tmp)) == NULL)
		return SOCKET_ERROR;
	/* StringBufSize is in WCHARs for W version. */
	if (StringBufSize <= strlen(tmp)) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (pStringBuf == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (!AnsiToWide(tmp, pStringBuf, (int)StringBufSize))
		return SOCKET_ERROR;
	return 0;
}

INT WSAAPI WINSOCK351_InetPtonW(INT Family, LPCWSTR pszAddrString, LPVOID pAddrBuf) {
	char tmp[64];
	if (pszAddrString == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (!WideToAnsi(pszAddrString, tmp, sizeof(tmp))) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	return WINSOCK351_inet_pton(Family, tmp, pAddrBuf);
}

/* ---------- protocol / namespace database ---------- */

static void FillTcpUdpProtocolInfo(LPWSAPROTOCOL_INFOA info, int tcp) {
	memset(info, 0, sizeof(*info));
	info->dwServiceFlags1 = 0;
	info->dwProviderFlags = 0;
	info->dwCatalogEntryId = tcp ? 1001 : 1002;
	info->ProtocolChain.ChainLen = 1;
	info->ProtocolChain.ChainEntries[0] = info->dwCatalogEntryId;
	info->iVersion = 2;
	info->iAddressFamily = AF_INET;
	info->iMaxSockAddr = sizeof(struct sockaddr_in);
	info->iMinSockAddr = sizeof(struct sockaddr_in);
	info->iSocketType = tcp ? SOCK_STREAM : SOCK_DGRAM;
	info->iProtocol = tcp ? IPPROTO_TCP : IPPROTO_UDP;
	info->iNetworkByteOrder = 0; /* BIGENDIAN? wsock32 uses 0? */
	info->iSecurityScheme = 0;
	info->dwMessageSize = tcp ? 0 : 65507;
	strcpy(info->szProtocol, tcp ? "MSAFD Tcpip [TCP/IP]" : "MSAFD Tcpip [UDP/IP]");
}

int WSAAPI WINSOCK351_WSAEnumProtocolsA(LPINT lpiProtocols, LPWSAPROTOCOL_INFOA lpProtocolBuffer, LPDWORD lpdwBufferLength) {
	DWORD need = 2 * sizeof(WSAPROTOCOL_INFOA);
	int wantTcp = 1, wantUdp = 1;
	int n = 0;
	if (lpdwBufferLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (lpiProtocols != NULL) {
		wantTcp = wantUdp = 0;
		for (; *lpiProtocols != 0; lpiProtocols++) {
			if (*lpiProtocols == IPPROTO_TCP)
				wantTcp = 1;
			else if (*lpiProtocols == IPPROTO_UDP)
				wantUdp = 1;
		}
		n = (wantTcp ? 1 : 0) + (wantUdp ? 1 : 0);
		need = (DWORD)n * sizeof(WSAPROTOCOL_INFOA);
		if (n == 0) {
			*lpdwBufferLength = 0;
			return 0;
		}
	} else {
		n = 2;
	}
	if (lpProtocolBuffer == NULL || *lpdwBufferLength < need) {
		*lpdwBufferLength = need;
		WSASetLastError(WSAENOBUFS);
		return SOCKET_ERROR;
	}
	n = 0;
	if (wantTcp) {
		FillTcpUdpProtocolInfo(&lpProtocolBuffer[n], 1);
		n++;
	}
	if (wantUdp) {
		FillTcpUdpProtocolInfo(&lpProtocolBuffer[n], 0);
		n++;
	}
	*lpdwBufferLength = (DWORD)n * sizeof(WSAPROTOCOL_INFOA);
	return n;
}

int WSAAPI WINSOCK351_WSAEnumProtocolsW(LPINT lpiProtocols, LPWSAPROTOCOL_INFOW lpProtocolBuffer, LPDWORD lpdwBufferLength) {
	/* Implement via ANSI then convert szProtocol. */
	WSAPROTOCOL_INFOA ansi[2];
	DWORD lenA = sizeof(ansi);
	int r, i;
	r = WINSOCK351_WSAEnumProtocolsA(lpiProtocols, ansi, &lenA);
	if (r == SOCKET_ERROR) {
		if (lpdwBufferLength != NULL)
			*lpdwBufferLength = (lenA / sizeof(WSAPROTOCOL_INFOA)) * sizeof(WSAPROTOCOL_INFOW);
		return SOCKET_ERROR;
	}
	if (lpdwBufferLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	if (lpProtocolBuffer == NULL || *lpdwBufferLength < (DWORD)r * sizeof(WSAPROTOCOL_INFOW)) {
		*lpdwBufferLength = (DWORD)r * sizeof(WSAPROTOCOL_INFOW);
		WSASetLastError(WSAENOBUFS);
		return SOCKET_ERROR;
	}
	for (i = 0; i < r; i++) {
		memset(&lpProtocolBuffer[i], 0, sizeof(lpProtocolBuffer[i]));
		lpProtocolBuffer[i].dwServiceFlags1 = ansi[i].dwServiceFlags1;
		lpProtocolBuffer[i].dwServiceFlags2 = ansi[i].dwServiceFlags2;
		lpProtocolBuffer[i].dwServiceFlags3 = ansi[i].dwServiceFlags3;
		lpProtocolBuffer[i].dwServiceFlags4 = ansi[i].dwServiceFlags4;
		lpProtocolBuffer[i].dwProviderFlags = ansi[i].dwProviderFlags;
		lpProtocolBuffer[i].ProviderId = ansi[i].ProviderId;
		lpProtocolBuffer[i].dwCatalogEntryId = ansi[i].dwCatalogEntryId;
		lpProtocolBuffer[i].ProtocolChain = ansi[i].ProtocolChain;
		lpProtocolBuffer[i].iVersion = ansi[i].iVersion;
		lpProtocolBuffer[i].iAddressFamily = ansi[i].iAddressFamily;
		lpProtocolBuffer[i].iMaxSockAddr = ansi[i].iMaxSockAddr;
		lpProtocolBuffer[i].iMinSockAddr = ansi[i].iMinSockAddr;
		lpProtocolBuffer[i].iSocketType = ansi[i].iSocketType;
		lpProtocolBuffer[i].iProtocol = ansi[i].iProtocol;
		lpProtocolBuffer[i].iProtocolMaxOffset = ansi[i].iProtocolMaxOffset;
		lpProtocolBuffer[i].iNetworkByteOrder = ansi[i].iNetworkByteOrder;
		lpProtocolBuffer[i].iSecurityScheme = ansi[i].iSecurityScheme;
		lpProtocolBuffer[i].dwMessageSize = ansi[i].dwMessageSize;
		lpProtocolBuffer[i].dwProviderReserved = ansi[i].dwProviderReserved;
		AnsiToWide(ansi[i].szProtocol, lpProtocolBuffer[i].szProtocol, WSAPROTOCOL_LEN + 1);
	}
	*lpdwBufferLength = (DWORD)r * sizeof(WSAPROTOCOL_INFOW);
	return r;
}

INT WSAAPI WINSOCK351_WSAEnumNameSpaceProvidersA(LPDWORD lpdwBufferLength, LPVOID lpnspBuffer) {
	(void)lpnspBuffer;
	if (lpdwBufferLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	/* Only TCP/IP namespace on 1.1 stack; report none needed. */
	*lpdwBufferLength = 0;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAEnumNameSpaceProvidersW(LPDWORD lpdwBufferLength, LPVOID lpnspBuffer) {
	(void)lpnspBuffer;
	if (lpdwBufferLength == NULL) {
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}
	*lpdwBufferLength = 0;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAGetQOSByName(SOCKET s, LPWSABUF lpQOSName, LPQOS lpQOS) {
	(void)s; (void)lpQOSName; (void)lpQOS;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAGetServiceClassInfoA(LPGUID lpProviderId, LPGUID lpServiceClassId, LPDWORD lpdwBufSize, LPVOID lpServiceClassInfo) {
	(void)lpProviderId; (void)lpServiceClassId; (void)lpdwBufSize; (void)lpServiceClassInfo;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAGetServiceClassInfoW(LPGUID lpProviderId, LPGUID lpServiceClassId, LPDWORD lpdwBufSize, LPVOID lpServiceClassInfo) {
	(void)lpProviderId; (void)lpServiceClassId; (void)lpdwBufSize; (void)lpServiceClassInfo;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

DWORD WSAAPI WINSOCK351_WSAGetServiceClassNameByClassIdA(LPGUID lpServiceClassId, LPSTR lpszServiceClassName, LPDWORD lpdwBufferLength) {
	(void)lpServiceClassId; (void)lpszServiceClassName; (void)lpdwBufferLength;
	WSASetLastError(WSAEOPNOTSUPP);
	return (DWORD)SOCKET_ERROR;
}

DWORD WSAAPI WINSOCK351_WSAGetServiceClassNameByClassIdW(LPGUID lpServiceClassId, LPWSTR lpszServiceClassName, LPDWORD lpdwBufferLength) {
	(void)lpServiceClassId; (void)lpszServiceClassName; (void)lpdwBufferLength;
	WSASetLastError(WSAEOPNOTSUPP);
	return (DWORD)SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAInstallServiceClassA(LPVOID lpServiceClassInfo) {
	(void)lpServiceClassInfo;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAInstallServiceClassW(LPVOID lpServiceClassInfo) {
	(void)lpServiceClassInfo;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSARemoveServiceClass(LPGUID lpServiceClassId) {
	(void)lpServiceClassId;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSANSPIoctl(HANDLE hLookup, DWORD dwControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer, LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned, LPVOID lpCompletion) {
	(void)hLookup; (void)dwControlCode; (void)lpvInBuffer; (void)cbInBuffer;
	(void)lpvOutBuffer; (void)cbOutBuffer; (void)lpcbBytesReturned; (void)lpCompletion;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSALookupServiceBeginA(LPVOID lpqsRestrictions, DWORD dwControlFlags, LPHANDLE lphLookup) {
	(void)lpqsRestrictions; (void)dwControlFlags; (void)lphLookup;
	WSASetLastError(WSASERVICE_NOT_FOUND);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSALookupServiceBeginW(LPVOID lpqsRestrictions, DWORD dwControlFlags, LPHANDLE lphLookup) {
	(void)lpqsRestrictions; (void)dwControlFlags; (void)lphLookup;
	WSASetLastError(WSASERVICE_NOT_FOUND);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSALookupServiceEnd(HANDLE hLookup) {
	(void)hLookup;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSALookupServiceNextA(HANDLE hLookup, DWORD dwControlFlags, LPDWORD lpdwBufferLength, LPVOID lpqsResults) {
	(void)hLookup; (void)dwControlFlags; (void)lpdwBufferLength; (void)lpqsResults;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSALookupServiceNextW(HANDLE hLookup, DWORD dwControlFlags, LPDWORD lpdwBufferLength, LPVOID lpqsResults) {
	(void)hLookup; (void)dwControlFlags; (void)lpdwBufferLength; (void)lpqsResults;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSASetServiceA(LPVOID lpqsRegInfo, DWORD essoperation, DWORD dwControlFlags) {
	(void)lpqsRegInfo; (void)essoperation; (void)dwControlFlags;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSASetServiceW(LPVOID lpqsRegInfo, DWORD essoperation, DWORD dwControlFlags) {
	(void)lpqsRegInfo; (void)essoperation; (void)dwControlFlags;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

INT WSAAPI WINSOCK351_WSAProviderConfigChange(LPHANDLE lpNotificationHandle, LPVOID lpOverlapped, LPVOID lpCompletionRoutine) {
	(void)lpNotificationHandle; (void)lpOverlapped; (void)lpCompletionRoutine;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSApSetPostRoutine(LPVOID lpPostRoutine) {
	(void)lpPostRoutine;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WPUCompleteOverlappedRequest(SOCKET s, LPWSAOVERLAPPED lpOverlapped, DWORD dwError, DWORD cbTransferred, LPVOID lpCompletionRoutine) {
	(void)s; (void)lpOverlapped; (void)dwError; (void)cbTransferred; (void)lpCompletionRoutine;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

/* ---------- WSC stubs ---------- */

int WSAAPI WINSOCK351_WSCDeinstallProvider(LPGUID lpProviderId, LPINT lpErrno) {
	(void)lpProviderId;
	if (lpErrno != NULL)
		*lpErrno = WSAEOPNOTSUPP;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCEnableNSProvider(LPGUID lpProviderId, BOOL fEnable) {
	(void)lpProviderId; (void)fEnable;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCEnumProtocols(LPINT lpiProtocols, LPWSAPROTOCOL_INFOA lpProtocolBuffer, LPDWORD lpdwBufferLength, LPINT lpErrno) {
	int r = WINSOCK351_WSAEnumProtocolsA(lpiProtocols, lpProtocolBuffer, lpdwBufferLength);
	if (r == SOCKET_ERROR && lpErrno != NULL)
		*lpErrno = WSAGetLastError();
	return r;
}

int WSAAPI WINSOCK351_WSCGetProviderPath(LPGUID lpProviderId, LPSTR lpszProviderDllPath, LPINT lpProviderDllPathLen, LPINT lpErrno) {
	(void)lpProviderId; (void)lpszProviderDllPath; (void)lpProviderDllPathLen;
	if (lpErrno != NULL)
		*lpErrno = WSAEOPNOTSUPP;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCInstallNameSpace(LPSTR lpszIdentifier, LPSTR lpszPathName, DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId) {
	(void)lpszIdentifier; (void)lpszPathName; (void)dwNameSpace; (void)dwVersion; (void)lpProviderId;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCInstallProvider(LPGUID lpProviderId, const LPSTR lpszProviderDllPath, const LPVOID lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno) {
	(void)lpProviderId; (void)lpszProviderDllPath; (void)lpProtocolInfoList; (void)dwNumberOfEntries;
	if (lpErrno != NULL)
		*lpErrno = WSAEOPNOTSUPP;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCUnInstallNameSpace(LPGUID lpProviderId) {
	(void)lpProviderId;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCUpdateProvider(LPGUID lpProviderId, const LPSTR lpszProviderDllPath, const LPVOID lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno) {
	(void)lpProviderId; (void)lpszProviderDllPath; (void)lpProtocolInfoList; (void)dwNumberOfEntries;
	if (lpErrno != NULL)
		*lpErrno = WSAEOPNOTSUPP;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCWriteNameSpaceOrder(LPGUID lpProviderId, DWORD dwNumberOfEntries) {
	(void)lpProviderId; (void)dwNumberOfEntries;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}

int WSAAPI WINSOCK351_WSCWriteProviderOrder(LPDWORD lpwdCatalogEntryId, DWORD dwNumberOfEntries) {
	(void)lpwdCatalogEntryId; (void)dwNumberOfEntries;
	WSASetLastError(WSAEOPNOTSUPP);
	return SOCKET_ERROR;
}
