#include "UdpServer.h"
#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#endif

// Debug macro/flag for debug output (no main.h include needed)
extern bool debugEnabled;
#define DEBUG_PRINT(x) do { if (debugEnabled) { std::cout << x << std::endl; } } while(0)

UdpServer::UdpServer(int port, Callback cb)
    : port_(port), callback_(cb), running_(false) {}

UdpServer::~UdpServer() { stop(); }

void UdpServer::start() {
    running_ = true;
    th_ = std::thread(&UdpServer::threadFunc, this);
}

void UdpServer::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void UdpServer::threadFunc() {
    DEBUG_PRINT("[UDP] UdpServer::threadFunc started (port=" << port_ << ")");
#ifdef _WIN32
    WSADATA wsaData;
    int wsaret = WSAStartup(MAKEWORD(2,2), &wsaData);
    DEBUG_PRINT("[UDP] WSAStartup returned: " << wsaret);
#endif
    int sock =
#ifdef _WIN32
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        socket(AF_INET, SOCK_DGRAM, 0);
#endif
    DEBUG_PRINT("[UDP] socket() returned: " << sock);
    if (sock < 0) {
        std::cerr << "[UDP] Failed to create socket" << std::endl;
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    int bindret = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    DEBUG_PRINT("[UDP] bind() returned: " << bindret);
    if (bindret < 0) {
        std::cerr << "[UDP] Failed to bind socket" << std::endl;
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return;
    }
    std::cout << "[INFO] UDP server listening on 0.0.0.0:" << port_ << std::endl;
    while (running_) {
        uint8_t buf[512];
        sockaddr_in src_addr = {};
#ifdef _WIN32
        int addrlen = sizeof(src_addr);
        int n = recvfrom(sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&src_addr, &addrlen);
#else
        socklen_t addrlen = sizeof(src_addr);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&src_addr, &addrlen);
#endif
        if (n > 0) {
            char addrstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &src_addr.sin_addr, addrstr, sizeof(addrstr));
            DEBUG_PRINT("[UDP] Received " << n << " bytes from " << addrstr << ":" << ntohs(src_addr.sin_port));
            if (debugEnabled) {
                std::cout << "[UDP] Data: ";
                for (int i = 0; i < n; ++i) std::cout << (int)buf[i] << " ";
                std::cout << std::endl;
            }
            callback_(buf, n);
        }
    }
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}
