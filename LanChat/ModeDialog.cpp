#include "ModeDialog.h"
#include "Resource.h"

ModeResult ModeDialog::s_result;

static const wchar_t* DLG_CLASS = L"LanChatModeDlg";

ModeResult ModeDialog::Show(HINSTANCE hInstance, HWND hParent)
{
    s_result = { false, "", true };

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = DLG_CLASS;
    RegisterClassExW(&wc);

    HWND hDlg = CreateWindowExW(0, DLG_CLASS, L"LanChat - \u9009\u62e9\u8fd0\u884c\u6a21\u5f0f",
        WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 220,
        hParent, nullptr, hInstance, nullptr);
    if (!hDlg) return s_result;

    RECT rc; GetWindowRect(hDlg, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hDlg, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"\u5fae\u8f6f\u96c5\u9ed1");

    CreateWindowExW(0, L"Button", L"\u8fd0\u884c\u6a21\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 8, 280, 70, hDlg, nullptr, hInstance, nullptr);
    HWND hSrv = CreateWindowExW(0, L"Button", L"\u670d\u52a1\u7aef\u6a21\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP,
        20, 24, 260, 20, hDlg, (HMENU)IDC_MODE_SERVER, hInstance, nullptr);
    SendMessage(hSrv, WM_SETFONT, (WPARAM)hFont, FALSE);
    SendMessage(hSrv, BM_SETCHECK, BST_CHECKED, 0);

    HWND hCli = CreateWindowExW(0, L"Button", L"\u5ba2\u6237\u7aef\u6a21\u5f0f",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_TABSTOP,
        20, 48, 260, 20, hDlg, (HMENU)IDC_MODE_CLIENT, hInstance, nullptr);
    SendMessage(hCli, WM_SETFONT, (WPARAM)hFont, FALSE);

    CreateWindowExW(0, L"Button", L"\u670d\u52a1\u7aef\u8bbe\u7f6e",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 85, 280, 50, hDlg, nullptr, hInstance, nullptr);
    CreateWindowExW(0, L"Static", L"\u670d\u52a1\u5668\u540d\u79f0:",
        WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 20, 103, 75, 18, hDlg, nullptr, hInstance, nullptr);

    char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameLen = sizeof(computerName);
    GetComputerNameA(computerName, &nameLen);
    wchar_t wName[64]; MultiByteToWideChar(CP_ACP, 0, computerName, -1, wName, 64);

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", wName,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        100, 102, 175, 20, hDlg, (HMENU)IDC_SERVER_NAME, hInstance, nullptr);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, FALSE);

    HWND hOk = CreateWindowExW(0, L"Button", L"\u786e\u5b9a(&O)",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        100, 145, 65, 25, hDlg, (HMENU)IDC_OK, hInstance, nullptr);
    SendMessage(hOk, WM_SETFONT, (WPARAM)hFont, FALSE);

    HWND hCancel = CreateWindowExW(0, L"Button", L"\u53d6\u6d88(&C)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        175, 145, 65, 25, hDlg, (HMENU)IDC_CANCEL, hInstance, nullptr);
    SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, FALSE);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);
    MSG msg;
    while (IsWindow(hDlg) && GetMessage(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessage(hDlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DeleteObject(hFont);
    return s_result;
}

INT_PTR CALLBACK ModeDialog::DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_MODE_SERVER:
            EnableWindow(GetDlgItem(hDlg, IDC_SERVER_NAME), TRUE);
            return TRUE;

        case IDC_MODE_CLIENT:
            EnableWindow(GetDlgItem(hDlg, IDC_SERVER_NAME), FALSE);
            return TRUE;

        case IDC_OK:
        {
            bool isServer = (SendMessage(GetDlgItem(hDlg, IDC_MODE_SERVER), BM_GETCHECK, 0, 0) == BST_CHECKED);
            std::string name;
            if (isServer)
            {
                wchar_t buf[256] = {};
                GetWindowTextW(GetDlgItem(hDlg, IDC_SERVER_NAME), buf, 256);
                int mbLen = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
                std::string mb(mbLen, '\0');
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, &mb[0], mbLen, nullptr, nullptr);
                mb.resize(mbLen - 1);
                name = mb;
                if (name.empty())
                {
                    MessageBoxW(hDlg, L"\u8bf7\u8f93\u5165\u670d\u52a1\u5668\u540d\u79f0", L"\u63d0\u793a", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
            }
            s_result.isServer = isServer;
            s_result.serverName = name;
            s_result.cancelled = false;
            DestroyWindow(hDlg);
            return TRUE;
        }

        case IDC_CANCEL:
            s_result.cancelled = true;
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        s_result.cancelled = true;
        DestroyWindow(hDlg);
        return TRUE;
    }
    return DefWindowProc(hDlg, msg, wParam, lParam);
}
