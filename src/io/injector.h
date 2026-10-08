#ifndef FH_IO_INJECTOR_H
#define FH_IO_INJECTOR_H

#include "driver/driver.h"

namespace fh {

// ============================================================================
//  Injector —— 上层唯一允许调用的注入入口。
//
//  职责: 把"逻辑屏幕坐标"(横屏, 原点左上, 宽=长边) 换算成驱动需要的坐标,
//  并应用用户配置里的 flipX/flipY。业务代码只关心逻辑坐标。
// ============================================================================
class Injector {
public:
    static Injector& Get();

    void SetDriver(IDriver* drv);
    IDriver* Driver() { return m_driver; }

    // 初始化; 屏幕按横屏给出 (w = 长边, h = 短边)
    bool Init(int screenW, int screenH);
    void Release();
    bool IsReady() const { return m_ready && m_driver && m_driver->IsReady(); }

    int ScreenW() const { return m_screenW; }
    int ScreenH() const { return m_screenH; }

    void TouchDown(int slot, int x, int y);
    void TouchUp(int slot);

    // 全部抬指 (断线/退出/切模式时清理, 防止手指残留按下)
    void ReleaseAll(int maxSlot = 16);

private:
    Injector() = default;

    IDriver* m_driver = nullptr;
    int m_screenW = 1920, m_screenH = 1080;
    bool m_ready = false;
};

}  // namespace fh

#endif  // FH_IO_INJECTOR_H
