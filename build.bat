@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d D:\workspace\02_lan_im\LanChat
if not exist ..\bin mkdir ..\bin
echo === Compiling ===
cl /nologo /EHsc /std:c++17 /DUNICODE /D_UNICODE /I. main.cpp Protocol.cpp NetUtils.cpp ServerCore.cpp ClientCore.cpp FileTransfer.cpp ModeDialog.cpp ChatWindow.cpp /link /SUBSYSTEM:WINDOWS /OUT:..\bin\LanChat.exe user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ws2_32.lib iphlpapi.lib
echo Exit code: %ERRORLEVEL%
