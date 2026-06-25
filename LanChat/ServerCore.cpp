#include "ServerCore.h"
#include <sstream>
#include <algorithm>

ServerCore::ServerCore() : m_running(false), m_udpSock(INVALID_SOCKET), m_tcpListenSock(INVALID_SOCKET) {}
ServerCore::~ServerCore() { Stop(); }

bool ServerCore::Start(const std::string& serverName)
{
    m_serverName = serverName;
    m_running = true;
    m_udpSock = NetUtils::CreateUdpListenSocket(UDP_DISCOVER_PORT);
    if (m_udpSock == INVALID_SOCKET) { m_running = false; return false; }
    m_tcpListenSock = NetUtils::CreateTcpListenSocket(TCP_DATA_PORT);
    if (m_tcpListenSock == INVALID_SOCKET)
    {
        NetUtils::CloseSocket(m_udpSock); m_udpSock = INVALID_SOCKET;
        m_running = false; return false;
    }
    m_udpThread = std::thread(&ServerCore::UdpDiscoveryThread, this);
    m_acceptThread = std::thread(&ServerCore::TcpAcceptThread, this);
    m_heartbeatThread = std::thread(&ServerCore::HeartbeatThread, this);
    return true;
}

void ServerCore::Stop()
{
    m_running = false;
    NetUtils::CloseSocket(m_udpSock); NetUtils::CloseSocket(m_tcpListenSock);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [sock, _] : m_clients) NetUtils::CloseSocket(sock);
        m_clients.clear();
    }
    if (m_udpThread.joinable()) m_udpThread.join();
    if (m_acceptThread.joinable()) m_acceptThread.join();
    if (m_heartbeatThread.joinable()) m_heartbeatThread.join();
    for (auto& t : m_clientThreads) if (t.joinable()) t.join();
    m_clientThreads.clear();
}

void ServerCore::UdpDiscoveryThread()
{
    char recvBuf[256];
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    while (m_running)
    {
        FD_SET fdSet; FD_ZERO(&fdSet); FD_SET(m_udpSock, &fdSet);
        timeval tv = {1,0};
        if (select(0, &fdSet, nullptr, nullptr, &tv) <= 0) continue;
        if (!FD_ISSET(m_udpSock, &fdSet)) continue;
        int len = recvfrom(m_udpSock, recvBuf, sizeof(recvBuf)-1, 0, (sockaddr*)&fromAddr, &fromLen);
        if (len <= 0) continue;
        recvBuf[len] = '\0';
        if (strcmp(recvBuf, DISCOVER_REQ_STR) == 0)
        {
            std::string myIP = NetUtils::GetFirstLocalIP();
            std::ostringstream oss;
            oss << DISCOVER_RSP_PREFIX << m_serverName << "|" << myIP << "|" << TCP_DATA_PORT;
            std::string rsp = oss.str();
            sendto(m_udpSock, rsp.c_str(), (int)rsp.size(), 0, (sockaddr*)&fromAddr, fromLen);
        }
    }
}

void ServerCore::TcpAcceptThread()
{
    while (m_running)
    {
        FD_SET fdSet; FD_ZERO(&fdSet); FD_SET(m_tcpListenSock, &fdSet);
        timeval tv = {1,0};
        if (select(0, &fdSet, nullptr, nullptr, &tv) <= 0) continue;
        sockaddr_in ca; int al = sizeof(ca);
        SOCKET cs = accept(m_tcpListenSock, (sockaddr*)&ca, &al);
        if (cs == INVALID_SOCKET) continue;
        DWORD sendTimeoutMs = 5000;
        setsockopt(cs, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeoutMs, sizeof(sendTimeoutMs));
        char ip[64]; inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ClientInfo info; info.socket = cs; info.ip = ip;
            info.port = ntohs(ca.sin_port); info.name = ip;
            info.lastHeartbeat = GetTickCount64();
            m_clients[cs] = info;
        }
        m_clientThreads.emplace_back(&ServerCore::ClientRecvThread, this, cs, std::string(ip), ntohs(ca.sin_port));
        if (callbacks.onClientConnected) callbacks.onClientConnected(ip, ip);
        BroadcastClientList();
    }
}

void ServerCore::ClientRecvThread(SOCKET clientSock, std::string ip, uint16_t port)
{
    while (m_running)
    {
        uint8_t hdrBuf[sizeof(PacketHeader)];
        if (!NetUtils::RecvAll(clientSock, hdrBuf, sizeof(PacketHeader))) break;
        PacketHeader* hdr = (PacketHeader*)hdrBuf;
        uint32_t plen = Ntohl(hdr->payloadLength);
        std::vector<uint8_t> payload(plen);
        if (plen > 0 && !NetUtils::RecvAll(clientSock, payload.data(), plen)) break;
        MessageType mt = (MessageType)hdr->type;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_clients.find(clientSock);
            if (it != m_clients.end()) it->second.lastHeartbeat = GetTickCount64();
        }
        switch (mt)
        {
        case MessageType::HEARTBEAT: break;
        case MessageType::TEXT_MSG:
            if (callbacks.onTextMessage)
                callbacks.onTextMessage(ip, std::string(payload.begin(), payload.end()));
            break;
        case MessageType::CLIENT_NAME:
        {
            std::string nm(payload.begin(), payload.end());
            { std::lock_guard<std::mutex> lock(m_mutex); auto it = m_clients.find(clientSock); if (it != m_clients.end()) it->second.name = nm; }
            if (callbacks.onClientConnected) callbacks.onClientConnected(ip, nm);
            BroadcastClientList();
            break;
        }
        case MessageType::FILE_META:
        {
            std::string ms(payload.begin(), payload.end());
            FileMeta meta = {};
            size_t p1 = ms.find('|'), p2 = ms.find('|',p1+1), p3 = ms.find('|',p2+1);
            if (p1!=std::string::npos && p2!=std::string::npos && p3!=std::string::npos)
            {
                meta.fileName = ms.substr(0,p1);
                meta.fileSize = std::stoull(ms.substr(p1+1,p2-p1-1));
                meta.transferId = (uint32_t)std::stoul(ms.substr(p2+1,p3-p2-1));
                meta.totalChunks = (uint32_t)std::stoul(ms.substr(p3+1));
            }
            if (callbacks.onFileMeta) callbacks.onFileMeta(ip, meta);
            break;
        }
        case MessageType::FILE_DATA:
            if (payload.size() >= 12 && callbacks.onFileData)
                callbacks.onFileData(ip, Protocol::ReadU32(payload.data()),
                    Protocol::ReadU32(payload.data()+4),
                    std::vector<uint8_t>(payload.begin()+12, payload.end()));
            break;
        case MessageType::FILE_COMPLETE:
            if (payload.size() >= 4 && callbacks.onFileComplete)
                callbacks.onFileComplete(ip, Protocol::ReadU32(payload.data()));
            break;
        case MessageType::FILE_CANCEL:
            if (payload.size() >= 4 && callbacks.onFileCancel)
                callbacks.onFileCancel(ip, Protocol::ReadU32(payload.data()));
            break;
        }
    }
    RemoveClient(clientSock);
}

void ServerCore::HeartbeatThread()
{
    auto lastTime = GetTickCount64();
    while (m_running)
    {
        Sleep(1000);
        auto now = GetTickCount64();
        if (now - lastTime >= HEARTBEAT_INTERVAL_MS)
        {
            lastTime = now;
            BroadcastData(Protocol::EncodeHeartbeat());
            std::vector<SOCKET> toRemove;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& [sock, info] : m_clients)
                    if (now - info.lastHeartbeat > HEARTBEAT_INTERVAL_MS * 3)
                        toRemove.push_back(sock);
                for (SOCKET s : toRemove) { m_clients.erase(s); NetUtils::CloseSocket(s); }
            }
            if (!toRemove.empty() && callbacks.onClientListChanged)
                callbacks.onClientListChanged(GetClients());
        }
    }
}

bool ServerCore::SendTextToClient(SOCKET clientSock, const std::string& text)
{
    return SendDataToClient(clientSock, Protocol::EncodeText(text));
}

void ServerCore::BroadcastText(const std::string& text)
{
    BroadcastData(Protocol::EncodeText(text));
}

bool ServerCore::SendDataToClient(SOCKET clientSock, const std::vector<uint8_t>& data)
{
    return NetUtils::SendAll(clientSock, data.data(), (int)data.size());
}

void ServerCore::BroadcastData(const std::vector<uint8_t>& data)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [sock, _] : m_clients)
        NetUtils::SendAll(sock, data.data(), (int)data.size());
}

std::vector<ClientInfo> ServerCore::GetClients() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ClientInfo> list;
    for (const auto& [_, info] : m_clients) list.push_back(info);
    return list;
}

void ServerCore::RemoveClient(SOCKET sock)
{
    std::string ip;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_clients.find(sock);
        if (it != m_clients.end()) { ip = it->second.ip; m_clients.erase(it); }
    }
    NetUtils::CloseSocket(sock);
    if (!ip.empty() && callbacks.onClientDisconnected) callbacks.onClientDisconnected(ip);
    BroadcastClientList();
}

void ServerCore::BroadcastClientList()
{
    if (callbacks.onClientListChanged) callbacks.onClientListChanged(GetClients());
}
