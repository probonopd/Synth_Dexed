#include "midi_socket_server.h"
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

MidiSocketServer::MidiSocketServer(int port, MidiCallback cb)
    : port_(port), callback_(cb), running_(false) {}

MidiSocketServer::~MidiSocketServer() { stop(); }

void MidiSocketServer::start() {
    running_ = true;
    th_ = std::thread(&MidiSocketServer::threadFunc, this);
}

void MidiSocketServer::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void MidiSocketServer::threadFunc() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
    int listen_sock =
#ifdef _WIN32
        socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
        socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (listen_sock < 0) {
        std::cerr << "[MIDI SOCKET] Failed to create socket" << std::endl;
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[MIDI SOCKET] Failed to bind socket" << std::endl;
#ifdef _WIN32
        closesocket(listen_sock);
        WSACleanup();
#else
        close(listen_sock);
#endif
        return;
    }
    listen(listen_sock, 1);
    std::cout << "[INFO] MIDI socket server listening on 127.0.0.1:" << port_ << std::endl;
    while (running_) {
#ifdef _WIN32
        SOCKET client = accept(listen_sock, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
#else
        int client = accept(listen_sock, NULL, NULL);
        if (client < 0) continue;
#endif
        std::cout << "[MIDI SOCKET] Client connected" << std::endl;
        uint8_t buf[256];
        int n =
#ifdef _WIN32
            recv(client, (char*)buf, sizeof(buf), 0);
#else
            recv(client, buf, sizeof(buf), 0);
#endif
        if (n > 0) callback_(buf, n);
#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
    }
#ifdef _WIN32
    closesocket(listen_sock);
    WSACleanup();
#else
    close(listen_sock);
#endif
}

MidiUdpServer::MidiUdpServer(int port, MidiCallback cb)
    : port_(port), callback_(cb), running_(false) {}

MidiUdpServer::~MidiUdpServer() { stop(); }

void MidiUdpServer::start() {
    running_ = true;
    th_ = std::thread(&MidiUdpServer::threadFunc, this);
}

void MidiUdpServer::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void MidiUdpServer::threadFunc() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
    int sock =
#ifdef _WIN32
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        socket(AF_INET, SOCK_DGRAM, 0);
#endif
    if (sock < 0) {
        std::cerr << "[MIDI UDP] Failed to create socket" << std::endl;
        return;
    }
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    // Bind to all interfaces for UDP (INADDR_ANY)
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[MIDI UDP] Failed to bind socket" << std::endl;
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return;
    }
    std::cout << "[INFO] MIDI UDP server listening on 0.0.0.0:" << port_ << std::endl;
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
        if (n > 0) callback_(buf, n);
    }
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
}
