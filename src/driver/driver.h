#ifndef FH_DRIVER_H
#define FH_DRIVER_H

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <sys/types.h>

namespace fh {

// ============================================================================
//  IDriver —— 本工程与"内核注入驱动"之间的唯一接缝。
//
//  本仓库不包含任何具体驱动的实现或对接代码, 只提供接口定义和一个可编译的
//  空实现 (StubDriver)。接入自己的驱动时:
//
//    1. 继承 IDriver, 实现需要的纯虚函数;
//    2. 在 src/driver/stub_driver.cpp 的 CreateDriver() 里注册;
//    3. 触摸注入的坐标语义见 TouchInit / TouchDown 注释 —— 这是整套映射的基准。
//
//  最小可用能力 = 多指触摸按下/移动/抬起;
//  陀螺仪、进程隐藏、内存读写都是可选项, 不支持就返回 false。
// ============================================================================
class IDriver {
public:
    virtual ~IDriver() = default;

    virtual const char* Name() const = 0;

    virtual bool Init() = 0;
    virtual void Release() = 0;
    virtual bool IsReady() const = 0;

    // ---- 触摸注入 (必须) ----
    // 坐标语义: X 走短边域, Y 走长边域, 即"横屏时的物理分辨率"。
    // 1080x2400 的机器横屏使用 -> TouchInit(1080, 2400)。
    virtual bool TouchInit(int short_edge, int long_edge) = 0;
    // slot: 0..15; x/y 为上面坐标系下的绝对像素
    virtual bool TouchDown(int slot, int x, int y) = 0;
    virtual bool TouchUp(int slot) = 0;

    // ---- 可选: 陀螺仪 ----
    virtual bool SupportsGyro() const { return false; }
    virtual bool GyroUpdate(float x, float y, uint32_t mask, bool enable) {
        (void)x; (void)y; (void)mask; (void)enable; return false;
    }

    // ---- 可选: 进程控制 ----
    virtual bool SupportsProcessControl() const { return false; }
    virtual bool TargetInit(pid_t pid) { (void)pid; return false; }
    virtual pid_t GetPid(const char* name) { (void)name; return -1; }
    virtual uintptr_t GetModuleBase(const char* name) { (void)name; return 0; }
    virtual bool IsProcessAlive(pid_t pid, int* alive_out) {
        (void)pid; if (alive_out) *alive_out = 0; return false;
    }
    virtual bool HideProcess(pid_t pid, bool hide) { (void)pid; (void)hide; return false; }
    virtual bool HidePath(const char* path, bool hide) { (void)path; (void)hide; return false; }

    // ---- 可选: 内存 ----
    virtual bool SupportsMemory() const { return false; }
    virtual bool ReadMemory(uintptr_t addr, void* buf, size_t size) {
        (void)addr; (void)buf; (void)size; return false;
    }
    virtual bool WriteMemory(uintptr_t addr, const void* buf, size_t size) {
        (void)addr; (void)buf; (void)size; return false;
    }

    template <typename T> T Read(uintptr_t addr) {
        T v{}; ReadMemory(addr, &v, sizeof(T)); return v;
    }
    template <typename T> bool Write(uintptr_t addr, T value) {
        return WriteMemory(addr, &value, sizeof(T));
    }
};

// 按名字创建驱动; 未注册的名字返回 nullptr。内置: "stub", "paradise"(需驱动库)。
std::unique_ptr<IDriver> CreateDriver(const std::string& name);

// 已注册的驱动名列表 (用于 --help / 报错提示)
std::string ListDrivers();

}  // namespace fh

#endif  // FH_DRIVER_H
