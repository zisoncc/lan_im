#pragma once
#include <winsock2.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include "Protocol.h"
#include "NetUtils.h"

// 发现的服务端信息
struct ServerInfo {
    std::string name;
    std::string ip;
    uint16_t    port;
};

// 客户端事件回调
struct ClientCallbacks {
    std::function<void(const ServerInfo& server)> onServerDiscovered;
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const std::string& message)> onTextMessage;
    std::function<void(const FileMeta& meta)> onFileMeta;
    std::function<void(uint32_t transferId, uint32_t chunkIndex, const std::vector<uint8_t>& data)> onFileData;
    std::function<void(uint32_t transferId)> onFileComplete;
    std::function<void(uint32_t transferId)> onFileCancel;
};

class ClientCore {
public:
    ClientCore();
    ~ClientCore();

    // 开始局域网扫描 (后台线程持续扫描)
    void StartDiscovery();
    void StopDiscovery();

    // 连接到指定的服务端
    bool ConnectToServer(const ServerInfo& server);

    // 断开连接
    void Disconnect();

    // 发送文本
    bool SendText(const std::string& text);

    // 发送数据
    bool SendData(const std::vector<uint8_t>& data);

    // 发送客户端名称
    bool SendClientName(const std::string& name);

    // 是否已连接
    bool IsConnected() const;

    // 获取当前连接的服务端信息
    ServerInfo GetConnectedServer() const { return m_connectedServer; }

    // 设置/获取客户端名称
    void SetClientName(const std::string& name) { m_clientName = name; }
    std::string GetClientName() const { return m_clientName; }

    // 回调
    ClientCallbacks callbacks;

private:
    void DiscoveryThread();
    void RecvThread();

    bool m_discovering;
    bool m_running;

    SOCKET m_udpSock;
    SOCKET m_tcpSock;

    std::thread m_discoveryThread;
    std::thread m_recvThread;

    mutable std::mutex m_mutex;

    ServerInfo m_connectedServer;
    std::string m_clientName;
};
