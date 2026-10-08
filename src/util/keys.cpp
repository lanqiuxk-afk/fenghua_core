#include "util/keys.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace fh {

std::string KeyName(uint16_t vk) {
    if (vk >= 0x41 && vk <= 0x5A) { char s[2] = {(char)vk, 0}; return s; }
    if (vk >= 0x30 && vk <= 0x39) { char s[2] = {(char)vk, 0}; return s; }
    if (vk >= 0x60 && vk <= 0x69) { char s[2] = {(char)(vk - 0x30), 0}; return std::string("Num") + s; }
    if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x6F);
    switch (vk) {
        case 0x0D: return "Enter";  case 0x1B: return "Esc";   case 0x08: return "Bksp";
        case 0x09: return "Tab";    case 0x20: return "Space";
        case 0x10: case 0xA0: case 0xA1: return "Shift";
        case 0x11: case 0xA2: case 0xA3: return "Ctrl";
        case 0x12: case 0xA4: case 0xA5: return "Alt";
        case 0x14: return "Caps";
        case 0x25: return "Left";   case 0x26: return "Up";
        case 0x27: return "Right";  case 0x28: return "Down";
        case 0x2D: return "Ins";    case 0x2E: return "Del";
        case 0x21: return "PgUp";   case 0x22: return "PgDn";
        case 0x23: return "End";    case 0x24: return "Home";
        case 0x2C: return "PrtSc";  case 0x13: return "Pause";
        case 0x5B: return "LWin";   case 0x5C: return "RWin";  case 0x5D: return "Menu";
        case 0xBA: return ";";      case 0xBB: return "=";     case 0xBC: return ",";
        case 0xBD: return "-";      case 0xBE: return ".";     case 0xBF: return "/";
        case 0xC0: return "`";      case 0xDB: return "[";     case 0xDC: return "\\";
        case 0xDD: return "]";      case 0xDE: return "'";
        case 0x01: return "MouseLeft";   case 0x02: return "MouseRight";
        case 0x04: return "MouseMiddle"; case 0x05: return "MouseX1"; case 0x06: return "MouseX2";
        case 0x88: return "WheelUp";     case 0x89: return "WheelDown";
        default: return "VK" + std::to_string(vk);
    }
}

uint16_t MouseButtonKeyCode(int button) {
    switch (button) {
        case 0: return 0x01;  // left
        case 1: return 0x02;  // right
        case 2: return 0x04;  // middle
        case 4: return 0x05;  // X1
        case 5: return 0x06;  // X2
        default: return 0;
    }
}

std::string GetDeviceIP() {
    char buf[128];
    FILE* fp = popen("ip addr show wlan0 2>/dev/null | grep 'inet ' | awk '{print $2}' | cut -d/ -f1", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            char* nl = strchr(buf, '\n'); if (nl) *nl = 0;
            pclose(fp);
            if (strlen(buf) > 6 && strncmp(buf, "127.", 4) != 0) return buf;
        } else {
            pclose(fp);
        }
    }
    fp = popen("ip addr show 2>/dev/null | grep -E 'inet [0-9]+' | grep -v 127.0.0.1 "
               "| awk '{print $2}' | cut -d/ -f1 | head -1", "r");
    if (fp) {
        if (fgets(buf, sizeof(buf), fp)) {
            char* nl = strchr(buf, '\n'); if (nl) *nl = 0;
            pclose(fp);
            if (buf[0]) return buf;
        } else {
            pclose(fp);
        }
    }
    return "0.0.0.0";
}

int64_t NowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

}  // namespace fh
