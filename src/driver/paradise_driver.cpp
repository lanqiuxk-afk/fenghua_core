#include "driver/driver.h"
#include "io/file_logger.h"

// ============================================================================
//  Paradise 驱动 —— 参考实现 (reference only)
//
//  本仓库只提供"接口 + 调用示例", 不附带任何驱动对接文件或驱动本体。
//  想让下面的实现真正生效, 需要你自己准备:
//
//    1) include/paradise_api.h                    驱动头文件 (本仓库放了一份原版供参考)
//    2) libs/<abi>/libparadise_api.a               驱动静态库 (由驱动作者提供)
//
//  CMake 配置阶段会检测这两个文件:
//    存在   -> 定义 FH_HAVE_PARADISE_LIB, 编译下面的实现
//    不存在 -> CreateParadiseDriver() 返回 nullptr, 程序用 --driver stub 照常运行
//
//  换成别的驱动: 照抄本文件改一个 IDriver 子类, 在 CreateDriver() 里注册即可。
// ============================================================================

#ifdef FH_HAVE_PARADISE_LIB

#include "paradise_api.h"
#include <memory>

namespace fh {
namespace {

class ParadiseDriver : public IDriver {
public:
    const char* Name() const override { return "paradise"; }

    bool Init() override {
        try {
            m_driver = std::make_unique<paradise_driver>();
            m_ready = true;
            FH_LOG("INFO", "Paradise", "driver initialized");
            return true;
        } catch (...) {
            FH_LOG("ERROR", "Paradise", "driver init failed");
            m_ready = false;
            return false;
        }
    }
    void Release() override {
        m_driver.reset();
        m_ready = false;
    }
    bool IsReady() const override { return m_ready && m_driver != nullptr; }

    // 坐标语义: TouchInit 传 (短边, 长边); TouchDown 的 x 属于短边域, y 属于长边域。
    bool TouchInit(int shortEdge, int longEdge) override {
        if (!IsReady()) return false;
        return m_driver->touch_init(shortEdge, longEdge);
    }
    bool TouchDown(int slot, int x, int y) override {
        if (!IsReady()) return false;
        return m_driver->touch_down(slot, x, y);
    }
    bool TouchUp(int slot) override {
        if (!IsReady()) return false;
        return m_driver->touch_up(slot);
    }

    bool SupportsGyro() const override { return true; }
    bool GyroUpdate(float x, float y, uint32_t mask, bool enable) override {
        if (!IsReady()) return false;
        return m_driver->gyro_update(x, y, mask, enable);
    }

    bool SupportsProcessControl() const override { return true; }
    bool TargetInit(pid_t pid) override {
        if (!IsReady()) return false;
        try { m_driver->initialize(pid); return true; } catch (...) { return false; }
    }
    pid_t GetPid(const char* name) override {
        if (!IsReady()) return -1;
        return m_driver->get_pid(name);
    }
    uintptr_t GetModuleBase(const char* name) override {
        if (!IsReady()) return 0;
        return m_driver->get_module_base(name);
    }
    bool IsProcessAlive(pid_t pid, int* alive_out) override {
        if (!IsReady()) return false;
        return m_driver->is_process_alive(pid, alive_out);
    }
    bool HideProcess(pid_t pid, bool hide) override {
        if (!IsReady()) return false;
        return m_driver->hide_process(pid, hide);
    }
    bool HidePath(const char* path, bool hide) override {
        if (!IsReady()) return false;
        return m_driver->hide_path(path, hide);
    }

    bool SupportsMemory() const override { return true; }
    bool ReadMemory(uintptr_t addr, void* buf, size_t size) override {
        if (!IsReady()) return false;
        return m_driver->read(addr, buf, size);
    }
    bool WriteMemory(uintptr_t addr, const void* buf, size_t size) override {
        if (!IsReady()) return false;
        return m_driver->write(addr, const_cast<void*>(buf), size);
    }

private:
    std::unique_ptr<paradise_driver> m_driver;
    bool m_ready = false;
};

}  // namespace

std::unique_ptr<IDriver> CreateParadiseDriver() {
    return std::unique_ptr<IDriver>(new ParadiseDriver());
}

}  // namespace fh

#else  // !FH_HAVE_PARADISE_LIB

namespace fh {
std::unique_ptr<IDriver> CreateParadiseDriver() {
    // 没有驱动库: 返回空, CreateDriver() 会决定是否回退到 stub。
    // 需要时把驱动头文件与静态库放好, CMake 会自动打开 FH_HAVE_PARADISE_LIB。
    return nullptr;
}
}  // namespace fh

#endif  // FH_HAVE_PARADISE_LIB
