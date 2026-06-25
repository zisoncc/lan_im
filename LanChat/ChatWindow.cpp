#include "ChatWindow.h"
#include <sstream>
#include <commdlg.h>
#include <windowsx.h>
#include <time.h>
#include <map>
#include <cwctype>

static const wchar_t* CLASS_NAME = L"LanChatWindow";
enum { COL_MSG = 0 };
enum { ID_COPY_MESSAGE = 40001 };

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.c_str(), (int)text.size(), nullptr, 0);
    if (len <= 0)
    {
        len = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring fallback(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), fallback.data(), len);
        return fallback;
    }
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), len);
    return wide;
}

ChatWindow::ChatWindow()
    : m_hWnd(nullptr), m_hInst(nullptr), m_isServer(false)
    , m_hMsgList(nullptr), m_hInputEdit(nullptr), m_hSendBtn(nullptr)
    , m_hFileBtn(nullptr), m_hScanBtn(nullptr), m_hStatusText(nullptr)
    , m_hClientList(nullptr)
    , m_serverCore(nullptr), m_clientCore(nullptr), m_fileTransfer(nullptr)
    , m_running(false), m_discoveryDone(false)
    , m_fileProgressItem(-1)
{}

ChatWindow::~ChatWindow()
{
    if (m_serverCore) { m_serverCore->Stop(); delete m_serverCore; }
    if (m_clientCore) { m_clientCore->Disconnect(); m_clientCore->StopDiscovery(); delete m_clientCore; }
    if (m_fileTransfer) delete m_fileTransfer;
    if (m_hWnd) DestroyWindow(m_hWnd);
}

bool ChatWindow::Create(HINSTANCE hInstance, bool isServer, const std::string& serverName)
{
    m_hInst = hInstance; m_isServer = isServer; m_serverName = serverName;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);
    std::wstring title = isServer ? L"LanChat - \x670d\x52a1\x7aef" : L"LanChat - \x5ba2\x6237\x7aef";
    m_hWnd = CreateWindowExW(0, CLASS_NAME, title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 600,
        nullptr, nullptr, hInstance, this);
    if (!m_hWnd) return false;

    m_fileTransfer = new FileTransfer();
    if (isServer)
    {
        m_serverCore = new ServerCore();
        SetupServerCallbacks();
        if (!m_serverCore->Start(serverName))
        {
            MessageBoxA(m_hWnd, "\u542f\u52a8\u670d\u52a1\u7aef\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u7aef\u53e3",
                "\u9519\u8bef", MB_OK|MB_ICONERROR);
            return false;
        }
        AddMessage(L"\u2705 \u670d\u52a1\u7aef\u5df2\u542f\u52a8\uff0c\u7b49\u5f85\u5ba2\u6237\u7aef\u8fde\u63a5...", MessageAlign::Center);
        wchar_t buf[128];
        swprintf(buf, 128, L"\u672c\u673a IP: %hs | \u7aef\u53e3: %d",
            NetUtils::GetFirstLocalIP().c_str(), TCP_DATA_PORT);
        AddMessage(buf, MessageAlign::Center);
    }
    else
    {
        m_clientCore = new ClientCore();
        SetupClientCallbacks();
        m_clientCore->StartDiscovery();
        AddMessage(L"\u6b63\u5728\u626b\u63cf\u5c40\u57df\u7f51\u670d\u52a1\u7aef...", MessageAlign::Center);
    }
    return true;
}

void ChatWindow::Show(int nCmdShow) { ShowWindow(m_hWnd, nCmdShow); UpdateWindow(m_hWnd); }

LRESULT CALLBACK ChatWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ChatWindow* pThis = nullptr;
    if (msg == WM_CREATE)
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        pThis = (ChatWindow*)cs->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
        pThis->m_hWnd = hWnd;
        return pThis->OnCreate();
    }
    pThis = (ChatWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (!pThis) return DefWindowProc(hWnd, msg, wParam, lParam);
    switch (msg)
    {
    case WM_DESTROY:    return pThis->OnDestroy();
    case WM_SIZE:       return pThis->OnSize();
    case WM_COMMAND:    return pThis->OnCommand(wParam);
    case WM_CONTEXTMENU:
        if ((HWND)wParam == pThis->m_hMsgList)
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            pThis->ShowMessageContextMenu(pt);
            return 0;
        }
        break;
    case WM_NOTIFY:     return pThis->OnNotify(wParam, lParam);
    case WM_NET_MSG:    return pThis->OnNetMsg(wParam, lParam);
    case WM_FILE_PROGRESS: return pThis->OnFileProgress(wParam, lParam);
    case WM_TIMER:      return pThis->OnTimer(wParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT ChatWindow::OnCreate()
{
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH|FF_DONTCARE, L"\u5fae\u8f6f\u96c5\u9ed1");
    m_hStatusText = CreateWindowW(WC_STATICW, L"",
        WS_CHILD|WS_VISIBLE|SS_CENTERIMAGE|SS_LEFT, 0, 0, 800, 24,
        m_hWnd, nullptr, m_hInst, nullptr);
    SendMessage(m_hStatusText, WM_SETFONT, (WPARAM)hFont, FALSE);
    m_hMsgList = CreateWindowW(WC_LISTVIEWW, L"",
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOSORTHEADER|LVS_SHOWSELALWAYS
        |WS_BORDER|WS_TABSTOP,
        0, 28, 800, 420, m_hWnd, (HMENU)IDC_MSG_LIST, m_hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hMsgList, LVS_EX_FULLROWSELECT);
    LVCOLUMNW lvCol = {};
    lvCol.mask = LVCF_WIDTH|LVCF_TEXT|LVCF_FMT; lvCol.fmt = LVCFMT_LEFT;
    lvCol.cx = 780;
    lvCol.pszText = const_cast<LPWSTR>(L"\u6d88 \u606f");
    ListView_InsertColumn(m_hMsgList, COL_MSG, &lvCol);
    m_hClientList = CreateWindowW(WC_LISTVIEWW, L"",
        WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_NOSORTHEADER|WS_BORDER,
        600, 28, 200, 420, m_hWnd, (HMENU)IDC_CLIENT_LIST, m_hInst, nullptr);
    ListView_SetExtendedListViewStyle(m_hClientList, LVS_EX_FULLROWSELECT);
    LVCOLUMNW clCol = {}; clCol.mask = LVCF_WIDTH|LVCF_TEXT; clCol.cx = 190;
    clCol.pszText = const_cast<LPWSTR>(m_isServer
        ? L"\u5df2\u8fde\u63a5\u5ba2\u6237\u7aef"
        : L"\u53d1\u73b0\u7684\u670d\u52a1\u7aef");
    ListView_InsertColumn(m_hClientList, 0, &clCol);
    m_hInputEdit = CreateWindowW(WC_EDITW, L"",
        WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN
        |WS_BORDER|WS_TABSTOP,
        8, 460, 620, 60, m_hWnd, (HMENU)IDC_INPUT_EDIT, m_hInst, nullptr);
    SendMessage(m_hInputEdit, WM_SETFONT, (WPARAM)hFont, FALSE);
    m_hSendBtn = CreateWindowW(WC_BUTTONW, L"\u53d1\u9001",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,
        636, 460, 80, 28, m_hWnd, (HMENU)IDC_SEND_BTN, m_hInst, nullptr);
    SendMessage(m_hSendBtn, WM_SETFONT, (WPARAM)hFont, FALSE);
    m_hFileBtn = CreateWindowW(WC_BUTTONW, L"\u9009\u62e9\u6587\u4ef6",
        WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,
        636, 494, 80, 28, m_hWnd, (HMENU)IDC_FILE_BTN, m_hInst, nullptr);
    SendMessage(m_hFileBtn, WM_SETFONT, (WPARAM)hFont, FALSE);
    if (!m_isServer)
    {
        m_hScanBtn = CreateWindowW(WC_BUTTONW, L"\u91cd\u65b0\u626b\u63cf",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_TABSTOP,
            636, 528, 80, 28, m_hWnd, (HMENU)100, m_hInst, nullptr);
        SendMessage(m_hScanBtn, WM_SETFONT, (WPARAM)hFont, FALSE);
    }
    UpdateStatusBar();
    SetTimer(m_hWnd, 1, 2000, nullptr);
    return 0;
}

LRESULT ChatWindow::OnDestroy() { KillTimer(m_hWnd, 1); PostQuitMessage(0); return 0; }

LRESULT ChatWindow::OnSize()
{
    if (!m_hMsgList) return 0;
    RECT rc; GetClientRect(m_hWnd, &rc);
    int w = rc.right, h = rc.bottom;
    int listH = h - 28 - 110, rightW = 200, inputTop = h - 100;
    SetWindowPos(m_hStatusText, nullptr, 4, 2, w-8, 22, SWP_NOZORDER);
    SetWindowPos(m_hMsgList, nullptr, 4, 28, w-rightW-12, listH, SWP_NOZORDER);
    SetWindowPos(m_hClientList, nullptr, w-rightW-4, 28, rightW, listH, SWP_NOZORDER);
    SetWindowPos(m_hInputEdit, nullptr, 4, inputTop, w-rightW-16, 60, SWP_NOZORDER);
    SetWindowPos(m_hSendBtn, nullptr, w-rightW-8, inputTop, 80, 28, SWP_NOZORDER);
    SetWindowPos(m_hFileBtn, nullptr, w-rightW-8, inputTop+34, 80, 28, SWP_NOZORDER);
    if (m_hScanBtn && !m_isServer)
        SetWindowPos(m_hScanBtn, nullptr, w-rightW-8, inputTop+68, 80, 28, SWP_NOZORDER);
    return 0;
}

LRESULT ChatWindow::OnCommand(WPARAM wParam)
{
    WORD id = LOWORD(wParam), code = HIWORD(wParam);
    if (id == ID_COPY_MESSAGE)
    {
        CopySelectedMessageText();
        return 0;
    }
    if (code == BN_CLICKED)
    {
        if (id == IDC_SEND_BTN) { OnSendClick(); return 0; }
        if (id == IDC_FILE_BTN) { OnFileClick(); return 0; }
        if (id == 100 && !m_isServer && m_hScanBtn) { OnScanClick(); return 0; }
    }
    return 0;
}

LRESULT ChatWindow::OnNotify(WPARAM wParam, LPARAM lParam)
{
    LPNMHDR nmhdr = (LPNMHDR)lParam;
    if (nmhdr->idFrom == IDC_MSG_LIST && nmhdr->code == NM_CUSTOMDRAW)
    {
        NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
            return CDRF_NOTIFYITEMDRAW;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
        {
            int itemIndex = static_cast<int>(cd->nmcd.dwItemSpec);
            RECT rowRect;
            ListView_GetItemRect(m_hMsgList, itemIndex, &rowRect, LVIR_BOUNDS);

            HDC hdc = cd->nmcd.hdc;
            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
            FillRect(hdc, &rowRect, bg);
            DeleteObject(bg);

            wchar_t text[4096] = {};
            ListView_GetItemText(m_hMsgList, itemIndex, 0, text, 4096);

            MessageAlign align = MessageAlign::Left;
            if (itemIndex >= 0 && itemIndex < (int)m_messageAligns.size())
                align = m_messageAligns[itemIndex];

            RECT contentRect = rowRect;
            contentRect.left += 10;
            contentRect.right -= 10;
            contentRect.top += 2;
            contentRect.bottom -= 2;

            HFONT font = (HFONT)SendMessage(m_hMsgList, WM_GETFONT, 0, 0);
            HFONT oldFont = font ? (HFONT)SelectObject(hdc, font) : nullptr;
            SetBkMode(hdc, TRANSPARENT);

            SIZE textSize = {};
            GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &textSize);
            int maxBubbleW = max(180, (contentRect.right - contentRect.left) * 2 / 3);
            int bubbleW = min(maxBubbleW, textSize.cx + 28);

            RECT bubble = contentRect;
            if (align == MessageAlign::Right)
            {
                bubble.left = contentRect.right - bubbleW;
                bubble.right = contentRect.right;
                SetTextColor(hdc, RGB(24, 67, 120));
            }
            else if (align == MessageAlign::Center)
            {
                bubble.left = contentRect.left + ((contentRect.right - contentRect.left) - bubbleW) / 2;
                bubble.right = bubble.left + bubbleW;
                SetTextColor(hdc, RGB(105, 105, 105));
            }
            else
            {
                bubble.right = contentRect.left + bubbleW;
                SetTextColor(hdc, RGB(35, 35, 35));
            }

            COLORREF fillColor = align == MessageAlign::Right
                ? RGB(218, 236, 255)
                : (align == MessageAlign::Center ? RGB(242, 242, 242) : RGB(238, 238, 238));
            HBRUSH bubbleBrush = CreateSolidBrush(fillColor);
            bool selected = (ListView_GetItemState(m_hMsgList, itemIndex, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            HPEN bubblePen = CreatePen(PS_SOLID, selected ? 2 : 1,
                selected ? RGB(0, 120, 215) : fillColor);
            HGDIOBJ oldBrush = SelectObject(hdc, bubbleBrush);
            HGDIOBJ oldPen = SelectObject(hdc, bubblePen);
            RoundRect(hdc, bubble.left, bubble.top, bubble.right, bubble.bottom, 12, 12);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(bubblePen);
            DeleteObject(bubbleBrush);

            RECT textRect = bubble;
            textRect.left += 12;
            textRect.right -= 12;
            DrawTextW(hdc, text, -1, &textRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
                (align == MessageAlign::Right ? DT_RIGHT : (align == MessageAlign::Center ? DT_CENTER : DT_LEFT)));

            if (oldFont) SelectObject(hdc, oldFont);
            return CDRF_SKIPDEFAULT;
        }
    }

    if (nmhdr->idFrom == IDC_MSG_LIST && nmhdr->code == NM_RCLICK)
    {
        DWORD pos = GetMessagePos();
        POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
        ShowMessageContextMenu(pt);
        return 0;
    }

    if (nmhdr->idFrom == IDC_MSG_LIST && nmhdr->code == NM_DBLCLK)
    {
        CopySelectedMessageText();
        return 0;
    }

    if (nmhdr->idFrom == IDC_MSG_LIST && nmhdr->code == LVN_KEYDOWN)
    {
        NMLVKEYDOWN* kd = reinterpret_cast<NMLVKEYDOWN*>(lParam);
        if ((kd->wVKey == 'C' || kd->wVKey == 'c') && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            CopySelectedMessageText();
            return 0;
        }
    }

    if (!m_isServer && nmhdr->idFrom == IDC_CLIENT_LIST && nmhdr->code == NM_DBLCLK)
    {
        int sel = ListView_GetNextItem(m_hClientList, -1, LVNI_SELECTED);
        if (sel >= 0 && sel < (int)m_discoveredServers.size())
        {
            auto& server = m_discoveredServers[sel];
            m_clientCore->StartDiscovery();
            Sleep(200);
            m_clientCore->StopDiscovery();
        if (m_clientCore->ConnectToServer(server))
        {
            std::wstring wname = Utf8ToWide(server.name);
            wchar_t buf[256];
            swprintf(buf, 256, L"\u5df2\u8fde\u63a5\u5230\u670d\u52a1\u7aef: %s (%hs:%d)",
                wname.c_str(), server.ip.c_str(), server.port);
                AddMessage(buf, MessageAlign::Center);
            }
            else AddMessage(L"\u8fde\u63a5\u5931\u8d25", MessageAlign::Center);
        }
        return 0;
    }
    return 0;
}

void ChatWindow::ShowMessageContextMenu(POINT pt)
{
    int sel = ListView_GetNextItem(m_hMsgList, -1, LVNI_SELECTED);
    if (sel < 0) return;

    if (pt.x == -1 && pt.y == -1)
    {
        RECT rc = {};
        ListView_GetItemRect(m_hMsgList, sel, &rc, LVIR_BOUNDS);
        pt.x = rc.left + 20;
        pt.y = rc.top + 10;
        ClientToScreen(m_hMsgList, &pt);
    }

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_COPY_MESSAGE, L"\u590d\u5236\u6587\u5b57");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
    DestroyMenu(menu);
}

std::wstring ChatWindow::GetCopyableMessageText(int itemIndex) const
{
    if (itemIndex < 0) return L"";

    wchar_t raw[4096] = {};
    ListView_GetItemText(m_hMsgList, itemIndex, 0, raw, 4096);
    std::wstring text(raw);

    size_t start = 0;
    while (start < text.size() && iswspace(text[start])) start++;

    if (start < text.size() && text[start] == L'[')
    {
        size_t close = text.find(L"] ", start);
        if (close != std::wstring::npos)
            start = close + 2;
    }

    while (start < text.size() && iswspace(text[start])) start++;
    std::wstring result = text.substr(start);

    const std::wstring prefixes[] = { L"\u6211: ", L"\u670d\u52a1\u7aef: " };
    for (const auto& prefix : prefixes)
    {
        if (result.rfind(prefix, 0) == 0)
            return result.substr(prefix.size());
    }

    if (!result.empty() && result[0] == L'[')
    {
        size_t close = result.find(L"] ");
        if (close != std::wstring::npos)
            return result.substr(close + 2);
    }

    return result;
}

void ChatWindow::CopySelectedMessageText()
{
    int sel = ListView_GetNextItem(m_hMsgList, -1, LVNI_SELECTED);
    std::wstring text = GetCopyableMessageText(sel);
    if (text.empty()) return;

    if (!OpenClipboard(m_hWnd)) return;
    EmptyClipboard();

    SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem)
    {
        void* dest = GlobalLock(hMem);
        if (dest)
        {
            memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
            SetWindowTextW(m_hStatusText, L"\u5df2\u590d\u5236\u6d88\u606f");
        }
        else
        {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

LRESULT ChatWindow::OnTimer(WPARAM wParam)
{
    if (wParam == 1)
    {
        if (m_isServer && m_serverCore) UpdateClientList();
        UpdateStatusBar();
    }
    return 0;
}

LRESULT ChatWindow::OnNetMsg(WPARAM wParam, LPARAM lParam)
{
    uint32_t t = (uint32_t)wParam;
    wchar_t* txt = (wchar_t*)lParam;
    if (t == 0 && txt) { AddMessage(txt, MessageAlign::Left); delete[] txt; }
    else if (t == 1 && txt) { AddMessage(txt, MessageAlign::Center); delete[] txt; }
    else if (t == 2 && txt) { AddMessage(txt, MessageAlign::Center); delete[] txt; UpdateClientList(); }
    return 0;
}

LRESULT ChatWindow::OnFileProgress(WPARAM wParam, LPARAM lParam)
{
    FileProgressInfo* p = (FileProgressInfo*)wParam;
    if (p)
    {
        if (p->progress < 0)
        {
            std::wstring fileName = Utf8ToWide(p->fileName);
            wchar_t failed[512];
            swprintf(failed, 512, L"\u274c %s \u53d1\u9001\u5931\u8d25", fileName.c_str());
            AddMessage(failed, MessageAlign::Center);
            m_fileProgressItem = -1;
            m_fileProgressPrefix.clear();
            delete p;
            return 0;
        }

        // 格式: "发送/接收 1.mp4  10%"
        wchar_t direction = p->isSending ? L'\u53d1\u9001' : L'\u63a5\u6536';  // 发/收
        std::wstring fileName = Utf8ToWide(p->fileName);
        wchar_t buf[256];
        swprintf(buf, 256, L"\u2502 %c %s  %d%%",
            direction, fileName.c_str(), p->progress);

        if (p->progress < 100)
            UpdateFileProgress(buf, p->progress, p->isSending ? MessageAlign::Right : MessageAlign::Left);
        else
        {
            // 100% 完成：替换最后一行进度为完成消息
            wchar_t done[256];
            swprintf(done, 256, L"\u2714 %s \u4f20\u8f93\u5b8c\u6210", fileName.c_str());
            // 先更新进度到 100，再追加完成消息
            UpdateFileProgress(buf, 100, p->isSending ? MessageAlign::Right : MessageAlign::Left);
            AddMessage(done, MessageAlign::Center);
            m_fileProgressItem = -1;
            m_fileProgressPrefix.clear();
        }
        delete p;
    }
    return 0;
}

void ChatWindow::SetupServerCallbacks()
{
    if (!m_serverCore) return;
    m_serverCore->callbacks.onTextMessage = [this](const std::string& ip, const std::string& msg) {
        std::wstring wmsg = Utf8ToWide(msg);
        wchar_t buf[4096]; swprintf(buf, 4096, L"[%hs] %s", ip.c_str(), wmsg.c_str());
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 0, (LPARAM)c);
    };
    m_serverCore->callbacks.onClientConnected = [this](const std::string& ip, const std::string& name) {
        std::wstring wname = Utf8ToWide(name);
        wchar_t buf[256]; swprintf(buf, 256, L"\u5ba2\u6237\u7aef\u5df2\u8fde\u63a5: %s (%hs)", wname.c_str(), ip.c_str());
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 1, (LPARAM)c);
    };
    m_serverCore->callbacks.onClientDisconnected = [this](const std::string& ip) {
        wchar_t buf[256]; swprintf(buf, 256, L"\u5ba2\u6237\u7aef\u5df2\u65ad\u5f00: %hs", ip.c_str());
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 1, (LPARAM)c);
    };
    m_serverCore->callbacks.onFileMeta = [this](const std::string& ip, const FileMeta& meta) {
        m_fileTransfer->ReceiveFileStart(meta, [this](const FileProgressInfo& p) {
            PostMessage(m_hWnd, WM_FILE_PROGRESS, (WPARAM)(new FileProgressInfo(p)), 0);
        });
    };
    m_serverCore->callbacks.onFileData = [this](const std::string& ip, uint32_t id, uint32_t ci, const std::vector<uint8_t>& d) {
        m_fileTransfer->ReceiveFileData(id, ci, d);
    };
    m_serverCore->callbacks.onFileComplete = [this](const std::string& ip, uint32_t id) {
        m_fileTransfer->ReceiveFileComplete(id);
    };
    m_serverCore->callbacks.onFileCancel = [this](const std::string& ip, uint32_t id) {
        m_fileTransfer->CancelTransfer(id);
    };
}

void ChatWindow::SetupClientCallbacks()
{
    if (!m_clientCore) return;
    m_clientCore->callbacks.onServerDiscovered = [this](const ServerInfo& srv) {
        for (auto& s : m_discoveredServers)
            if (s.ip == srv.ip && s.port == srv.port) return;
        m_discoveredServers.push_back(srv);
        std::wstring wname = Utf8ToWide(srv.name);
        wchar_t buf[256];
        swprintf(buf, 256, L"\u53d1\u73b0\u670d\u52a1\u7aef: %s (%hs:%d)",
            wname.c_str(), srv.ip.c_str(), srv.port);
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 2, (LPARAM)c);
    };
    m_clientCore->callbacks.onConnected = [this]() {
        wchar_t buf[] = L"\u5df2\u8fde\u63a5";
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 1, (LPARAM)c);
    };
    m_clientCore->callbacks.onDisconnected = [this]() {
        wchar_t buf[] = L"\u8fde\u63a5\u5df2\u65ad\u5f00";
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 1, (LPARAM)c);
        m_clientCore->StartDiscovery();
    };
    m_clientCore->callbacks.onTextMessage = [this](const std::string& msg) {
        std::wstring wmsg = Utf8ToWide(msg);
        wchar_t buf[4096]; swprintf(buf, 4096, L"\u670d\u52a1\u7aef: %s", wmsg.c_str());
        size_t n = wcslen(buf)+1; wchar_t* c = new wchar_t[n]; wcscpy_s(c, n, buf);
        PostMessage(m_hWnd, WM_NET_MSG, 0, (LPARAM)c);
    };
    m_clientCore->callbacks.onFileMeta = [this](const FileMeta& meta) {
        m_fileTransfer->ReceiveFileStart(meta, [this](const FileProgressInfo& p) {
            PostMessage(m_hWnd, WM_FILE_PROGRESS, (WPARAM)(new FileProgressInfo(p)), 0);
        });
    };
    m_clientCore->callbacks.onFileData = [this](uint32_t id, uint32_t ci, const std::vector<uint8_t>& d) {
        m_fileTransfer->ReceiveFileData(id, ci, d);
    };
    m_clientCore->callbacks.onFileComplete = [this](uint32_t id) {
        m_fileTransfer->ReceiveFileComplete(id);
    };
    m_clientCore->callbacks.onFileCancel = [this](uint32_t id) {
        m_fileTransfer->CancelTransfer(id);
    };
}

void ChatWindow::AddMessage(const std::wstring& text, MessageAlign align)
{
    if (!m_hMsgList) return;
    time_t now = time(nullptr); tm t; localtime_s(&t, &now);
    wchar_t ts[16]; wcsftime(ts, 16, L"%H:%M:%S", &t);
    wchar_t full[4096];
    if (align == MessageAlign::Center)
        swprintf(full, 4096, L"    %s", text.c_str());
    else
        swprintf(full, 4096, L"[%s] %s", ts, text.c_str());
    LVITEMW item = {};
    item.mask = LVIF_TEXT; item.pszText = full;
    item.iItem = ListView_GetItemCount(m_hMsgList);
    ListView_InsertItem(m_hMsgList, &item);
    m_messageAligns.push_back(align);
    ListView_EnsureVisible(m_hMsgList, ListView_GetItemCount(m_hMsgList)-1, FALSE);
}

void ChatWindow::UpdateFileProgress(const std::wstring& text, int percent, MessageAlign align)
{
    if (!m_hMsgList) return;

    // 第一条进度消息 → 新增一个条目
    if (m_fileProgressItem < 0)
    {
        time_t now = time(nullptr); tm t; localtime_s(&t, &now);
        wchar_t ts[16]; wcsftime(ts, 16, L"%H:%M:%S", &t);
        wchar_t full[4096];
        swprintf(full, 4096, L"[%s] %s", ts, text.c_str());
        LVITEMW item = {};
        item.mask = LVIF_TEXT; item.pszText = full;
        item.iItem = ListView_GetItemCount(m_hMsgList);
        ListView_InsertItem(m_hMsgList, &item);
        m_fileProgressItem = item.iItem;
        m_messageAligns.push_back(align);
        ListView_EnsureVisible(m_hMsgList, item.iItem, FALSE);
    }
    else
    {
        // 已有进度条目 → 仅更新数字部分（保留时间戳）
        wchar_t existing[4096];
        ListView_GetItemText(m_hMsgList, m_fileProgressItem, 0, existing, 4096);

        // 保留时间戳部分 [HH:MM:SS] ，替换后面的内容
        wchar_t* bracket = wcschr(existing, L']');
        if (bracket)
        {
            wchar_t full[4096];
            swprintf(full, 4096, L"%s %s", existing, text.c_str() + 1);
            LVITEMW item = {};
            item.mask = LVIF_TEXT; item.pszText = full;
            item.iItem = m_fileProgressItem;
            ListView_SetItem(m_hMsgList, &item);
            ListView_EnsureVisible(m_hMsgList, m_fileProgressItem, FALSE);
        }
    }
}

void ChatWindow::UpdateStatusBar()
{
    if (!m_hStatusText) return;
    wchar_t buf[256];
    if (m_isServer && m_serverCore)
    {
        int n = (int)m_serverCore->GetClients().size();
        swprintf(buf, 256, L"\u670d\u52a1\u7aef\u8fd0\u884c\u4e2d | \u5df2\u8fde\u63a5\u5ba2\u6237\u7aef: %d \u53f0", n);
    }
    else if (!m_isServer && m_clientCore)
    {
        if (m_clientCore->IsConnected())
        {
            auto s = m_clientCore->GetConnectedServer();
            std::wstring wname = Utf8ToWide(s.name);
            swprintf(buf, 256, L"\u5df2\u8fde\u63a5\u5230 %s (%hs:%d)",
                wname.c_str(), s.ip.c_str(), s.port);
        }
        else
        {
            m_clientCore->StartDiscovery();
            swprintf(buf, 256, L"\u672a\u8fde\u63a5 | \u53d1\u73b0 %d \u4e2a\u670d\u52a1\u7aef (\u53cc\u51fb\u8fde\u63a5)",
                (int)m_discoveredServers.size());
        }
    }
    else swprintf(buf, 256, L"\u6b63\u5728\u521d\u59cb\u5316...");
    SetWindowTextW(m_hStatusText, buf);
}

void ChatWindow::UpdateClientList()
{
    if (!m_hClientList) return;
    ListView_DeleteAllItems(m_hClientList);
    if (m_isServer && m_serverCore)
    {
        auto cl = m_serverCore->GetClients();
        for (size_t i = 0; i < cl.size(); i++)
        {
            std::wstring wname = Utf8ToWide(cl[i].name);
            wchar_t buf[256];
            swprintf(buf, 256, L"%s (%hs)", wname.c_str(), cl[i].ip.c_str());
            LVITEMW item = {}; item.mask = LVIF_TEXT; item.pszText = buf; item.iItem = (int)i;
            ListView_InsertItem(m_hClientList, &item);
        }
    }
    else if (!m_isServer)
    {
        for (size_t i = 0; i < m_discoveredServers.size(); i++)
        {
            std::wstring wname = Utf8ToWide(m_discoveredServers[i].name);
            wchar_t buf[256];
            swprintf(buf, 256, L"%s (%hs)", wname.c_str(), m_discoveredServers[i].ip.c_str());
            LVITEMW item = {};
            item.mask = LVIF_TEXT|LVIF_PARAM; item.pszText = buf;
            item.iItem = (int)i; item.lParam = i;
            ListView_InsertItem(m_hClientList, &item);
        }
    }
}

void ChatWindow::OnSendClick()
{
    if (!m_hInputEdit) return;
    int len = GetWindowTextLengthW(m_hInputEdit);
    if (len == 0) return;
    std::wstring wtext(len+1, L'\0');
    GetWindowTextW(m_hInputEdit, &wtext[0], len+1); wtext.resize(len);
    SetWindowTextW(m_hInputEdit, L"");
    int u8len = WideCharToMultiByte(CP_UTF8, 0, wtext.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string text(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wtext.c_str(), -1, &text[0], u8len, nullptr, nullptr);
    text.resize(u8len-1);
    wchar_t local[4096]; swprintf(local, 4096, L"\u6211: %s", wtext.c_str());
    AddMessage(local, MessageAlign::Right);
    bool ok = false;
    if (m_isServer && m_serverCore) { m_serverCore->BroadcastText(text); ok = true; }
    else if (!m_isServer && m_clientCore) ok = m_clientCore->SendText(text);
    if (!ok) AddMessage(L"\u53d1\u9001\u5931\u8d25\uff1a\u672a\u8fde\u63a5\u5230\u4efb\u4f55\u5ba2\u6237\u7aef/\u670d\u52a1\u7aef", MessageAlign::Center);
}

void ChatWindow::OnFileClick()
{
    OPENFILENAMEW ofn = {};
    wchar_t filePath[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"\u9009\u62e9\u8981\u53d1\u9001\u7684\u6587\u4ef6";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return;

    int u8len = WideCharToMultiByte(CP_UTF8, 0, filePath, -1, nullptr, 0, nullptr, nullptr);
    std::string path(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, filePath, -1, &path[0], u8len, nullptr, nullptr);
    path.resize(u8len-1);
    size_t pos = path.find_last_of("\\/");
    std::string fname = (pos != std::string::npos) ? path.substr(pos+1) : path;

    // 重置进度条状态
    m_fileProgressItem = -1;
    m_fileProgressPrefix.clear();

    if (m_isServer && m_serverCore)
    {
        auto cl = m_serverCore->GetClients();
        if (cl.empty()) { AddMessage(L"\u6ca1\u6709\u5df2\u8fde\u63a5\u7684\u5ba2\u6237\u7aef", MessageAlign::Center); return; }
        for (const auto& client : cl)
        {
            m_fileTransfer->SendFile(fname, path, client.socket, [this](const FileProgressInfo& p) {
                PostMessage(m_hWnd, WM_FILE_PROGRESS, (WPARAM)(new FileProgressInfo(p)), 0);
            });
        }
    }
    else if (!m_isServer && m_clientCore && m_clientCore->IsConnected())
    {
        m_fileTransfer->SendFileDirect(fname, path, m_clientCore, [this](const FileProgressInfo& p) {
            PostMessage(m_hWnd, WM_FILE_PROGRESS, (WPARAM)(new FileProgressInfo(p)), 0);
        });
    }
    else AddMessage(L"\u672a\u8fde\u63a5\u5230\u670d\u52a1\u7aef", MessageAlign::Center);
}

void ChatWindow::OnScanClick()
{
    if (!m_isServer && m_clientCore)
    {
        m_discoveredServers.clear();
        ListView_DeleteAllItems(m_hClientList);
        m_clientCore->StartDiscovery();
        AddMessage(L"\u6b63\u5728\u91cd\u65b0\u626b\u63cf...", MessageAlign::Center);
    }
}

