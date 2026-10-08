#include "io/tcp_server.h"
#include "io/file_logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <errno.h>

namespace fh {


static bool RecvExact(int fd, void* data, size_t len, const std::atomic<bool>& running) {
    size_t got = 0;
    auto* out = static_cast<uint8_t*>(data);
    while (got < len && running) {
        ssize_t n = recv(fd, out + got, len - got, 0);
        if (n > 0) { got += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        return false;
    }
    return got == len;
}

TcpServer::TcpServer() {}
TcpServer::~TcpServer() { Stop(); }

bool TcpServer::Start(int port) {
    if (m_running) return true;
    m_port = port;

    m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        FH_LOG("ERROR", "TCP", "socket() failed: %s", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);

    if (bind(m_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        FH_LOG("ERROR", "TCP", "bind() failed: %s", strerror(errno));
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    if (listen(m_serverFd, 5) < 0) {
        FH_LOG("ERROR", "TCP", "listen() failed: %s", strerror(errno));
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    m_running = true;
    m_acceptThread = std::thread(&TcpServer::AcceptLoop, this);
    FH_LOG("INFO", "TCP", "Server started on port %d", m_port);
    return true;
}

void TcpServer::Stop() {
    m_running = false;
    if (m_serverFd >= 0) {
        shutdown(m_serverFd, SHUT_RDWR);
        close(m_serverFd);
        m_serverFd = -1;
    }
    if (m_acceptThread.joinable()) m_acceptThread.join();
    for (auto& t : m_clientThreads) {
        if (t.joinable()) t.join();
    }
    m_clientThreads.clear();
    FH_LOG("INFO", "TCP", "Server stopped");
}

void TcpServer::AcceptLoop() {
    while (m_running) {
        struct sockaddr_in clientAddr = {};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(m_serverFd, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientFd < 0) {
            if (m_running) FH_LOG("ERROR", "TCP", "accept() failed");
            continue;
        }
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        FH_LOG("INFO", "TCP", "Client connected: %s:%d", ip, ntohs(clientAddr.sin_port));

        m_clientThreads.emplace_back(&TcpServer::ClientLoop, this, clientFd);
    }
}

void TcpServer::ClientLoop(int clientFd) {
    struct timeval tv = {1, 0};
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (m_running) {
        uint8_t header[3];
        if (!RecvExact(clientFd, header, sizeof(header), m_running)) break;

        InputPacket pkt;
        pkt.type = header[0];
        uint16_t len = header[1] | (header[2] << 8);

        if (len > 0) {
            pkt.payload.resize(len);
            if (!RecvExact(clientFd, pkt.payload.data(), len, m_running)) break;
        }

        if (m_callback) m_callback(pkt);
    }
    close(clientFd);
}

}  // namespace fh
