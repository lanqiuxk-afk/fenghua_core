#include "io/udp_server.h"
#include "io/tcp_server.h"
#include "io/file_logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <time.h>

namespace fh {


UdpServer::UdpServer() {}
UdpServer::~UdpServer() { Stop(); }

bool UdpServer::Start(int port) {
    if (m_running) return true;
    m_port = port;
    m_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0) { FH_LOG("ERROR","UDP","socket failed"); return false; }

    int opt = 1;
    setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);
    if (bind(m_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        FH_LOG("ERROR","UDP","bind failed: %s",strerror(errno));
        close(m_fd); m_fd = -1; return false;
    }

    m_running = true;
    m_thread = std::thread(&UdpServer::RecvLoop, this);
    FH_LOG("INFO","UDP","Listening on port %d", m_port);
    return true;
}

void UdpServer::Stop() {
    m_running = false;
    // Keep the descriptor valid until RecvLoop has observed m_running=false.
    // SO_RCVTIMEO bounds this join to about 100ms and avoids close/recvfrom fd
    // reuse races when switching rapidly between network and direct input.
    if (m_thread.joinable()) m_thread.join();
    if (m_fd >= 0) { close(m_fd); m_fd = -1; }
    m_peerValid = false;
    m_lastPingSend = 0;
}

void UdpServer::RecvLoop() {
    uint8_t buf[65536];
    struct timeval tv = {0, 100000}; // 100ms
    setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int64_t lastPing = 0;
    uint8_t ping[3] = {0x30, 0, 0};

    while (m_running) {
        struct sockaddr_in from;
        socklen_t fromLen = sizeof(from);
        ssize_t n = recvfrom(m_fd, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fromLen);
        if (!m_running) break;
        if (n >= 3) {
            m_peer = from;
            m_peerValid = true;
            InputPacket pkt;
            pkt.type = buf[0];
            uint16_t len = buf[1] | (buf[2] << 8);
            if (len > 0 && n >= (ssize_t)(len + 3))
                pkt.payload.assign(buf + 3, buf + 3 + len);
            if (m_callback) m_callback(pkt);
        }
        // Periodic ping for RTT measurement (every 1000ms)
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        int64_t nowMs = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
        if (m_peerValid && nowMs - lastPing >= 1000) {
            lastPing = nowMs;
            m_lastPingSend = ts.tv_sec + ts.tv_nsec * 1e-9;
            sendto(m_fd, (const char*)ping, 3, 0, (struct sockaddr*)&m_peer, sizeof(m_peer));
        }
    }
}

int UdpServer::SendToPeer(const uint8_t* data, int len) {
    if (!m_peerValid || m_fd < 0) return -1;
    return (int)sendto(m_fd, (const char*)data, len, 0, (struct sockaddr*)&m_peer, sizeof(m_peer));
}

}  // namespace fh
