#pragma once

// 图标
#define IDI_APP                         101

// 控件 ID
#define IDC_STATIC                      -1

#define IDC_MODE_SERVER                 1001
#define IDC_MODE_CLIENT                 1002
#define IDC_OK                          1003
#define IDC_CANCEL                      1004
#define IDC_SERVER_NAME                 1005

#define IDC_MSG_LIST                    2001
#define IDC_INPUT_EDIT                  2002
#define IDC_SEND_BTN                    2003
#define IDC_FILE_BTN                    2004
#define IDC_STATUS_TEXT                 2005
#define IDC_CLIENT_LIST                 2006

// 自定义消息
#define WM_NET_MSG                      (WM_USER + 100)
#define WM_NET_CONNECTED                (WM_USER + 101)
#define WM_NET_DISCONNECTED             (WM_USER + 102)
#define WM_NET_CLIENT_JOIN              (WM_USER + 103)
#define WM_NET_CLIENT_LEAVE             (WM_USER + 104)
#define WM_NET_SERVER_DISCOVERED        (WM_USER + 105)
#define WM_FILE_PROGRESS                (WM_USER + 106)
#define WM_FILE_COMPLETE                (WM_USER + 107)
#define WM_NET_CLIENT_LIST              (WM_USER + 108)
