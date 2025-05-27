#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

#ifndef _WIN32
#include <unistd.h> // For pipe
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#endif

namespace FMRack {

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
#ifdef _WIN32
    SOCKET sock_;
#else
    int sock_;
    int shutdown_pipe_fds_[2]; // Pipe for shutdown signaling: [0] = read, [1] = write
#endif
};

}  // namespace FMRack