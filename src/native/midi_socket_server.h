#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

// Cross-platform TCP MIDI socket server
class MidiSocketServer {
public:
    using MidiCallback = std::function<void(const uint8_t* data, size_t len)>;
    MidiSocketServer(int port, MidiCallback cb);
    ~MidiSocketServer();
    void start();
    void stop();
private:
    void threadFunc();
    int port_;
    MidiCallback callback_;
    std::thread th_;
    std::atomic<bool> running_;
};

// Cross-platform UDP MIDI socket server
class MidiUdpServer {
public:
    using MidiCallback = std::function<void(const uint8_t* data, size_t len)>;
    MidiUdpServer(int port, MidiCallback cb);
    ~MidiUdpServer();
    void start();
    void stop();
    int port() const { return port_; }
private:
    void threadFunc();
    int port_;
    MidiCallback callback_;
    std::thread th_;
    std::atomic<bool> running_;
};
