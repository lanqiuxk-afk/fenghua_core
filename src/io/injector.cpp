#include "io/injector.h"
#include "io/file_logger.h"
#include "io/mapping_manager.h"

namespace fh {

Injector& Injector::Get() {
    static Injector s;
    return s;
}

void Injector::SetDriver(IDriver* drv) { m_driver = drv; }

bool Injector::Init(int screenW, int screenH) {
    if (!m_driver || !m_driver->IsReady()) return false;
    m_screenW = screenW;
    m_screenH = screenH;
    // 驱动侧: X 短边域, Y 长边域
    bool ok = (m_screenW > m_screenH)
                  ? m_driver->TouchInit(m_screenH, m_screenW)
                  : m_driver->TouchInit(m_screenW, m_screenH);
    m_ready = ok;
    FH_LOG(ok ? "INFO" : "ERROR", "Injector", "TouchInit(%d, %d) -> %d",
         m_screenH, m_screenW, (int)ok);
    return ok;
}

void Injector::Release() { m_ready = false; }

void Injector::TouchDown(int slot, int x, int y) {
    if (!m_ready || !m_driver) return;
    auto mc = MappingManager::Get().GetMouseConfig();
    if (mc.flipX) x = m_screenW - 1 - x;
    if (mc.flipY) y = m_screenH - 1 - y;
    if (m_screenW > m_screenH) {
        // 横屏: 逻辑坐标 -> 面板竖屏坐标, 旋转 90 度
        m_driver->TouchDown(slot, m_screenH - 1 - y, x);
    } else {
        m_driver->TouchDown(slot, x, y);
    }
}

void Injector::TouchUp(int slot) {
    if (!m_ready || !m_driver) return;
    m_driver->TouchUp(slot);
}

void Injector::ReleaseAll(int maxSlot) {
    if (!m_driver) return;
    for (int s = 0; s < maxSlot; s++) m_driver->TouchUp(s);
}

}  // namespace fh
