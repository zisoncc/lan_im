#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

struct ModeResult {
    bool    isServer;
    std::string serverName;
    bool    cancelled;
};

class ModeDialog {
public:
    static ModeResult Show(HINSTANCE hInstance, HWND hParent);
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
private:
    static ModeResult s_result;
};
