# LanChat — 局域网即时通讯

<div align="center">

![platform](https://img.shields.io/badge/platform-Windows%20%7C%20Android-blue)
![protocol](https://img.shields.io/badge/protocol-UDP%20Discovery%20%2B%20TCP%20Data-green)
![license](https://img.shields.io/badge/license-MIT-lightgrey)

**无需服务器、无需互联网，零配置的跨平台局域网聊天 & 文件传输工具**

[📱 下载 APK](#-下载) · [🖥️ 下载 Windows 客户端](#-下载) · [📖 功能](#-功能) · [🔧 构建](#-从源码构建)

</div>

---

## 🚀 功能

### 核心功能
- **零配置** — 打开即用，自动发现局域网内的其他用户
- **文字聊天** — 实时发送和接收文本消息
- **文件传输** — 支持任意格式文件的高速传输，大文件分块传输
- **服务器-客户端模式** — 一端创建会话，其他设备加入即可聊天

### 技术亮点
| 特性 | 说明 |
|------|------|
| 📡 **UDP 自动发现** | 基于 UDP 广播，自动发现局域网中的服务端（端口 `54320`） |
| 🔗 **TCP 可靠传输** | 建立 TCP 长连接传输消息和文件（端口 `54321`） |
| 💓 **心跳保活** | 5 秒间隔心跳，自动检测客户端离线 |
| 📦 **文件分块** | 大文件自动分块传输（64KB/块），支持进度显示和取消 |
| 🔄 **跨平台协议** | Windows 和 Android 使用同一套二进制协议，完全互通 |

---

## 📱 支持平台

| 平台 | 语言/框架 | 状态 |
|------|-----------|------|
| 🪟 Windows | C++ / Win32 API | ✅ 稳定 |
| 🤖 Android | Kotlin / Jetpack Compose | ✅ 稳定 |
| 🍎 iOS | — | ❌ 待开发 |
| 🐧 Linux | — | ❌ 待开发 |

---

## 📦 下载

### 预编译二进制

| 平台 | 文件 | 下载 |
|------|------|------|
| 🤖 Android APK | `app-release.apk`（~10 MB） | [⬇️ 下载 APK](LanChatAndroid/app/build/outputs/apk/release/app-release.apk) |
| 🪟 Windows EXE | `LanChat.exe`（~2 MB） | [⬇️ 下载 Windows 版](bin/LanChat.exe) |

### 安装说明

**Android**: 下载 APK 后直接安装。如果提示"未知来源应用"，请在设置中允许安装。

**Windows**: 下载 `LanChat.exe` 直接双击运行，无需安装。

> ℹ️ 也可通过 [Releases 页面](https://github.com/zisoncc/lan_im/releases) 下载历史版本。

---

## 🔧 从源码构建

### Windows 客户端

需要 **Visual Studio 2022**（包含 C++ 桌面开发工作负载）。

```bash
# 直接运行构建脚本
build.bat
```

输出在 `bin/LanChat.exe`。

### Android APK

项目自带 JDK 17、Android SDK 34 和 Gradle 8.4，无需额外安装环境。

```powershell
# 构建 Debug 版
.\LanChatAndroid\build-android-debug.ps1

# 构建 Release 版（已签名）
.\LanChatAndroid\build-android-release.ps1
```

输出 APK 在 `LanChatAndroid\app\build\outputs\apk\release\app-release.apk`。

---

## 🏗️ 项目结构

```
LanChat/
├── LanChat/                  # Windows 桌面客户端 (C++/Win32)
│   ├── main.cpp              # 入口
│   ├── Protocol.cpp/h        # 网络协议（与 Android 端互通）
│   ├── NetUtils.cpp/h        # 网络工具（Winsock 封装）
│   ├── ServerCore.cpp/h      # 服务器核心
│   ├── ClientCore.cpp/h      # 客户端核心
│   ├── FileTransfer.cpp/h    # 文件传输
│   ├── ModeDialog.cpp/h      # 模式选择对话框
│   ├── ChatWindow.cpp/h      # 主聊天窗口
│   └── resource.rc           # 资源文件
│
├── LanChatAndroid/           # Android 客户端 (Kotlin + Compose)
│   ├── app/src/main/java/com/lanchat/
│   │   ├── MainActivity.kt           # 主 Activity
│   │   ├── Protocol.kt               # 协议解析
│   │   ├── DiscoveryClient.kt        # UDP 服务发现
│   │   ├── TcpClient.kt / TcpServer.kt # TCP 通信
│   │   ├── FileTransferManager.kt    # 文件传输
│   │   ├── ChatMessage.kt            # 消息模型
│   │   └── ui/                       # Compose UI 界面
│   ├── app/build/outputs/apk/        # 编译产物
│   └── .build-tools/                 # 独立构建工具链
│
├── bin/LanChat.exe           # Windows 编译产物
├── build.bat                 # Windows 构建脚本
└── LanChat.sln               # Visual Studio 解决方案
```

---

## 🌐 网络协议

两台设备通过以下方式通信：

1. **UDP 广播发现**（端口 `54320`）
   - 客户端发送广播 `LANCHAT_DISCOVER_REQ`
   - 服务端响应 `LANCHAT_DISCOVER_RSP|服务器名称`

2. **TCP 数据传输**（端口 `54321`）
   - 所有消息和文件通过 TCP 长连接传输
   - 二进制协议：`[payload长度(4B)][消息类型(1B)][payload]`

| 消息类型 | 值 | 说明 |
|---------|----|------|
| HEARTBEAT | `0x01` | 心跳包，5 秒间隔 |
| TEXT_MSG | `0x02` | 文本消息 |
| FILE_META | `0x03` | 文件元信息（文件名、大小、分块数） |
| FILE_ACK | `0x04` | 文件接收确认 |
| FILE_DATA | `0x05` | 文件数据块（64KB/块） |
| FILE_COMPLETE | `0x06` | 文件传输完成 |
| FILE_CANCEL | `0x07` | 取消文件传输 |
| CLIENT_NAME | `0x08` | 客户端名称宣告 |

---

## 📄 许可证

本项目基于 MIT 许可证开源。

---

<div align="center">
Made with ❤️ for LAN party
</div>
