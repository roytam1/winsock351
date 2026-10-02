# winsock351

This is a reimplementation of `ws2_32.dll` (Winsock 2) around `wsock32.dll` (Winsock 1.1) in order to support Winsock 2 applications under operating systems that don't support it, such as Windows NT 3.51.

## What's supported

The following is the list of Winsock 2 exclusive functions that are supported by this library. Not all functions may be listed, since there are functions which target interaction with the network drivers directly which isn't a priority:

- :heavy_check_mark: **Full support**
- :warning: **Partial support**
- :x: **Not supported yet**

| Ordinal | Function                        | Support            | Notes                                                    |
|---------|---------------------------------|--------------------|----------------------------------------------------------|
| 3       | `closesocket`                   | :heavy_check_mark: | Wrapper to clean up `WSAEventSelect` state               |
| 7       | `getsockopt`                    | :warning:          | May have Winsock2-exclusive opts                         |
| 10      | `ioctlsocket`                   | :warning:          | May have Winsock2-exclusive cmds                         |
| 21      | `setsockopt`                    | :warning:          | May have Winsock2-exclusive opts                         |
| 24      | `WSApSetPostRoutine`            | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 25      | `FreeAddrInfoEx`                | :heavy_check_mark: | Same as `freeaddrinfo`                                   |
| 26      | `FreeAddrInfoExW`               | :heavy_check_mark: | Same as `freeaddrinfo`                                   |
| 27      | `FreeAddrInfoW`                 | :heavy_check_mark: | Same as `freeaddrinfo`                                   |
| 28      | `GetAddrInfoExA`                | :warning:          | Sync path only, async returns `WSAEOPNOTSUPP`             |
| 29      | `GetAddrInfoExCancel`           | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 30      | `GetAddrInfoExOverlappedResult` | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 31      | `GetAddrInfoExW`                | :warning:          | Sync path only, async returns `WSAEOPNOTSUPP`             |
| 32      | `GetAddrInfoW`                  | :heavy_check_mark: | IPv4 via `gethostbyname`, no IPv6                        |
| 33      | `GetHostNameW`                  | :heavy_check_mark: | Thunk to `gethostname`                                   |
| 34      | `GetNameInfoW`                  | :heavy_check_mark: | IPv4 via `gethostbyaddr`/`getservbyport`                 |
| 35      | `InetNtopW`                     | :heavy_check_mark: | IPv4 only                                                |
| 36      | `InetPtonW`                     | :heavy_check_mark: | IPv4 only                                                |
| 37      | `SetAddrInfoExA`                | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 38      | `SetAddrInfoExW`                | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 41      | `WSAAccept`                     | :warning:          | Around `accept`, condition callback ignored              |
| 42      | `WSAAddressToStringA`           | :heavy_check_mark: | IPv4 only                                                |
| 43      | `WSAAddressToStringW`           | :heavy_check_mark: | IPv4 only                                                |
| 45      | `WSACloseEvent`                 | :heavy_check_mark: |                                                          |
| 46      | `WSAConnect`                    | :heavy_check_mark: | Around `connect`, QoS/caller data ignored                |
| 47      | `WSAConnectByList`              | :warning:          | Best-effort first-address `connect`                      |
| 48      | `WSAConnectByNameA`             | :heavy_check_mark: | Via `getaddrinfo` + `connect` loop                       |
| 49      | `WSAConnectByNameW`             | :heavy_check_mark: | Thunk to `WSAConnectByNameA`                             |
| 50      | `WSACreateEvent`                | :heavy_check_mark: |                                                          |
| 64      | `WSAEnumNetworkEvents`          | :heavy_check_mark: | Now copies `iErrorCode[]` and auto-resets                |
| 67      | `WSAEventSelect`                | :heavy_check_mark: | Implemented around `WSAAsyncSelect`                      |
| 74      | `WSAHtonl`                      | :heavy_check_mark: |                                                          |
| 75      | `WSAHtons`                      | :heavy_check_mark: |                                                          |
| 78      | `WSAIoctl`                      | :warning:          | Exported stub (`WSAEOPNOTSUPP`)                          |
| 86      | `WSANtohl`                      | :heavy_check_mark: |                                                          |
| 87      | `WSANtohs`                      | :heavy_check_mark: |                                                          |
| 88      | `WSAPoll`                       | :heavy_check_mark: | Emulated via `select`                                    |
| 91      | `WSARecv`                       | :warning:          | Blocking fallback, overlapped unsupported                |
| 92      | `WSARecvDisconnect`             | :heavy_check_mark: | Via `shutdown(SD_RECEIVE)`                                 |
| 93      | `WSARecvFrom`                   | :warning:          | Blocking fallback, overlapped unsupported                |
| 95      | `WSAResetEvent`                 | :heavy_check_mark: |                                                          |
| 96      | `WSASend`                       | :warning:          | Blocking fallback, overlapped unsupported                |
| 97      | `WSASendDisconnect`             | :heavy_check_mark: | Via `shutdown(SD_SEND)`                                    |
| 98      | `WSASendMsg`                    | :warning:          | Via `WSASendTo`, ancillary data ignored                  |
| 99      | `WSASendTo`                     | :warning:          | Blocking fallback, overlapped unsupported                |
| 100     | `WSASetEvent`                   | :heavy_check_mark: |                                                          |
| 117     | `WSAStringToAddressA`           | :heavy_check_mark: | IPv4 only                                                |
| 118     | `WSAStringToAddressW`           | :heavy_check_mark: | IPv4 only                                                |
| 119     | `WSASocketA`                    | :warning:          | Around `socket`, `lpProtocolInfo` partially used         |
| 120     | `WSASocketW`                    | :warning:          | Around `socket`, `lpProtocolInfo` partially used         |
| 124     | `WSAWaitForMultipleEvents`      | :heavy_check_mark: |                                                          |
| 175     | `freeaddrinfo`                  | :heavy_check_mark: | IPv4 via `gethostbyname`, no IPv6                        |
| 176     | `getaddrinfo`                   | :heavy_check_mark: | IPv4 via `gethostbyname`, no IPv6                        |
| 177     | `getnameinfo`                   | :heavy_check_mark: | IPv4 via `gethostbyaddr`/`getservbyport`                 |
| (none)  | `inet_ntop` / `inet_pton`       | :heavy_check_mark: | IPv4 only                                                |
| (none)  | `WSAEnumProtocolsA/W`           | :heavy_check_mark: | Hardcoded TCP+UDP over IPv4                              |
| (none)  | `WSAGetOverlappedResult` etc.   | :warning:          | Exported stubs (`WSAEOPNOTSUPP`/`WSASERVICE_NOT_FOUND`)  |
| (none)  | `WSC*` / `WPU*`                 | :warning:          | Exported stubs so apps load                               |

## Usage

In order to use this library, you can simply drop it on the same path as the target application. Alternatively if you want to install the library globally, you can drop it along the rest of the libraries on your system on `%WINDIR%\system32`.

## Build requirements

At the moment Visual C++ 6.0 is required to build the project, but Visual C++ 4.2 support may be considered in the future.

## Supported systems

`winsock351` has only been tested under Windows NT 3.51, but other systems such as early versions of Windows 95 may work too.

## Credits

This project has been partly inspired by [MattKC's backport of .NET to Windows 9x](https://github.com/itsmattkc/dotnet9x). The MSVC workflows have been adapted from there.
