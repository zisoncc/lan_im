#include "ClientCore.h"
#include <sstream>

ClientCore::ClientCore()
    : m_discovering(false)
    , m_running(false)
    , m_udpSock(INVALID_SOCKET)
    , m_tcpSock(INVALID_SOCKET)
{
}

ClientCore::~ClientCore()
{
    Disconnect();
    StopDiscovery();
}

// -------------------- 发现 --------------------
void ClientCore::StartDiscovery()
{
    if (m_discovering) return;
    m_discovering = true;
    m_discoveryThread = std::thread(&ClientCore::DiscoveryThread, this);
}

void ClientCore::StopDiscovery()
{
    m_discovering = false;
    NetUtils::CloseSocket(m_udpSock);
    if (m_discoveryThread.joinable())
        m_discoveryThread.join();
}

void ClientCore::DiscoveryThread()
{
    m_udpSock = NetUtils::CreateUdpBroadcastSocket(0);
    if (m_udpSock == INVALID_SOCKET) return;

    // 设置广播目标地址
    sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(UDP_DISCOVER_PORT);
    inet_pton(AF_INET, "255.255.255.255", &broadcastAddr.sin_addr);

    char recvBuf[512];

    while (m_discovering)
    {
        // 发送发现请求
        sendto(m_udpSock, DISCOVER_REQ_STR, (int)strlen(DISCOVER_REQ_STR), 0,
               (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

        // 等待响应 (500ms 超时)
        auto startTime = GetTickCount64();
        while (m_discovering && GetTickCount64() - startTime < 1000)
        {
            FD_SET fdSet;
            FD_ZERO(&fdSet);
            FD_SET(m_udpSock, &fdSet);
            timeval tv = {0, 200000}; // 200ms

            int ret = select(0, &fdSet, nullptr, nullptr, &tv);
            if (ret <= 0) continue;

            sockaddr_in fromAddr;
            int addrLen = sizeof(fromAddr);
            int len = recvfrom(m_udpSock, recvBuf, sizeof(recvBuf) - 1, 0,
                              (sockaddr*)&fromAddr, &addrLen);
            if (len <= 0) continue;
            recvBuf[len] = '\0';

            // 解析响应: "LANCHAT_DISCOVER_RSP|name|ip|port"
            const char* prefix = DISCOVER_RSP_PREFIX;
            if (strncmp(recvBuf, prefix, strlen(prefix)) == 0)
            {
                char* p = recvBuf + strlen(prefix);
                char* nameEnd = strchr(p, '|');
                if (!nameEnd) continue;
                *nameEnd = '\0';
                std::string name = p;

                p = nameEnd + 1;
                char* ipEnd = strchr(p, '|');
                if (!ipEnd) continue;
                *ipEnd = '\0';
                std::string ip = p;

                p = ipEnd + 1;
                uint16_t port = static_cast<uint16_t>(std::stoul(p));

                ServerInfo info;
                info.name = name;
                info.ip = ip;
                info.port = port;

                if (callbacks.onServerDiscovered)
                    callbacks.onServerDiscovered(info);
            }
        }

        // 每隔 3 秒扫描一次
        int sleepCount = 0;
        while (m_discovering && sleepCount < 30)
        {
            Sleep(100);
            sleepCount++;
        }
    }
}

// -------------------- 连接 --------------------
bool ClientCore::ConnectToServer(const ServerInfo& server)
{
    Disconnect();

    SOCKET sock = NetUtils::ConnectToServer(server.ip, server.port);
    if (sock == INVALID_SOCKET) return false;

    m_tcpSock = sock;
    m_connectedServer = server;

    // 发送客户端名称
    if (!m_clientName.empty())
    {
        auto data = Protocol::EncodeClientName(m_clientName);
        NetUtils::SendAll(m_tcpSock, data.data(), (int)data.size());
    }

    // 启动接收线程
    m_running = true;
    m_recvThread = std::thread(&ClientCore::RecvThread, this);

    if (callbacks.onConnected)
        callbacks.onConnected();

    return true;
}

void ClientCore::Disconnect()
{
    m_running = false;
    NetUtils::CloseSocket(m_tcpSock);
    if (m_recvThread.joinable())
        m_recvThread.join();
}

// -------------------- 接收线程 --------------------
void ClientCore::RecvThread()
{
    while (m_running)
    {
        // 接收头部
        uint8_t headerBuf[sizeof(PacketHeader)];
        if (!NetUtils::RecvAll(m_tcpSock, headerBuf, sizeof(PacketHeader)))
            break;

        PacketHeader* hdr = reinterpret_cast<PacketHeader*>(headerBuf);
        uint32_t payloadLen = ntohl(hdr->payloadLength);

        // 接收负载
        std::vector<uint8_t> payload(payloadLen);
        if (payloadLen > 0 && !NetUtils::RecvAll(m_tcpSock, payload.data(), payloadLen))
            break;

        MessageType type = static_cast<MessageType>(hdr->type);

        switch (type)
        {
        case MessageType::HEARTBEAT:
            break;

        case MessageType::TEXT_MSG:
        {
            std::string text(payload.begin(), payload.end());
            if (callbacks.onTextMessage)
                callbacks.onTextMessage(text);
            break;
        }

        case MessageType::FILE_META:
        {
            std::string metaStr(payload.begin(), payload.end());
            FileMeta meta = {};
            size_t pos1 = metaStr.find('|');
            size_t pos2 = metaStr.find('|', pos1 + 1);
            size_t pos3 = metaStr.find('|', pos2 + 1);
            if (pos1 != std::string::npos && pos2 != std::string::npos && pos3 != std::string::npos)
            {
                meta.fileName = metaStr.substr(0, pos1);
                meta.fileSize = std::stoull(metaStr.substr(pos1 + 1, pos2 - pos1 - 1));
                meta.transferId = std::stoul(metaStr.substr(pos2 + 1, pos3 - pos2 - 1));
                meta.totalChunks = std::stoul(metaStr.substr(pos3 + 1));
            }
            if (callbacks.onFileMeta)
                callbacks.onFileMeta(meta);
            break;
        }

        case MessageType::FILE_DATA:
        {
            if (payload.size() >= 12)
            {
                uint32_t transferId = Protocol::ReadU32(payload.data());
                uint32_t chunkIndex = Protocol::ReadU32(payload.data() + 4);
                uint32_t totalChunks = Protocol::ReadU32(payload.data() + 8);
                std::vector<uint8_t> chunkData(payload.begin() + 12, payload.end());
                if (callbacks.onFileData)
                    callbacks.onFileData(transferId, chunkIndex, chunkData);
            }
            break;
        }

        case MessageType::FILE_COMPLETE:
        {
            if (payload.size() >= 4)
            {
                uint32_t transferId = Protocol::ReadU32(payload.data());
                if (callbacks.onFileComplete)
                    callbacks.onFileComplete(transferId);
            }
            break;
        }

        case MessageType::FILE_CANCEL:
        {
            if (payload.size() >= 4)
            {
                uint32_t transferId = Protocol::ReadU32(payload.data());
                if (callbacks.onFileCancel)
                    callbacks.onFileCancel(transferId);
            }
            break;
        }
        }
    }

    // 连接断开
    NetUtils::CloseSocket(m_tcpSock);
    if (callbacks.onDisconnected)
        callbacks.onDisconnected();
}

// -------------------- 发送 --------------------
bool ClientCore::SendText(const std::string& text)
{
    if (m_tcpSock == INVALID_SOCKET) return false;
    auto data = Protocol::EncodeText(text);
    return NetUtils::SendAll(m_tcpSock, data.data(), (int)data.size());
}

bool ClientCore::SendData(const std::vector<uint8_t>& data)
{
    if (m_tcpSock == INVALID_SOCKET) return false;
    return NetUtils::SendAll(m_tcpSock, data.data(), (int)data.size());
}

bool ClientCore::SendClientName(const std::string& name)
{
    if (m_tcpSock == INVALID_SOCKET) return false;
    auto data = Protocol::EncodeClientName(name);
    return NetUtils::SendAll(m_tcpSock, data.data(), (int)data.size());
}

bool ClientCore::IsConnected() const
{
    return m_tcpSock != INVALID_SOCKET;
}
