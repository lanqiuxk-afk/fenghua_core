#ifndef DIRECT_INPUT_H
#define DIRECT_INPUT_H

#include "io/tcp_server.h"
#include <atomic>
#include <functional>
#include <thread>

namespace fh {


class DirectInput {
public:
    using PacketCallback = std::function<void(const InputPacket&)>;

    static DirectInput& Get();
    ~DirectInput();

    bool Start(PacketCallback callback);
    void Stop();
    int DeviceCount() const { return m_deviceCount.load(); }
    int HiddenCount() const { return m_hiddenCount.load(); }

private:
    DirectInput() = default;
    void Loop();

    std::atomic<bool> m_running{false};
    std::atomic<int> m_deviceCount{0};
    std::atomic<int> m_hiddenCount{0};
    PacketCallback m_callback;
    std::thread m_thread;
};


}  // namespace fh

#endif
