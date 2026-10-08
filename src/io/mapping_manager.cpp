#include "io/mapping_manager.h"
#include "io/file_logger.h"
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <cmath>
#include "util/json.hpp"

namespace fh {


MappingManager& MappingManager::Get() {
    static MappingManager mgr;
    return mgr;
}

int MappingManager::AddKeyMapping(const KeyMapping& km) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_keyMappings.push_back(km);
    FH_LOG("INFO", "Mapping", "Added key: %s (code=%d)", km.name.c_str(), km.keyCode);
    return (int)m_keyMappings.size() - 1;
}

bool MappingManager::DeleteKeyMapping(int index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_keyMappings.size()) return false;
    FH_LOG("INFO", "Mapping", "Deleted key: %s", m_keyMappings[index].name.c_str());
    m_keyMappings.erase(m_keyMappings.begin() + index);
    return true;
}

KeyMapping* MappingManager::FindKeyMapping(uint16_t keyCode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (auto& km : m_keyMappings) {
        if (km.keyCode == keyCode) return &km;
    }
    return nullptr;
}

int MappingManager::AddJoystickMapping(const JoystickMapping& jm) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_joyMappings.size() >= 4) return -1;
    m_joyMappings.push_back(jm);
    FH_LOG("INFO", "Mapping", "Added joystick: %s", jm.name.c_str());
    return (int)m_joyMappings.size() - 1;
}

bool MappingManager::DeleteJoystickMapping(int index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (index < 0 || index >= (int)m_joyMappings.size()) return false;
    FH_LOG("INFO", "Mapping", "Deleted joystick: %s", m_joyMappings[index].name.c_str());
    m_joyMappings.erase(m_joyMappings.begin() + index);
    return true;
}

bool MappingManager::SaveToFile(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    try {
        nlohmann::json j;
        nlohmann::json keys = nlohmann::json::array();
        for (auto& km : m_keyMappings) {
            nlohmann::json k;
            k["name"] = km.name;
            k["keyCode"] = km.keyCode;
            k["x1"] = km.touchX1; k["y1"] = km.touchY1;
            k["x2"] = km.touchX2; k["y2"] = km.touchY2;
            k["radius"] = km.radius;
            k["isRange"] = km.isRange;
            k["eyeMode"] = km.eyeMode;
            keys.push_back(k);
        }
        j["keys"] = keys;

        nlohmann::json joys = nlohmann::json::array();
        for (auto& jm : m_joyMappings) {
            nlohmann::json v;
            v["name"] = jm.name;
            v["cx"] = jm.centerX; v["cy"] = jm.centerY;
            v["innerRadius"] = jm.innerRadius;
            v["middleRadius"] = jm.middleRadius;
            v["rectHeight"] = jm.rectHeight;
            v["speed"] = jm.speed;
            v["diagDrift"] = jm.diagDrift;
            v["spawnRadius"] = jm.spawnRadius;
            v["rangeScale"] = jm.rangeScale;
            v["keyUp"] = jm.keyUp; v["keyDown"] = jm.keyDown;
            v["keyLeft"] = jm.keyLeft; v["keyRight"] = jm.keyRight;
            v["sprintKey"] = jm.sprintKey; v["sprintHold"] = jm.sprintHold;
            v["sneakKey"] = jm.sneakKey; v["sneakHold"] = jm.sneakHold;
            joys.push_back(v);
        }
        j["joysticks"] = joys;

        nlohmann::json m;
        m["enabled"] = m_mouseConfig.enabled;
        m["sensitivity"] = m_mouseConfig.sensitivity;
        m["constrainedSensitivity"] = m_mouseConfig.constrainedSensitivity;
        m["gyroSensitivity"] = m_mouseConfig.gyroSensitivity;
        m["pointerSlot"] = m_mouseConfig.pointerSlot;
        m["pointerSize"] = m_mouseConfig.pointerSize;
        m["constrainedMode"] = m_mouseConfig.constrainedMode;
        m["gyroMode"] = m_mouseConfig.gyroMode;
        m["gyroShake"] = m_mouseConfig.gyroShake;
        m["gyroFlipX"] = m_mouseConfig.gyroFlipX;
        m["gyroFlipY"] = m_mouseConfig.gyroFlipY;
        m["constrainX1"] = m_mouseConfig.constrainX1;
        m["constrainY1"] = m_mouseConfig.constrainY1;
        m["constrainX2"] = m_mouseConfig.constrainX2;
        m["constrainY2"] = m_mouseConfig.constrainY2;
        m["sideBtn1"] = m_mouseConfig.sideBtn1;
        m["sideBtn2"] = m_mouseConfig.sideBtn2;
        m["modeSwitchKey"] = m_mouseConfig.modeSwitchKey;
        m["gyroSwitchKey"] = m_mouseConfig.gyroSwitchKey;
        m["flipX"] = m_mouseConfig.flipX;
        m["flipY"] = m_mouseConfig.flipY;
        j["mouse"] = m;

        j["overlayAlpha"] = m_overlayAlpha;
        j["keyRadius"] = m_keyRadius;

        nlohmann::json ui;
        ui["drawKeys"] = m_drawKeys;
        ui["drawJoy"] = m_drawJoy;
        ui["drawConstraint"] = m_drawConstraint;
        ui["antiRecord"] = m_antiRecord;
        ui["lastConfig"] = m_lastConfig;
        j["ui"] = ui;

        nlohmann::json sc;
        sc["streaming"] = m_screenConfig.streaming;
        sc["fullscreen"] = m_screenConfig.fullscreen;
        sc["captureW"] = m_screenConfig.captureW;
        sc["captureH"] = m_screenConfig.captureH;
        j["screen"] = sc;

        const std::string tmpPath = path + ".tmp";
        std::ofstream ofs(tmpPath, std::ios::trunc);
        if (!ofs) return false;
        ofs << j.dump(2);
        ofs.flush();
        if (!ofs) return false;
        ofs.close();
        if (std::rename(tmpPath.c_str(), path.c_str()) != 0) return false;
        FH_LOG("INFO", "Config", "Saved to %s", path.c_str());
        return true;
    } catch (...) {
        FH_LOG("ERROR", "Config", "Save failed");
        return false;
    }
}

bool MappingManager::LoadFromFile(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    try {
        std::ifstream ifs(path);
        if (!ifs) return false;
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (!j.is_object() || !j.contains("keys") || !j["keys"].is_array() ||
            !j.contains("joysticks") || !j["joysticks"].is_array()) return false;

        std::vector<KeyMapping> newKeys;
        std::vector<JoystickMapping> newJoys;
        MouseConfig newMouse = m_mouseConfig;
        ScreenConfig newScreen = m_screenConfig;
        float newAlpha = m_overlayAlpha;
        float newKeyRadius = m_keyRadius;
        bool newConstraintLoaded = false;
        bool newDrawKeys = m_drawKeys, newDrawJoy = m_drawJoy, newDrawConstraint = m_drawConstraint;
        bool newAntiRecord = m_antiRecord;
        std::string newLastConfig = m_lastConfig;
        for (auto& k : j["keys"]) {
            KeyMapping km;
            km.name = k["name"];
            km.keyCode = k["keyCode"];
            km.touchX1 = k["x1"]; km.touchY1 = k["y1"];
            km.touchX2 = k.value("x2", 0); km.touchY2 = k.value("y2", 0);
            km.radius = k.value("radius", 55);
            if (km.radius < 1 || km.radius > 2000) throw std::runtime_error("invalid key radius");
            km.isRange = k.value("isRange", false);
            km.eyeMode = k.value("eyeMode", false);
            newKeys.push_back(std::move(km));
        }

        if (j["joysticks"].size() > 4) throw std::runtime_error("too many joysticks");
        for (auto& v : j["joysticks"]) {
            JoystickMapping jm;
            jm.name = v["name"];
            jm.centerX = v["cx"]; jm.centerY = v["cy"];
            jm.innerRadius = v.value("innerRadius", 60);
            jm.middleRadius = v.value("middleRadius", 150);
            jm.rectHeight = v.value("rectHeight", 300);
            jm.speed = v.value("speed", 500.0f);
            jm.diagDrift = v.value("diagDrift", 20.0f);
            jm.spawnRadius = v.value("spawnRadius", 30);
            jm.rangeScale = v.value("rangeScale", 1.0f);
            jm.keyUp = v.value("keyUp", 0); jm.keyDown = v.value("keyDown", 0);
            jm.keyLeft = v.value("keyLeft", 0); jm.keyRight = v.value("keyRight", 0);
            jm.sprintKey = v.value("sprintKey", 0); jm.sprintHold = v.value("sprintHold", true);
            jm.sneakKey = v.value("sneakKey", 0); jm.sneakHold = v.value("sneakHold", true);
            if (jm.innerRadius < 1 || jm.middleRadius < jm.innerRadius || jm.rectHeight < 1 ||
                !std::isfinite(jm.speed) || jm.speed <= 0.0f || !std::isfinite(jm.diagDrift) ||
                jm.diagDrift < 0.0f || jm.spawnRadius < 0 || !std::isfinite(jm.rangeScale) ||
                jm.rangeScale <= 0.0f) throw std::runtime_error("invalid joystick config");
            newJoys.push_back(std::move(jm));
        }

        if (j.contains("mouse")) {
            auto& m = j["mouse"];
            newMouse.enabled = m.value("enabled", true);            newMouse.sensitivity = m.value("sensitivity", 2.0f);
            newMouse.constrainedSensitivity = m.value("constrainedSensitivity", 2.0f);
            newMouse.gyroSensitivity = m.value("gyroSensitivity", 2.0f);
            newMouse.pointerSlot = m.value("pointerSlot", 9);
            newMouse.pointerSize = m.value("pointerSize", 100.0f);
            newMouse.constrainedMode = m.value("constrainedMode", false);
            newMouse.gyroMode = m.value("gyroMode", false);
            newMouse.gyroShake = m.value("gyroShake", false);
            newMouse.gyroFlipX = m.value("gyroFlipX", false);
            newMouse.gyroFlipY = m.value("gyroFlipY", false);
            newMouse.constrainX1 = m.value("constrainX1", 0);
            newMouse.constrainY1 = m.value("constrainY1", 0);
            newMouse.constrainX2 = m.value("constrainX2", 1920);
            newMouse.constrainY2 = m.value("constrainY2", 1080);
            newConstraintLoaded = m.contains("constrainX1");
            newMouse.sideBtn1 = m.value("sideBtn1", 0);
            newMouse.sideBtn2 = m.value("sideBtn2", 0);
            newMouse.modeSwitchKey = m.value("modeSwitchKey", 0x2E);
            newMouse.gyroSwitchKey = m.value("gyroSwitchKey", 0);
            newMouse.flipX = m.value("flipX", false);
            newMouse.flipY = m.value("flipY", false);
            if (newMouse.pointerSlot < 0 || newMouse.pointerSlot >= 16 ||
                !std::isfinite(newMouse.pointerSize) || newMouse.pointerSize <= 0.0f ||
                !std::isfinite(newMouse.sensitivity) || newMouse.sensitivity <= 0.0f ||
                !std::isfinite(newMouse.constrainedSensitivity) || newMouse.constrainedSensitivity <= 0.0f ||
                !std::isfinite(newMouse.gyroSensitivity) || newMouse.gyroSensitivity <= 0.0f)
                throw std::runtime_error("invalid mouse config");
        }

        newAlpha = j.value("overlayAlpha", 0.7f);
        newKeyRadius = j.value("keyRadius", 55.0f);
        if (!std::isfinite(newAlpha) || newAlpha < 0.0f || newAlpha > 1.0f ||
            !std::isfinite(newKeyRadius) || newKeyRadius <= 0.0f || newKeyRadius > 2000.0f)
            throw std::runtime_error("invalid visual config");

        if (j.contains("ui")) {
            auto& ui = j["ui"];
            newDrawKeys = ui.value("drawKeys", true);
            newDrawJoy = ui.value("drawJoy", true);
            newDrawConstraint = ui.value("drawConstraint", true);
            newAntiRecord = ui.value("antiRecord", false);
            newLastConfig = ui.value("lastConfig", "");
        }

        if (j.contains("screen")) {
            auto& sc = j["screen"];
            newScreen.streaming = sc.value("streaming", false);
            newScreen.fullscreen = sc.value("fullscreen", true);
            newScreen.captureW = sc.value("captureW", 1280);
            newScreen.captureH = sc.value("captureH", 720);
            if (newScreen.captureW <= 0 || newScreen.captureH <= 0)
                throw std::runtime_error("invalid screen config");
        }

        m_keyMappings = std::move(newKeys);
        m_joyMappings = std::move(newJoys);
        m_mouseConfig = newMouse;
        m_screenConfig = newScreen;
        m_overlayAlpha = newAlpha;
        m_keyRadius = newKeyRadius;
        m_constraintLoaded = newConstraintLoaded;
        m_drawKeys = newDrawKeys;
        m_drawJoy = newDrawJoy;
        m_drawConstraint = newDrawConstraint;
        m_antiRecord = newAntiRecord;
        m_lastConfig = std::move(newLastConfig);

        FH_LOG("INFO", "Config", "Loaded from %s (%zu keys, %zu joys)", path.c_str(),
             m_keyMappings.size(), m_joyMappings.size());
        return true;
    } catch (...) {
        FH_LOG("ERROR", "Config", "Load failed from %s", path.c_str());
        return false;
    }
}

}  // namespace fh
