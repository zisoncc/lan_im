#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "NetUtils.h"
#include "ModeDialog.h"
#include "ChatWindow.h"
#include "Resource.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    if (!NetUtils::Initialize())
    {
        MessageBoxA(nullptr, "WinSock 初始化失败", "错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    ModeResult mode = ModeDialog::Show(hInstance, nullptr);
    if (mode.cancelled)
    {
        NetUtils::Cleanup();
        return 0;
    }

    ChatWindow* chatWnd = new ChatWindow();
    if (!chatWnd->Create(hInstance, mode.isServer, mode.serverName))
    {
        MessageBoxA(nullptr, "创建窗口失败", "错误", MB_OK | MB_ICONERROR);
        delete chatWnd;
        NetUtils::Cleanup();
        return 1;
    }

    chatWnd->Show(nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete chatWnd;
    NetUtils::Cleanup();
    return 0;
}
