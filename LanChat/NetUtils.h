#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

class NetUtils {
public:
    static bool Initialize();
    static void Cleanup();
    static std::vector<std::string> GetLocalIPs();
    static std::string GetFirstLocalIP();
    static SOCKET CreateUdpBroadcastSocket(uint16_t port);
    static SOCKET CreateUdpListenSocket(uint16_t port);
    static SOCKET CreateTcpListenSocket(uint16_t port);
    static SOCKET ConnectToServer(const std::string& ip, uint16_t port, int timeoutMs = 3000);
    static bool SetNonBlocking(SOCKET sock, bool nonBlocking);
    static void CloseSocket(SOCKET sock);
    static bool RecvAll(SOCKET sock, uint8_t* buffer, int len);
    static bool SendAll(SOCKET sock, const uint8_t* data, int len);
};
