#include "capture/screen_stream.h"
#include "io/file_logger.h"
#include "util/json.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <algorithm>
#include <fstream>

namespace fh {


static std::atomic<bool> g_running{false};
struct PendingChunk {
    std::vector<uint8_t> data;
    size_t offset = 0;
};
struct OutputClient {
    int fd = -1;
    std::deque<PendingChunk> queue;
    size_t queuedBytes = 0;
};
static std::vector<OutputClient> g_outs;
static std::mutex g_mtx;
static std::vector<uint8_t> g_prefix;
static std::mutex g_prefixMtx;
static std::atomic<int> g_serverFd{-1};
static std::thread g_serverThread;
static constexpr size_t kPrefixMax = 512;  // 流头 + 少量 CSD/首包缓存
static constexpr size_t kClientQueueMax = 4 * 1024 * 1024;

// 配置落盘改为异步合并写:
// 旧实现直接在渲染线程里 ofstream + JSON 序列化 + tmp + rename, 点设置/改投屏
// 参数时单次阻塞 0.2~30ms(宏多时更久), CPU 软渲染下就是"点一下卡一下"。
// 这里只做一次内存拷贝并投递, 由后台线程合并节流后落盘。
namespace {
struct AsyncFileWriter {
    std::mutex mtx;
    std::string pendingName;
    std::string pendingData;
    bool hasPending = false;
    bool running = false;
    // 不能用 std::thread 成员直接赋值(mutex 成员不可移动); 用指针持有
    std::unique_ptr<std::thread> worker;
    std::condition_variable cv;

    void Kick() {
        if (running) return;
        running = true;
        worker.reset(new std::thread([this] {
            for (;;) {
                std::string name, data;
                {
                    std::unique_lock<std::mutex> ul(mtx);
                    cv.wait_for(ul, std::chrono::milliseconds(300),
                                [this] { return hasPending; });
                    if (!hasPending) { running = false; return; }   // 空闲即退出
                    name.swap(pendingName);
                    data.swap(pendingData);
                    hasPending = false;
                }
                FILE* f = fopen(name.c_str(), "wb");
                if (f) {
                    fwrite(data.data(), 1, data.size(), f);
                    fclose(f);
                }
            }
        }));
    }

    void Post(const std::string& path, std::string&& data) {
        {
            std::lock_guard<std::mutex> l(mtx);
            pendingName = path;
            pendingData = std::move(data);
            hasPending = true;
        }
        Kick();
        cv.notify_all();
    }
};
AsyncFileWriter g_asyncWriter;
}  // namespace

// 投屏配置落盘(异步): 只在内容真的变化时调用
void WriteScreenConfigAsync(const ScreenConfig& sc) {
    try {
        nlohmann::json j;
        j["streaming"] = sc.streaming;
        j["w"] = sc.captureW;
        j["h"] = sc.captureH;
        j["fullscreen"] = sc.fullscreen;
        g_asyncWriter.Post("/data/adb/fenghua/screen.json", j.dump());
    } catch (...) {}
}

void WriteScreenConfig(const ScreenConfig& sc) {
    try {
        nlohmann::json j;
        j["streaming"] = sc.streaming;
        j["w"] = sc.captureW;
        j["h"] = sc.captureH;
        j["fullscreen"] = sc.fullscreen;
        std::ofstream ofs("/data/adb/fenghua/screen.json");
        ofs << j.dump();
    } catch (...) {}
}

// 新采集会话开始: 清空前缀缓存
void ScreenStreamBegin() {
    {
        std::lock_guard<std::mutex> l(g_prefixMtx);
        g_prefix.clear();
    }
    // A new encoder session must not be appended after stale bytes queued
    // for a slow client.
    std::lock_guard<std::mutex> l(g_mtx);
    for (auto& client : g_outs) {
        client.queue.clear();
        client.queuedBytes = 0;
    }
}

static bool FlushClient(OutputClient& client) {
    while (!client.queue.empty()) {
        PendingChunk& chunk = client.queue.front();
        size_t remain = chunk.data.size() - chunk.offset;
        ssize_t n = send(client.fd, (const char*)chunk.data.data() + chunk.offset,
                         remain, MSG_NOSIGNAL);
        if (n > 0) {
            chunk.offset += (size_t)n;
            client.queuedBytes -= (size_t)n;
            if (chunk.offset == chunk.data.size()) client.queue.pop_front();
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

// Queue complete chunks per client and drain them with full writes. This keeps
// the raw Annex-B byte stream intact across TCP partial writes.
static void BroadcastToClients(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> l(g_mtx);
    for (auto it = g_outs.begin(); it != g_outs.end();) {
        if (len > kClientQueueMax || it->queuedBytes > kClientQueueMax - len) {
            close(it->fd);
            it = g_outs.erase(it);
        } else {
            PendingChunk chunk;
            chunk.data.assign(data, data + len);
            it->queuedBytes += len;
            it->queue.push_back(std::move(chunk));
            if (!FlushClient(*it)) {
                close(it->fd);
                it = g_outs.erase(it);
            } else ++it;
        }
    }
}

// 推送数据到所有 PC 客户端, 只缓存完整流头; 中途客户端等待下一个关键帧
void ScreenStreamPush(const uint8_t* data, size_t len) {
    if (len == 0 || !g_running.load()) return;
    {
        std::lock_guard<std::mutex> l(g_prefixMtx);
        if (g_prefix.size() < kPrefixMax) {
            size_t take = std::min(len, kPrefixMax - g_prefix.size());
            g_prefix.insert(g_prefix.end(), data, data + take);
        }
    }
    BroadcastToClients(data, len);
}

void StartScreenServer() {
    if (g_running.load()) return;
    if (g_serverThread.joinable()) g_serverThread.join();
    g_running = true;
    g_serverThread = std::thread([]{
        int srv = socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) {
            FH_LOG("ERROR", "Screen", "socket failed: %s", strerror(errno));
            g_running = false;
            return;
        }
        int opt = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        // 子进程 exec 时不要继承监听 socket, 避免残留占端口
        int srvFlags = fcntl(srv, F_GETFD);
        fcntl(srv, F_SETFD, srvFlags | FD_CLOEXEC);
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(56790);
        if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            FH_LOG("ERROR", "Screen", "bind 56790 failed: %s", strerror(errno));
            close(srv);
            g_running = false;
            return;
        }
        if (listen(srv, 8) < 0) {
            FH_LOG("ERROR", "Screen", "listen 56790 failed: %s", strerror(errno));
            close(srv);
            g_running = false;
            return;
        }
        g_serverFd = srv;
        if (!g_running.load()) {
            g_serverFd = -1;
            close(srv);
            return;
        }
        FH_LOG("INFO", "Screen", "forwarder listening on 56790");
        while (g_running.load()) {
            struct sockaddr_in cli = {};
            socklen_t cl = sizeof(cli);
            int c = accept(srv, (struct sockaddr*)&cli, &cl);
            if (c < 0) {
                if (!g_running.load()) break;
                continue;
            }
            setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
            int cFlags = fcntl(c, F_GETFD);
            fcntl(c, F_SETFD, cFlags | FD_CLOEXEC);
            bool fromLocal = (cli.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
            if (fromLocal) {
                // 新采集会话接入: 清掉旧流头缓存, 避免 PC 拿到过期分辨率
                {
                    std::lock_guard<std::mutex> lp(g_prefixMtx);
                    g_prefix.clear();
                }
                // 采集输入源(本机采集子进程): 读数据 -> 缓存前缀 + 转发给所有 PC 客户端
                std::thread([c] {
                    char buf[16384];
                    while (true) {
                        ssize_t n = recv(c, buf, sizeof(buf), 0);
                        if (n <= 0) break;
                        {
                            std::lock_guard<std::mutex> l(g_prefixMtx);
                            if (g_prefix.size() < kPrefixMax) {
                                size_t take = std::min((size_t)n, kPrefixMax - g_prefix.size());
                                g_prefix.insert(g_prefix.end(), buf, buf + take);
                            }
                        }
                        BroadcastToClients((const uint8_t*)buf, (size_t)n);
                    }
                    close(c);
                }).detach();
            } else {
                // PC 客户端: 先重放流头+前缀, 再加入实时转发列表
                OutputClient client;
                client.fd = c;
                {
                    std::lock_guard<std::mutex> lp(g_prefixMtx);
                    if (!g_prefix.empty()) {
                        PendingChunk chunk;
                        chunk.data = g_prefix;
                        client.queuedBytes = chunk.data.size();
                        client.queue.push_back(std::move(chunk));
                    }
                }
                std::lock_guard<std::mutex> l(g_mtx);
                if (!FlushClient(client)) close(c);
                else g_outs.push_back(std::move(client));
            }
        }
        g_serverFd = -1;
        close(srv);
    });
}

void StopScreenServer() {
    g_running = false;
    int fd = g_serverFd.exchange(-1);
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    if (g_serverThread.joinable()) g_serverThread.join();
    std::lock_guard<std::mutex> l(g_mtx);
    for (auto& client : g_outs) close(client.fd);
    g_outs.clear();
}

}  // namespace fh
