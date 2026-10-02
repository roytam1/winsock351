cl /O2 /c tree234.c
cl /O2 /c main.cpp
cl /O2 /c winsock2.cpp
cl /O2 /c events.cpp
link /dll /def:ws2_32.def /out:ws2_32.dll main.obj events.obj winsock2.obj tree234.obj wsock32.lib user32.lib 
