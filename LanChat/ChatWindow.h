#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "Resource.h"
#include "ServerCore.h"
#include "ClientCore.h"
#include "FileTransfer.h"

enum class MessageAlign {
    Left,
    Right,
    Center
};

class ChatWindow {
public:
    ChatWindow();
    ~ChatWindow();
    bool Create(HINSTANCE hInstance, bool isServer, const std::string& serverName);
    void Show(int nCmdShow);
    HWND GetHandle() const { return m_hWnd; }
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
private:
    LRESULT OnCreate();
    LRESULT OnDestroy();
    LRESULT OnSize();
    LRESULT OnCommand(WPARAM wParam);
    LRESULT OnNetMsg(WPARAM wParam, LPARAM lParam);
    LRESULT OnFileProgress(WPARAM wParam, LPARAM lParam);
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam);
    LRESULT OnTimer(WPARAM wParam);
    void ShowMessageContextMenu(POINT pt);
    void CopySelectedMessageText();
    std::wstring GetCopyableMessageText(int itemIndex) const;
    void AddMessage(const std::wstring& text, MessageAlign align = MessageAlign::Left);
    void UpdateFileProgress(const std::wstring& text, int percent, MessageAlign align);
    void UpdateStatusBar();
    void UpdateClientList();
    void OnSendClick();
    void OnFileClick();
    void OnScanClick();
    void SetupServerCallbacks();
    void SetupClientCallbacks();

    HWND m_hWnd;
    HINSTANCE m_hInst;
    bool m_isServer;
    std::string m_serverName;
    HWND m_hMsgList;
    HWND m_hInputEdit;
    HWND m_hSendBtn;
    HWND m_hFileBtn;
    HWND m_hScanBtn;
    HWND m_hStatusText;
    HWND m_hClientList;
    ServerCore* m_serverCore;
    ClientCore* m_clientCore;
    FileTransfer* m_fileTransfer;
    bool m_running;
    bool m_discoveryDone;
    std::vector<ServerInfo> m_discoveredServers;
    std::vector<MessageAlign> m_messageAligns;
    int m_fileProgressItem;      // 文件进度条目的 ListView 索引, -1 表示无
    std::wstring m_fileProgressPrefix; // 当前文件名前缀, 用于匹配
};
