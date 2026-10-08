#ifndef MAPPING_MANAGER_H
#define MAPPING_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>

namespace fh {


struct KeyMapping {
    std::string name;
    uint16_t keyCode = 0;
    int touchX1 = 0, touchY1 = 0;
    int radius = 55;
    int touchX2 = 0, touchY2 = 0;
    bool isRange = false;
    bool eyeMode = false;   // 小眼睛: hold to drag camera from this key
};

struct JoystickMapping {
    std::string name;
    int centerX = 960, centerY = 540;
    int innerRadius = 60;      // sneak range
    int middleRadius = 150;    // walk range
    int rectHeight = 300;      // sprint rectangle height (forward of center)
    float speed = 500.0f;      // slide speed from center (px/sec)
    float diagDrift = 20.0f;   // max diagonal angular drift (degrees), 0 = fixed 45°
    int spawnRadius = 30;      // 中心随机落点半径(px): 起手触摸点在该半径内随机偏移, 0=关闭
    float rangeScale = 1.0f;   // 范围大小整体缩放: 内/中/疾跑矩形半径统一乘这个系数
    uint16_t keyUp = 0;
    uint16_t keyDown = 0;
    uint16_t keyLeft = 0;
    uint16_t keyRight = 0;
    uint16_t sprintKey = 0;
    uint16_t sneakKey = 0;
    bool sprintHold = true;    // true=hold to sprint, false=toggle
    bool sneakHold = true;     // true=hold to sneak, false=toggle
};

struct MouseConfig {
    bool enabled = true;
    float sensitivity = 2.0f;             // 自由模式灵敏度
    float constrainedSensitivity = 2.0f;  // 约束模式灵敏度 (独立)
    float gyroSensitivity = 2.0f;         // 陀螺仪灵敏度 (独立)
    int pointerSlot = 9;
    float pointerSize = 100.0f;
    bool constrainedMode = false;
    bool gyroMode = false;   // gyroscope mode: PC mouse drives the gyro
    bool gyroShake = false;  // 伪人手: small random shake when idle (anti-detection)
    bool gyroFlipX = false;  // 陀螺仪独立翻转 X
    bool gyroFlipY = false;  // 陀螺仪独立翻转 Y
    bool flipX = false;  // invert X axis (指针光标)
    bool flipY = false;  // invert Y axis
    int constrainX1 = 0, constrainY1 = 0, constrainX2 = 1920, constrainY2 = 1080;
    uint16_t modeSwitchKey = 0x2E;  // default Del (0x2E)
    uint16_t gyroSwitchKey = 0;     // separate key to toggle gyro mode on/off
    uint16_t sideBtn1 = 0;
    uint16_t sideBtn2 = 0;
};

struct ScreenConfig {
    bool streaming = false;      // 投屏开关
    bool fullscreen = true;      // 全屏截取
    int captureW = 1280;         // 截取宽度(以屏幕中心为中心)
    int captureH = 720;          // 截取高度
};

class MappingManager {
public:
    static MappingManager& Get();

    // Screen capture (投屏)
    void SetScreenConfig(const ScreenConfig& sc) { m_screenConfig = sc; }
    const ScreenConfig& GetScreenConfig() const { return m_screenConfig; }

    // Key mappings
    int AddKeyMapping(const KeyMapping& km);
    bool DeleteKeyMapping(int index);
    const std::vector<KeyMapping>& GetKeyMappings() const { return m_keyMappings; }
    std::vector<KeyMapping>& GetKeyMappingsMutable() { return m_keyMappings; }
    KeyMapping* FindKeyMapping(uint16_t keyCode);

    // Joystick mappings
    int AddJoystickMapping(const JoystickMapping& jm);
    bool DeleteJoystickMapping(int index);
    const std::vector<JoystickMapping>& GetJoystickMappings() const { return m_joyMappings; }
    std::vector<JoystickMapping>& GetJoystickMappingsMutable() { return m_joyMappings; }

    // Visual settings
    float GetAlpha() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_overlayAlpha; }
    void SetAlpha(float a) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_overlayAlpha = a; }
    float GetKeyRadius() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_keyRadius; }
    void SetKeyRadius(float r) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_keyRadius = r; }

    // 绘制选项 (持久化到配置)
    bool GetDrawKeys() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_drawKeys; }
    void SetDrawKeys(bool v) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_drawKeys = v; }
    bool GetDrawJoy() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_drawJoy; }
    void SetDrawJoy(bool v) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_drawJoy = v; }
    bool GetDrawConstraint() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_drawConstraint; }
    void SetDrawConstraint(bool v) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_drawConstraint = v; }
    bool GetAntiRecord() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_antiRecord; }
    void SetAntiRecord(bool v) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_antiRecord = v; }

    // 上次使用的配置名 (自动加载)
    std::string GetLastConfig() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_lastConfig; }
    void SetLastConfig(const std::string& name) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_lastConfig = name; }

    // Mouse
    void SetMouseConfig(const MouseConfig& mc) { std::lock_guard<std::recursive_mutex> l(m_mutex); m_mouseConfig = mc; }
    MouseConfig GetMouseConfig() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_mouseConfig; }
    std::recursive_mutex& GetMutex() const { return m_mutex; }
    float m_mouseX = 960, m_mouseY = 540;
    // 配置文件是否提供过约束范围 (避免启动默认值覆盖已保存的约束框)
    bool HasLoadedConstraint() const { std::lock_guard<std::recursive_mutex> l(m_mutex); return m_constraintLoaded; }

    // Save/Load
    bool SaveToFile(const std::string& path);
    bool LoadFromFile(const std::string& path);

private:
    MappingManager() = default;
    std::vector<KeyMapping> m_keyMappings;
    std::vector<JoystickMapping> m_joyMappings;
    MouseConfig m_mouseConfig;
    ScreenConfig m_screenConfig;
    float m_overlayAlpha = 0.7f;
    float m_keyRadius = 55.0f;
    bool m_drawKeys = true;
    bool m_drawJoy = true;
    bool m_drawConstraint = true;
    bool m_antiRecord = false;
    bool m_constraintLoaded = false;
    std::string m_lastConfig;
    mutable std::recursive_mutex m_mutex;
};


}  // namespace fh

#endif
