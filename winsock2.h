#ifndef WINSOCK351_WINSOCK2_H
#define WINSOCK351_WINSOCK2_H

#include <winsock.h>
#include "events.h"

#define WSAAPI __stdcall
#define WSA_INVALID_EVENT ((WSAEVENT)NULL)
#define WSA_WAIT_FAILED ((DWORD)-1L)
#define WSA_WAIT_EVENT_0 (WAIT_OBJECT_0)
#define WSA_WAIT_TIMEOUT WAIT_TIMEOUT
#define WSA_INFINITE INFINITE

#ifndef AF_INET6
#define AF_INET6 23
#endif

typedef unsigned int GROUP;
typedef ULONG SERVICETYPE;

#define SG_UNCONSTRAINED_GROUP 0x01
#define SG_CONSTRAINED_GROUP 0x02
#define FROM_PROTOCOL_INFO -1

#define WSA_FLAG_OVERLAPPED 0x01
#define WSA_FLAG_MULTIPOINT_C_ROOT 0x02
#define WSA_FLAG_MULTIPOINT_C_LEAF 0x04
#define WSA_FLAG_MULTIPOINT_D_ROOT 0x08
#define WSA_FLAG_MULTIPOINT_D_LEAF 0x10

#define SO_GROUP_ID 0x2005
#define SO_GROUP_PRIORITY 0x2006
#define SO_PROTOCOL_INFOA 0x2004
/* NOTE: real ws2 header has SO_PROTOCOL_INFOW==0x2005 which collides with
   SO_GROUP_ID; keep same value for compat but never switch() on both. */
#ifndef SO_PROTOCOL_INFOW
#define SO_PROTOCOL_INFOW 0x2005
#endif

#ifndef WSASERVICE_NOT_FOUND
#define WSASERVICE_NOT_FOUND 10108
#endif
#ifndef WSATYPE_NOT_FOUND
#define WSATYPE_NOT_FOUND 10109
#endif
#ifndef WSA_NOT_ENOUGH_MEMORY
#define WSA_NOT_ENOUGH_MEMORY 8
#endif

#ifndef FD_QOS
#define FD_QOS 0x40
#endif
#ifndef FD_GROUP_QOS
#define FD_GROUP_QOS 0x80
#endif
#ifndef FD_ROUTING_INTERFACE_CHANGE
#define FD_ROUTING_INTERFACE_CHANGE 0x100
#endif
#ifndef FD_ADDRESS_LIST_CHANGE
#define FD_ADDRESS_LIST_CHANGE 0x200
#endif
#ifndef FD_MAX_EVENTS
#define FD_MAX_EVENTS 10
#endif

/* getaddrinfo / getnameinfo flags (ws2tcpip.h compatible) */
#ifndef AI_PASSIVE
#define AI_PASSIVE 0x1
#define AI_CANONNAME 0x2
#define AI_NUMERICHOST 0x4
#define AI_NUMERICSERV 0x8
#define AI_ALL 0x100
#define AI_ADDRCONFIG 0x400
#define AI_V4MAPPED 0x800
#endif

#ifndef EAI_AGAIN
#define EAI_AGAIN WSATRY_AGAIN
#define EAI_BADFLAGS WSAEINVAL
#define EAI_FAIL WSANO_RECOVERY
#define EAI_FAMILY WSAEAFNOSUPPORT
#define EAI_MEMORY WSA_NOT_ENOUGH_MEMORY
#define EAI_NONAME WSAHOST_NOT_FOUND
#define EAI_SERVICE WSATYPE_NOT_FOUND
#define EAI_SOCKTYPE WSAESOCKTNOSUPPORT
#endif

#ifndef NI_NOFQDN
#define NI_NOFQDN 0x01
#define NI_NUMERICHOST 0x02
#define NI_NAMEREQD 0x04
#define NI_NUMERICSERV 0x08
#define NI_DGRAM 0x10
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 22
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 65
#endif

/* WSAPoll (Vista+) - emulated via select() */
#ifndef POLLRDNORM
#define POLLRDNORM 0x0100
#define POLLRDBAND 0x0200
#define POLLIN (POLLRDNORM | POLLRDBAND)
#define POLLPRI 0x0400
#define POLLOUT 0x0010
#define POLLWRNORM 0x0010
#define POLLWRBAND 0x0020
#define POLLERR 0x0001
#define POLLHUP 0x0002
#define POLLNVAL 0x0004
#endif

typedef struct _WSABUF {
    u_long      len;     /* the length of the buffer */
    char FAR *  buf;     /* the pointer to the buffer */
} WSABUF, FAR * LPWSABUF;

typedef struct _flowspec {
    ULONG       TokenRate;              /* In Bytes/sec */
    ULONG       TokenBucketSize;        /* In Bytes */
    ULONG       PeakBandwidth;          /* In Bytes/sec */
    ULONG       Latency;                /* In microseconds */
    ULONG       DelayVariation;         /* In microseconds */
    SERVICETYPE ServiceType;
    ULONG       MaxSduSize;             /* In Bytes */
    ULONG       MinimumPolicedSize;     /* In Bytes */

} FLOWSPEC, *PFLOWSPEC, * LPFLOWSPEC;

typedef struct _QualityOfService {
    FLOWSPEC      SendingFlowspec;       /* the flow spec for data sending */
    FLOWSPEC      ReceivingFlowspec;     /* the flow spec for data receiving */
    WSABUF        ProviderSpecific;      /* additional provider specific stuff */
} QOS, FAR * LPQOS;

typedef struct _WSAOVERLAPPED {
  DWORD    Internal;
  DWORD    InternalHigh;
  DWORD    Offset;
  DWORD    OffsetHigh;
  WSAEVENT hEvent;
} WSAOVERLAPPED, *LPWSAOVERLAPPED;

typedef
int
(CALLBACK * LPCONDITIONPROC)(
    LPWSABUF lpCallerId,
    LPWSABUF lpCallerData,
    LPQOS lpSQOS,
    LPQOS lpGQOS,
    LPWSABUF lpCalleeId,
    LPWSABUF lpCalleeData,
    GROUP FAR * g,
    DWORD dwCallbackData
    );

typedef
void
(CALLBACK * LPWSAOVERLAPPED_COMPLETION_ROUTINE)(
    DWORD dwError,
    DWORD cbTransferred,
    LPWSAOVERLAPPED lpOverlapped,
    DWORD dwFlags
    );

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    unsigned long Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
typedef GUID FAR * LPGUID;
#endif

#ifndef MAX_PROTOCOL_CHAIN
#define MAX_PROTOCOL_CHAIN 7
#define WSAPROTOCOL_LEN 255
#endif

typedef struct _WSAPROTOCOLCHAIN {
    int ChainLen;
    DWORD ChainEntries[MAX_PROTOCOL_CHAIN];
} WSAPROTOCOLCHAIN, FAR * LPWSAPROTOCOLCHAIN;

typedef struct _WSAPROTOCOL_INFOA {
    DWORD dwServiceFlags1;
    DWORD dwServiceFlags2;
    DWORD dwServiceFlags3;
    DWORD dwServiceFlags4;
    DWORD dwProviderFlags;
    GUID ProviderId;
    DWORD dwCatalogEntryId;
    WSAPROTOCOLCHAIN ProtocolChain;
    int iVersion;
    int iAddressFamily;
    int iMaxSockAddr;
    int iMinSockAddr;
    int iSocketType;
    int iProtocol;
    int iProtocolMaxOffset;
    int iNetworkByteOrder;
    int iSecurityScheme;
    DWORD dwMessageSize;
    DWORD dwProviderReserved;
    CHAR szProtocol[WSAPROTOCOL_LEN+1];
} WSAPROTOCOL_INFOA, FAR * LPWSAPROTOCOL_INFOA;

typedef struct _WSAPROTOCOL_INFOW {
    DWORD dwServiceFlags1;
    DWORD dwServiceFlags2;
    DWORD dwServiceFlags3;
    DWORD dwServiceFlags4;
    DWORD dwProviderFlags;
    GUID ProviderId;
    DWORD dwCatalogEntryId;
    WSAPROTOCOLCHAIN ProtocolChain;
    int iVersion;
    int iAddressFamily;
    int iMaxSockAddr;
    int iMinSockAddr;
    int iSocketType;
    int iProtocol;
    int iProtocolMaxOffset;
    int iNetworkByteOrder;
    int iSecurityScheme;
    DWORD dwMessageSize;
    DWORD dwProviderReserved;
    WCHAR szProtocol[WSAPROTOCOL_LEN+1];
} WSAPROTOCOL_INFOW, FAR * LPWSAPROTOCOL_INFOW;

typedef struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char FAR * ai_canonname;
    struct sockaddr FAR * ai_addr;
    struct addrinfo FAR * ai_next;
} ADDRINFOA, FAR * LPADDRINFOA;

typedef struct WSAPOLLFD {
    SOCKET fd;
    SHORT events;
    SHORT revents;
} WSAPOLLFD, FAR * LPWSAPOLLFD;

typedef struct _WSAMSG {
    LPSOCKADDR name;
    INT namelen;
    LPWSABUF lpBuffers;
    DWORD dwBufferCount;
    WSABUF Control;
    DWORD dwFlags;
} WSAMSG, FAR * LPWSAMSG;

/* Overlapped stubs use same struct */
typedef WSAOVERLAPPED WSAOVERLAPPED_STRUCT;

#ifdef __cplusplus
extern "C" {
#endif

int WSAAPI WINSOCK351_closesocket(SOCKET s);
int WSAAPI WINSOCK351_getsockopt(SOCKET s, int level, int optname, char* optval, int* optlen);
int WSAAPI WINSOCK351_ioctlsocket(SOCKET s, long cmd, u_long* argp);
int WSAAPI WINSOCK351_select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const struct timeval* timeout);
int WSAAPI WINSOCK351_setsockopt(SOCKET s, int level, int optname, const char* optval, int optlen);
BOOL WSAAPI WINSOCK351_WSACloseEvent(WSAEVENT hEvent);
WSAEVENT WSAAPI WINSOCK351_WSACreateEvent();
int WSAAPI WINSOCK351_WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents);
int WSAAPI WINSOCK351_WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents);
int WSAAPI WINSOCK351_WSAIoctl(
	SOCKET s,
	DWORD dwIoControlCode,
	LPVOID lpvInBuffer,
	DWORD cbInBuffer,
	LPVOID lpvOutBuffer,
	DWORD cbOutBuffer,
	LPDWORD lpcbBytesReturned,
	LPWSAOVERLAPPED lpOverlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
BOOL WSAAPI WINSOCK351_WSAResetEvent(WSAEVENT hEvent);
BOOL WSAAPI WINSOCK351_WSASetEvent(WSAEVENT hEvent);
int WSAAPI WINSOCK351_WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WSAAPI WINSOCK351_WSACleanup();
DWORD WSAAPI WINSOCK351_WSAWaitForMultipleEvents(
	DWORD cEvents,
	const WSAEVENT FAR * lphEvents,
	BOOL fWaitAll,
	DWORD dwTimeout,
	BOOL fAlertable);

/* Winsock2 exclusive: socket/connection */
SOCKET WSAAPI WINSOCK351_WSAAccept(SOCKET s, struct sockaddr FAR * addr, LPINT addrlen, LPCONDITIONPROC lpfnCondition, DWORD dwCallbackData);
SOCKET WSAAPI WINSOCK351_WSASocketA(int af, int type, int protocol, LPWSAPROTOCOL_INFOA lpProtocolInfo, GROUP g, DWORD dwFlags);
SOCKET WSAAPI WINSOCK351_WSASocketW(int af, int type, int protocol, LPWSAPROTOCOL_INFOW lpProtocolInfo, GROUP g, DWORD dwFlags);
int WSAAPI WINSOCK351_WSAConnect(SOCKET s, const struct sockaddr FAR * name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS);
int WSAAPI WINSOCK351_WSAConnectByList(SOCKET s, LPWSABUF Slice, LPDWORD SliceLength, LPWSABUF Local, LPDWORD LocalLength, LPWSABUF Remote, LPDWORD RemoteLength, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved);
int WSAAPI WINSOCK351_WSAConnectByNameA(SOCKET s, LPCSTR nodename, LPCSTR servicename, LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress, LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved);
int WSAAPI WINSOCK351_WSAConnectByNameW(SOCKET s, LPCWSTR nodename, LPCWSTR servicename, LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress, LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress, const struct timeval FAR * timeout, LPWSAOVERLAPPED Reserved);
int WSAAPI WINSOCK351_WSAJoinLeaf(SOCKET s, const struct sockaddr FAR * name, int namelen, LPWSABUF lpCallerData, LPWSABUF lpCalleeData, LPQOS lpSQOS, LPQOS lpGQOS, DWORD dwFlags);
int WSAAPI WINSOCK351_WSADuplicateSocketA(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOA lpProtocolInfo);
int WSAAPI WINSOCK351_WSADuplicateSocketW(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOW lpProtocolInfo);

/* Overlapped-style send/recv: blocking fallback, overlapped -> WSAEOPNOTSUPP */
int WSAAPI WINSOCK351_WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
int WSAAPI WINSOCK351_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd, LPDWORD lpFlags, struct sockaddr FAR * lpFrom, LPINT lpFromlen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
int WSAAPI WINSOCK351_WSARecvDisconnect(SOCKET s, LPWSABUF lpInboundDisconnectData);
int WSAAPI WINSOCK351_WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
int WSAAPI WINSOCK351_WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent, DWORD dwFlags, const struct sockaddr FAR * lpTo, int iTolen, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
int WSAAPI WINSOCK351_WSASendDisconnect(SOCKET s, LPWSABUF lpOutboundDisconnectData);
int WSAAPI WINSOCK351_WSASendMsg(SOCKET s, LPWSAMSG lpMsg, DWORD dwFlags, LPDWORD lpNumberOfBytesSent, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
BOOL WSAAPI WINSOCK351_WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED lpOverlapped, LPDWORD lpcbTransfer, BOOL fWait, LPDWORD lpdwFlags);

/* Byte-order helpers */
int WSAAPI WINSOCK351_WSAHtonl(SOCKET s, u_long hostlong, u_long FAR * lpnetlong);
int WSAAPI WINSOCK351_WSAHtons(SOCKET s, u_short hostshort, u_short FAR * lpnetshort);
int WSAAPI WINSOCK351_WSANtohl(SOCKET s, u_long netlong, u_long FAR * lphostlong);
int WSAAPI WINSOCK351_WSANtohs(SOCKET s, u_short netshort, u_short FAR * lphostshort);

/* Poll */
int WSAAPI WINSOCK351_WSAPoll(LPWSAPOLLFD fdarray, ULONG nfds, INT timeout);

/* Name resolution (IPv4 via wsock32) */
void WSAAPI WINSOCK351_freeaddrinfo(LPADDRINFOA pAddrInfo);
int WSAAPI WINSOCK351_getaddrinfo(const char FAR * pNodeName, const char FAR * pServiceName, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult);
int WSAAPI WINSOCK351_getnameinfo(const struct sockaddr FAR * sa, int salen, char FAR * host, DWORD hostlen, char FAR * serv, DWORD servlen, int flags);
void WSAAPI WINSOCK351_FreeAddrInfoW(LPADDRINFOA pAddrInfo);
int WSAAPI WINSOCK351_GetAddrInfoW(LPCWSTR pNodeName, LPCWSTR pServiceName, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult);
int WSAAPI WINSOCK351_GetNameInfoW(const struct sockaddr FAR * sa, int salen, LPWSTR host, DWORD hostlen, LPWSTR serv, DWORD servlen, int flags);
int WSAAPI WINSOCK351_GetHostNameW(LPWSTR name, int namelen);
void WSAAPI WINSOCK351_FreeAddrInfoEx(LPADDRINFOA pAddrInfo);
void WSAAPI WINSOCK351_FreeAddrInfoExW(LPADDRINFOA pAddrInfo);
int WSAAPI WINSOCK351_GetAddrInfoExA(const char FAR * pNodeName, const char FAR * pServiceName, DWORD dwNameSpace, LPGUID lpNspId, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult, LPVOID lpCallback, LPVOID lpContext, HANDLE FAR * handle);
int WSAAPI WINSOCK351_GetAddrInfoExW(LPCWSTR pNodeName, LPCWSTR pServiceName, DWORD dwNameSpace, LPGUID lpNspId, const LPADDRINFOA pHints, LPADDRINFOA FAR * ppResult, LPVOID lpCallback, LPVOID lpContext, HANDLE FAR * handle);
int WSAAPI WINSOCK351_GetAddrInfoExCancel(HANDLE FAR * handle);
int WSAAPI WINSOCK351_GetAddrInfoExOverlappedResult(LPVOID lpOverlapped);
int WSAAPI WINSOCK351_SetAddrInfoExA(const char FAR * pNodeName, const char FAR * pServiceName, const LPVOID pBlob, DWORD dwFlags, LPGUID lpNspId, DWORD dwTimeout, LPVOID lpContext, LPVOID lpCallback, HANDLE FAR * handle);
int WSAAPI WINSOCK351_SetAddrInfoExW(LPCWSTR pNodeName, LPCWSTR pServiceName, const LPVOID pBlob, DWORD dwFlags, LPGUID lpNspId, DWORD dwTimeout, LPVOID lpContext, LPVOID lpCallback, HANDLE FAR * handle);

/* String conversions (IPv4) */
INT WSAAPI WINSOCK351_WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSTR lpszAddressString, LPDWORD lpdwAddressStringLength);
INT WSAAPI WINSOCK351_WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength, LPWSAPROTOCOL_INFOW lpProtocolInfo, LPWSTR lpszAddressString, LPDWORD lpdwAddressStringLength);
INT WSAAPI WINSOCK351_WSAStringToAddressA(LPSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSOCKADDR lpAddress, LPINT lpAddressLength);
INT WSAAPI WINSOCK351_WSAStringToAddressW(LPWSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOW lpProtocolInfo, LPSOCKADDR lpAddress, LPINT lpAddressLength);
LPCSTR WSAAPI WINSOCK351_inet_ntop(INT Family, LPCVOID pAddr, LPSTR pStringBuf, size_t StringBufSize);
INT WSAAPI WINSOCK351_inet_pton(INT Family, LPCSTR pszAddrString, LPVOID pAddrBuf);
INT WSAAPI WINSOCK351_InetNtopW(INT Family, LPCVOID pAddr, LPWSTR pStringBuf, size_t StringBufSize);
INT WSAAPI WINSOCK351_InetPtonW(INT Family, LPCWSTR pszAddrString, LPVOID pAddrBuf);

/* Protocol / namespace / service-class database */
int WSAAPI WINSOCK351_WSAEnumProtocolsA(LPINT lpiProtocols, LPWSAPROTOCOL_INFOA lpProtocolBuffer, LPDWORD lpdwBufferLength);
int WSAAPI WINSOCK351_WSAEnumProtocolsW(LPINT lpiProtocols, LPWSAPROTOCOL_INFOW lpProtocolBuffer, LPDWORD lpdwBufferLength);
INT WSAAPI WINSOCK351_WSAEnumNameSpaceProvidersA(LPDWORD lpdwBufferLength, LPVOID lpnspBuffer);
INT WSAAPI WINSOCK351_WSAEnumNameSpaceProvidersW(LPDWORD lpdwBufferLength, LPVOID lpnspBuffer);
INT WSAAPI WINSOCK351_WSAGetQOSByName(SOCKET s, LPWSABUF lpQOSName, LPQOS lpQOS);
INT WSAAPI WINSOCK351_WSAGetServiceClassInfoA(LPGUID lpProviderId, LPGUID lpServiceClassId, LPDWORD lpdwBufSize, LPVOID lpServiceClassInfo);
INT WSAAPI WINSOCK351_WSAGetServiceClassInfoW(LPGUID lpProviderId, LPGUID lpServiceClassId, LPDWORD lpdwBufSize, LPVOID lpServiceClassInfo);
DWORD WSAAPI WINSOCK351_WSAGetServiceClassNameByClassIdA(LPGUID lpServiceClassId, LPSTR lpszServiceClassName, LPDWORD lpdwBufferLength);
DWORD WSAAPI WINSOCK351_WSAGetServiceClassNameByClassIdW(LPGUID lpServiceClassId, LPWSTR lpszServiceClassName, LPDWORD lpdwBufferLength);
INT WSAAPI WINSOCK351_WSAInstallServiceClassA(LPVOID lpServiceClassInfo);
INT WSAAPI WINSOCK351_WSAInstallServiceClassW(LPVOID lpServiceClassInfo);
INT WSAAPI WINSOCK351_WSARemoveServiceClass(LPGUID lpServiceClassId);
INT WSAAPI WINSOCK351_WSANSPIoctl(HANDLE hLookup, DWORD dwControlCode, LPVOID lpvInBuffer, DWORD cbInBuffer, LPVOID lpvOutBuffer, DWORD cbOutBuffer, LPDWORD lpcbBytesReturned, LPVOID lpCompletion);
INT WSAAPI WINSOCK351_WSALookupServiceBeginA(LPVOID lpqsRestrictions, DWORD dwControlFlags, LPHANDLE lphLookup);
INT WSAAPI WINSOCK351_WSALookupServiceBeginW(LPVOID lpqsRestrictions, DWORD dwControlFlags, LPHANDLE lphLookup);
INT WSAAPI WINSOCK351_WSALookupServiceEnd(HANDLE hLookup);
INT WSAAPI WINSOCK351_WSALookupServiceNextA(HANDLE hLookup, DWORD dwControlFlags, LPDWORD lpdwBufferLength, LPVOID lpqsResults);
INT WSAAPI WINSOCK351_WSALookupServiceNextW(HANDLE hLookup, DWORD dwControlFlags, LPDWORD lpdwBufferLength, LPVOID lpqsResults);
INT WSAAPI WINSOCK351_WSASetServiceA(LPVOID lpqsRegInfo, DWORD essoperation, DWORD dwControlFlags);
INT WSAAPI WINSOCK351_WSASetServiceW(LPVOID lpqsRegInfo, DWORD essoperation, DWORD dwControlFlags);
INT WSAAPI WINSOCK351_WSAProviderConfigChange(LPHANDLE lpNotificationHandle, LPVOID lpOverlapped, LPVOID lpCompletionRoutine);
int WSAAPI WINSOCK351_WSApSetPostRoutine(LPVOID lpPostRoutine);
int WSAAPI WINSOCK351_WPUCompleteOverlappedRequest(SOCKET s, LPWSAOVERLAPPED lpOverlapped, DWORD dwError, DWORD cbTransferred, LPVOID lpCompletionRoutine);

/* WSC / LSP stubs */
int WSAAPI WINSOCK351_WSCDeinstallProvider(LPGUID lpProviderId, LPINT lpNumberOfEntries);
int WSAAPI WINSOCK351_WSCEnableNSProvider(LPGUID lpProviderId, BOOL fEnable);
int WSAAPI WINSOCK351_WSCEnumProtocols(LPINT lpiProtocols, LPWSAPROTOCOL_INFOA lpProtocolBuffer, LPDWORD lpdwBufferLength, LPINT lpErrno);
int WSAAPI WINSOCK351_WSCGetProviderPath(LPGUID lpProviderId, LPSTR lpszProviderDllPath, LPINT lpProviderDllPathLen, LPINT lpErrno);
int WSAAPI WINSOCK351_WSCInstallNameSpace(LPSTR lpszIdentifier, LPSTR lpszPathName, DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId);
int WSAAPI WINSOCK351_WSCInstallProvider(LPGUID lpProviderId, const LPSTR lpszProviderDllPath, const LPVOID lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno);
int WSAAPI WINSOCK351_WSCUnInstallNameSpace(LPGUID lpProviderId);
int WSAAPI WINSOCK351_WSCUpdateProvider(LPGUID lpProviderId, const LPSTR lpszProviderDllPath, const LPVOID lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno);
int WSAAPI WINSOCK351_WSCWriteNameSpaceOrder(LPGUID lpProviderId, DWORD dwNumberOfEntries);
int WSAAPI WINSOCK351_WSCWriteProviderOrder(LPDWORD lpwdCatalogEntryId, DWORD dwNumberOfEntries);

#ifdef __cplusplus
}
#endif

#endif /* WINSOCK351_WINSOCK2_H */
