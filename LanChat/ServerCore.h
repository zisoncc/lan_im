#pragma once
#include <winsock2.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <functional>
#include "Protocol.h"
#include "NetUtils.h"

struct ClientInfo {
    SOCKET      socket;
    std::string ip;
    uint16_t    port;
    std::string name;
    uint64_t    lastHeartbeat;
};

struct ServerCallbacks {
    std::function<void(const std::string& clientIp, const std::string& clientName)> onClientConnected;
    std::function<void(const std::string& clientIp)> onClientDisconnected;
    std::function<void(const std::string& clientIp, const std::string& message)> onTextMessage;
    std::function<void(const std::string& clientIp, const FileMeta& meta)> onFileMeta;
    std::function<void(const std::string& clientIp, uint32_t transferId, uint32_t chunkIndex, const std::vector<uint8_t>& data)> onFileData;
    std::function<void(const std::string& clientIp, uint32_t transferId)> onFileComplete;
    std::function<void(const std::string& clientIp, uint32_t transferId)> onFileCancel;
    std::function<void(const std::vector<ClientInfo>& clients)> onClientListChanged;
};

class ServerCore {
public:
    ServerCore();
    ~ServerCore();
    bool Start(const std::string& serverName);
    void Stop();
    bool SendTextToClient(SOCKET clientSock, const std::string& text);
    void BroadcastText(const std::string& text);
    bool SendDataToClient(SOCKET clientSock, const std::vector<uint8_t>& data);
    void BroadcastData(const std::vector<uint8_t>& data);
    std::vector<ClientInfo> GetClients() const;
    std::string GetServerName() const { return m_serverName; }
    ServerCallbacks callbacks;
private:
    void UdpDiscoveryThread();
    void TcpAcceptThread();
    void ClientRecvThread(SOCKET clientSock, std::string ip, uint16_t port);
    void HeartbeatThread();
    void RemoveClient(SOCKET sock);
    void BroadcastClientList();
    std::string m_serverName;
    bool m_running;
    SOCKET m_udpSock;
    SOCKET m_tcpListenSock;
    std::thread m_udpThread;
    std::thread m_acceptThread;
    std::thread m_heartbeatThread;
    mutable std::mutex m_mutex;
    std::map<SOCKET, ClientInfo> m_clients;
    std::vector<std::thread> m_clientThreads;
};
