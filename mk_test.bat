@echo off
rem mk_test.bat: build WSAEventSelect / WSAEnumNetworkEvents tests (VC4/VC6)
rem Dynamic test (for NT 3.51, loads local ws2_32.dll):
cl /O2 /nologo test_events.cpp user32.lib kernel32.lib /Fetest_events.exe
rem Static test (links objs directly, runs on modern Windows too):
cl /O2 /nologo /DTEST_STATIC_LINK /Fetest_events_static.exe test_events.cpp events.obj winsock2.obj tree234.obj wsock32.lib user32.lib kernel32.lib
