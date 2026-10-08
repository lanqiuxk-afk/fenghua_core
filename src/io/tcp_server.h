#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdint>

namespace fh {


struct InputPacket {
    uint8_t type;
    std::vector<uint8_t> payload;
};

class TcpServer {
public:
    using PacketCallback = std::function<void(const InputPacket&)>;

    TcpServer();
    ~TcpServer();

    bool Start(int port = 12345);
    void Stop();
    bool IsRunning() const { return m_running; }
    void SetCallback(PacketCallback cb) { m_callback = std::move(cb); }

private:
    void AcceptLoop();
    void ClientLoop(int clientFd);

    std::atomic<bool> m_running{false};
    int m_serverFd = -1;
    int m_port = 12345;
    std::thread m_acceptThread;
    std::vector<std::thread> m_clientThreads;
    PacketCallback m_callback;
};


}  // namespace fh

#endif
