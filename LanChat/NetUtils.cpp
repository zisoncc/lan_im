#include "NetUtils.h"
#include <stdexcept>
#include <iphlpapi.h>
#include <vector>
#include <mutex>

#pragma comment(lib, "iphlpapi.lib")

static std::mutex g_sendMutex;

bool NetUtils::Initialize()
{
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void NetUtils::Cleanup()
{
    WSACleanup();
}

std::vector<std::string> NetUtils::GetLocalIPs()
{
    std::vector<std::string> ips;
    DWORD size = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &size);
    if (size == 0) return ips;
    std::vector<uint8_t> buf(size);
    PIP_ADAPTER_ADDRESSES pAddr = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    if (GetAdaptersAddresses(AF_INET, 0, nullptr, pAddr, &size) != NO_ERROR)
        return ips;
    for (auto p = pAddr; p; p = p->Next)
    {
        if (p->OperStatus != IfOperStatusUp) continue;
        for (auto u = p->FirstUnicastAddress; u; u = u->Next)
        {
            sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            char ip[64];
            inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
            if (strcmp(ip, "127.0.0.1") != 0)
                ips.push_back(ip);
        }
    }
    return ips;
}

std::string NetUtils::GetFirstLocalIP()
{
    auto ips = GetLocalIPs();
    return ips.empty() ? "127.0.0.1" : ips[0];
}

SOCKET NetUtils::CreateUdpBroadcastSocket(uint16_t port)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL opt = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&opt, sizeof(opt));
    return sock;
}

SOCKET NetUtils::CreateUdpListenSocket(uint16_t port)
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL opt = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

SOCKET NetUtils::CreateTcpListenSocket(uint16_t port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    BOOL opt = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

SOCKET NetUtils::ConnectToServer(const std::string& ip, uint16_t port, int timeoutMs)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    SetNonBlocking(sock, true);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    connect(sock, (sockaddr*)&addr, sizeof(addr));
    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(sock, &fd);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int ret = select(1, nullptr, &fd, nullptr, &tv);
    if (ret <= 0) { closesocket(sock); return INVALID_SOCKET; }
    int err = 0;
    socklen_t errLen = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errLen);
    if (err != 0) { closesocket(sock); return INVALID_SOCKET; }
    SetNonBlocking(sock, false);
    return sock;
}

bool NetUtils::SetNonBlocking(SOCKET sock, bool nonBlocking)
{
    u_long mode = nonBlocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
}

void NetUtils::CloseSocket(SOCKET sock)
{
    if (sock != INVALID_SOCKET)
        closesocket(sock);
}

bool NetUtils::RecvAll(SOCKET sock, uint8_t* buffer, int len)
{
    while (len > 0)
    {
        int ret = recv(sock, (char*)buffer, len, 0);
        if (ret <= 0) return false;
        buffer += ret;
        len -= ret;
    }
    return true;
}

bool NetUtils::SendAll(SOCKET sock, const uint8_t* data, int len)
{
    std::lock_guard<std::mutex> lock(g_sendMutex);
    while (len > 0)
    {
        int ret = send(sock, (char*)data, len, 0);
        if (ret <= 0) return false;
        data += ret;
        len -= ret;
    }
    return true;
}
