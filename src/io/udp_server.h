#ifndef UDP_SERVER_H
#define UDP_SERVER_H

#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>

namespace fh {


struct InputPacket;

class UdpServer {
public:
    using PacketCallback = std::function<void(const InputPacket&)>;

    UdpServer();
    ~UdpServer();

    bool Start(int port);
    void Stop();
    bool IsRunning() const { return m_running; }
    void SetCallback(PacketCallback cb) { m_callback = std::move(cb); }

    // Send a raw packet back to the last peer (the PC). Returns bytes sent.
    int SendToPeer(const uint8_t* data, int len);
    bool HasPeer() const { return m_peerValid; }
    double LastPingSend() const { return m_lastPingSend; }

private:
    void RecvLoop();
    std::atomic<bool> m_running{false};
    int m_fd = -1, m_port = 56789;
    std::thread m_thread;
    PacketCallback m_callback;
    struct sockaddr_in m_peer = {};
    bool m_peerValid = false;
    double m_lastPingSend = 0;
};


}  // namespace fh

#endif
