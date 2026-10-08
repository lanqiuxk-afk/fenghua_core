#include "driver/driver.h"
#include "io/file_logger.h"

namespace fh {

// ============================================================================
//  StubDriver —— 空实现 (最简模板)
//
//  它不注入任何东西, 只把每次注入请求写进日志。用途:
//    - 在没有任何内核驱动的设备上也能编译/启动/联调协议与配置;
//    - 作为实现自己驱动时的最小参照。
//
//  真正要注入时, 让 TouchDown/TouchUp 把 (slot, x, y) 写进你的内核通道。
// ============================================================================
class StubDriver : public IDriver {
public:
    const char* Name() const override { return "stub"; }

    bool Init() override {
        FH_LOG("INFO", "Stub", "stub driver ready (no real injection). "
                             "Implement IDriver to inject touches.");
        m_ready = true;
        return true;
    }
    void Release() override { m_ready = false; }
    bool IsReady() const override { return m_ready; }

    bool TouchInit(int shortEdge, int longEdge) override {
        m_x = shortEdge;
        m_y = longEdge;
        FH_LOG("INFO", "Stub", "TouchInit short=%d long=%d", shortEdge, longEdge);
        return true;
    }
    bool TouchDown(int slot, int x, int y) override {
        static int logged = 0;
        if (logged < 20) { logged++; FH_LOG("DBG", "Stub", "TouchDown slot=%d x=%d y=%d", slot, x, y); }
        return true;
    }
    bool TouchUp(int slot) override {
        static int logged = 0;
        if (logged < 20) { logged++; FH_LOG("DBG", "Stub", "TouchUp slot=%d", slot); }
        return true;
    }

private:
    bool m_ready = false;
    int m_x = 0, m_y = 0;
};

// 由 src/driver/paradise_driver.cpp 提供 (没有驱动库时返回 nullptr)
std::unique_ptr<IDriver> CreateParadiseDriver();

std::unique_ptr<IDriver> CreateDriver(const std::string& name) {
    if (name == "stub") return std::unique_ptr<IDriver>(new StubDriver());
    if (name == "paradise") {
        if (auto d = CreateParadiseDriver()) return d;
        FH_LOG("WARN", "Driver", "paradise driver not linked in, falling back to stub");
        return std::unique_ptr<IDriver>(new StubDriver());
    }
    return nullptr;
}

std::string ListDrivers() { return "stub, paradise"; }

}  // namespace fh
