#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

class UdpServer {
public:
    using Callback = std::function<void(const uint8_t*, int)>;
    UdpServer(int port, Callback cb);
    ~UdpServer();
    void start();
    void stop();
private:
    void threadFunc();
    int port_;
    Callback callback_;
    std::atomic<bool> running_;
    std::thread th_;
};
