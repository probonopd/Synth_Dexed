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
#include <cstring> // For strerror
#include <cerrno>  // For errno
#include <iostream> // For std::cerr (debugging)
#include <fcntl.h>  // For fcntl
#include <sys/select.h> // For select
#endif

// Debug macro/flag for debug output (no main.h include needed)
extern bool debugEnabled;
#define DEBUG_PRINT(x) do { if (debugEnabled) { std::cout << x << std::endl; } } while(0)

namespace FMRack {

UdpServer::UdpServer(int port, Callback cb) : port_(port), callback_(cb), running_(false), sock_(-1) {
#ifdef _WIN32
    // Windows specific initialization
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // Handle error
        if (debugEnabled) std::cerr << "WSAStartup failed" << std::endl;
    }
#else
    shutdown_pipe_fds_[0] = -1;
    shutdown_pipe_fds_[1] = -1;
    if (pipe(shutdown_pipe_fds_) == -1) {
        if (debugEnabled) perror("UdpServer: Failed to create shutdown pipe");
        // Proceed without pipe, shutdown might be less reliable
    } else {
        // Set read end to non-blocking
        int flags = fcntl(shutdown_pipe_fds_[0], F_GETFL, 0);
        if (flags == -1 || fcntl(shutdown_pipe_fds_[0], F_SETFL, flags | O_NONBLOCK) == -1) {
            if (debugEnabled) perror("UdpServer: Failed to set shutdown pipe O_NONBLOCK");
            close(shutdown_pipe_fds_[0]);
            close(shutdown_pipe_fds_[1]);
            shutdown_pipe_fds_[0] = -1;
            shutdown_pipe_fds_[1] = -1;
        } else {
            if (debugEnabled) std::cout << "[UDP SERVER] Shutdown pipe created successfully." << std::endl;
        }
    }
#endif
}

UdpServer::~UdpServer() {
    if (running_) {
        stop();
    }
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
    }
    WSACleanup();
#else
    if (sock_ != -1) {
        close(sock_);
        sock_ = -1;
    }
    if (shutdown_pipe_fds_[0] != -1) {
        close(shutdown_pipe_fds_[0]);
        shutdown_pipe_fds_[0] = -1;
    }
    if (shutdown_pipe_fds_[1] != -1) {
        close(shutdown_pipe_fds_[1]);
        shutdown_pipe_fds_[1] = -1;
    }
#endif
    if (debugEnabled) std::cout << "[UDP SERVER] Destructor finished." << std::endl;
}

void UdpServer::start() {
    if (running_) return;

#ifdef _WIN32
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) {
        // Handle error
        if (debugEnabled) std::cerr << "Failed to create socket: " << WSAGetLastError() << std::endl;
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port_);

    if (bind(sock_, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        // Handle error
        if (debugEnabled) std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return;
    }
#else
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        if (debugEnabled) perror("UdpServer: Failed to create socket");
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port_);

    if (bind(sock_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        if (debugEnabled) perror("UdpServer: Bind failed");
        close(sock_);
        sock_ = -1;
        return;
    }
#endif

    running_ = true;
    th_ = std::thread(&UdpServer::threadFunc, this);
    if (debugEnabled) std::cout << "[UDP SERVER] Started on port " << port_ << "." << std::endl;
}

void UdpServer::stop() {
    if (!running_.exchange(false)) { // Atomically set to false and get previous value
        if (debugEnabled) std::cout << "[UDP SERVER] Stop called but already stopping or stopped." << std::endl;
        if (th_.joinable()) { // Ensure join is attempted if thread was started
             // th_.join(); // This might be too aggressive if called multiple times
        }
        return;
    }

    if (debugEnabled) std::cout << "[UDP SERVER] Stopping..." << std::endl;

#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        shutdown(sock_, SD_RECEIVE); // Interrupts recvfrom on Windows
        // closesocket(sock_); // Closing might be better done after join or in destructor
        // sock_ = INVALID_SOCKET; // Mark as closed
    }
#else
    if (shutdown_pipe_fds_[1] != -1) {
        if (debugEnabled) std::cout << "[UDP SERVER] Writing to shutdown pipe." << std::endl;
        char dummy = 's';
        ssize_t written = write(shutdown_pipe_fds_[1], &dummy, 1);
        if (written <= 0) {
            if (debugEnabled) perror("UdpServer: Write to shutdown pipe failed");
        }
    }
    // Closing the socket here will also interrupt recvfrom/select
    if (sock_ != -1) {
         // close(sock_); // Moved to after join or destructor for cleanliness
         // sock_ = -1; // Mark as closed
    }
#endif

    if (th_.joinable()) {
        if (debugEnabled) std::cout << "[UDP SERVER] Joining thread..." << std::endl;
        th_.join();
        if (debugEnabled) std::cout << "[UDP SERVER] Thread joined." << std::endl;
    } else {
        if (debugEnabled) std::cout << "[UDP SERVER] Thread was not joinable on stop." << std::endl;
    }
    
    // Ensure socket is closed after thread has finished
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
#else
    if (sock_ != -1) {
        close(sock_);
        sock_ = -1;
    }
#endif
    if (debugEnabled) std::cout << "[UDP SERVER] Stopped." << std::endl;
}

void UdpServer::threadFunc() {
    if (debugEnabled) std::cout << "[UDP SERVER THREAD] Started." << std::endl;
    char buffer[1024]; // Max UDP packet size

#ifdef _WIN32
    // Windows uses blocking recvfrom with shutdown() in stop() to unblock
    while (running_) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        int len = recvfrom(sock_, buffer, sizeof(buffer), 0, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (len > 0) {
            if (running_ && callback_) { // Check running_ again in case stop() was called during recvfrom
                callback_(reinterpret_cast<uint8_t*>(buffer), len);
            }
        } else if (len == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEINTR || error == WSAECONNRESET || error == WSAESHUTDOWN || error == WSAENOTSOCK) {
                 if (debugEnabled) std::cout << "[UDP SERVER THREAD] recvfrom interrupted or socket closed, error: " << error << ". Exiting loop." << std::endl;
                break; 
            }
            if (running_) { // Only log if we weren't expecting a shutdown
                 if (debugEnabled) std::cerr << "[UDP SERVER THREAD] recvfrom failed: " << error << std::endl;
            }
            break; // Exit on other errors too
        } else if (len == 0) {
            // Connection gracefully closed (not typical for UDP)
            if (debugEnabled) std::cout << "[UDP SERVER THREAD] recvfrom returned 0." << std::endl;
        }
        // Minimal sleep to prevent tight loop if recvfrom returns immediately with no data (should not happen for UDP blocking)
        // std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
    }
#else
    // Linux/macOS uses select() with a pipe for shutdown
    fd_set read_fds;
    int max_fd = sock_;
    if (shutdown_pipe_fds_[0] != -1) {
        max_fd = std::max(sock_, shutdown_pipe_fds_[0]);
    }

    while (running_) {
        FD_ZERO(&read_fds);
        FD_SET(sock_, &read_fds);
        if (shutdown_pipe_fds_[0] != -1) {
            FD_SET(shutdown_pipe_fds_[0], &read_fds);
        }

        struct timeval timeout;
        timeout.tv_sec = 1; // 1 second timeout for select
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (!running_) { // Check running_ immediately after select returns
            if (debugEnabled) std::cout << "[UDP SERVER THREAD] running_ is false after select. Exiting loop." << std::endl;
            break;
        }

        if (activity < 0) {
            if (errno == EINTR) { // Interrupted by a signal (e.g. SIGINT handled by main)
                if (debugEnabled) std::cout << "[UDP SERVER THREAD] select() interrupted by signal (EINTR)." << std::endl;
                continue; // Re-check running_ and continue loop
            }
            if (debugEnabled) perror("UdpServer: select() error");
            break; // Exit on other select errors
        }

        if (activity == 0) { // Timeout
            // if (debugEnabled) std::cout << "[UDP SERVER THREAD] select() timed out." << std::endl;
            continue; // Continue to check running_
        }
        
        if (shutdown_pipe_fds_[0] != -1 && FD_ISSET(shutdown_pipe_fds_[0], &read_fds)) {
            if (debugEnabled) std::cout << "[UDP SERVER THREAD] Shutdown pipe signaled." << std::endl;
            char drain_buf[32];
            while (read(shutdown_pipe_fds_[0], drain_buf, sizeof(drain_buf)) > 0); // Drain the pipe
            // running_ should be false, loop will terminate
            break;
        }

        if (FD_ISSET(sock_, &read_fds)) {
            sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            ssize_t len = recvfrom(sock_, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);

            if (len > 0) {
                if (running_ && callback_) { // Check running_ again
                    callback_(reinterpret_cast<uint8_t*>(buffer), len);
                }
            } else if (len < 0) {
                if (errno == EINTR) continue; // Interrupted by signal, continue loop
                if (running_) { // Only log if we weren't expecting a shutdown
                    if (debugEnabled) perror("UdpServer: recvfrom failed");
                }
                break; // Exit on other recvfrom errors
            } else { // len == 0
                 if (debugEnabled) std::cout << "[UDP SERVER THREAD] recvfrom returned 0." << std::endl;
            }
        }
    }
#endif
    if (debugEnabled) std::cout << "[UDP SERVER THREAD] Exited." << std::endl;
}

} // namespace FMRack
