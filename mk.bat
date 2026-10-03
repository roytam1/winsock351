rem set WS351CFLAGS=/O2 /D_DEBUG
set WS351CFLAGS=/Ox
cl %WS351CFLAGS% /c tree234.c
cl %WS351CFLAGS% /c main.cpp
cl %WS351CFLAGS% /c winsock2.cpp
cl %WS351CFLAGS% /c events.cpp
link /dll /def:ws2_32.def /out:ws2_32.dll main.obj events.obj winsock2.obj tree234.obj wsock32.lib user32.lib 
