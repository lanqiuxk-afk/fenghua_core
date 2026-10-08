#include "io/direct_input.h"
#include "io/file_logger.h"
#include "io/injector.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace fh {


namespace {

constexpr size_t kBitsPerLong = sizeof(unsigned long) * 8;
#define FH_NBITS(x) (((x) + kBitsPerLong - 1) / kBitsPerLong)

bool HasBit(const unsigned long* bits, int bit) {
    return (bits[bit / kBitsPerLong] & (1UL << (bit % kBitsPerLong))) != 0;
}

int64_t NowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

uint16_t LinuxKeyToVk(uint16_t key) {
    if (key >= KEY_1 && key <= KEY_9) return (uint16_t)('1' + key - KEY_1);
    if (key >= KEY_F1 && key <= KEY_F10) return (uint16_t)(0x70 + key - KEY_F1);
    if (key >= KEY_F11 && key <= KEY_F12) return (uint16_t)(0x7A + key - KEY_F11);
    switch (key) {
        case KEY_0: return '0';
        case KEY_Q: return 'Q'; case KEY_W: return 'W'; case KEY_E: return 'E';
        case KEY_R: return 'R'; case KEY_T: return 'T'; case KEY_Y: return 'Y';
        case KEY_U: return 'U'; case KEY_I: return 'I'; case KEY_O: return 'O';
        case KEY_P: return 'P'; case KEY_A: return 'A'; case KEY_S: return 'S';
        case KEY_D: return 'D'; case KEY_F: return 'F'; case KEY_G: return 'G';
        case KEY_H: return 'H'; case KEY_J: return 'J'; case KEY_K: return 'K';
        case KEY_L: return 'L'; case KEY_Z: return 'Z'; case KEY_X: return 'X';
        case KEY_C: return 'C'; case KEY_V: return 'V'; case KEY_B: return 'B';
        case KEY_N: return 'N'; case KEY_M: return 'M';
        case KEY_ESC: return 0x1B; case KEY_BACKSPACE: return 0x08;
        case KEY_TAB: return 0x09; case KEY_ENTER: case KEY_KPENTER: return 0x0D;
        case KEY_SPACE: return 0x20; case KEY_CAPSLOCK: return 0x14;
        case KEY_LEFTSHIFT: return 0xA0; case KEY_RIGHTSHIFT: return 0xA1;
        case KEY_LEFTCTRL: return 0xA2; case KEY_RIGHTCTRL: return 0xA3;
        case KEY_LEFTALT: return 0xA4; case KEY_RIGHTALT: return 0xA5;
        case KEY_LEFTMETA: return 0x5B; case KEY_RIGHTMETA: return 0x5C;
        case KEY_MENU: case KEY_COMPOSE: return 0x5D;
        case KEY_HOME: return 0x24; case KEY_END: return 0x23;
        case KEY_PAGEUP: return 0x21; case KEY_PAGEDOWN: return 0x22;
        case KEY_INSERT: return 0x2D; case KEY_DELETE: return 0x2E;
        case KEY_LEFT: return 0x25; case KEY_UP: return 0x26;
        case KEY_RIGHT: return 0x27; case KEY_DOWN: return 0x28;
        case KEY_SYSRQ: return 0x2C; case KEY_PAUSE: return 0x13;
        case KEY_NUMLOCK: return 0x90; case KEY_SCROLLLOCK: return 0x91;
        case KEY_KP0: return 0x60; case KEY_KP1: return 0x61;
        case KEY_KP2: return 0x62; case KEY_KP3: return 0x63;
        case KEY_KP4: return 0x64; case KEY_KP5: return 0x65;
        case KEY_KP6: return 0x66; case KEY_KP7: return 0x67;
        case KEY_KP8: return 0x68; case KEY_KP9: return 0x69;
        case KEY_KPASTERISK: return 0x6A; case KEY_KPPLUS: return 0x6B;
        case KEY_KPMINUS: return 0x6D; case KEY_KPDOT: return 0x6E;
        case KEY_KPSLASH: return 0x6F;
        case KEY_SEMICOLON: return 0xBA; case KEY_EQUAL: return 0xBB;
        case KEY_COMMA: return 0xBC; case KEY_MINUS: return 0xBD;
        case KEY_DOT: return 0xBE; case KEY_SLASH: return 0xBF;
        case KEY_GRAVE: return 0xC0; case KEY_LEFTBRACE: return 0xDB;
        case KEY_BACKSLASH: return 0xDC; case KEY_RIGHTBRACE: return 0xDD;
        case KEY_APOSTROPHE: return 0xDE;
        default: return 0;
    }
}

int LinuxButtonToProtocol(uint16_t code) {
    switch (code) {
        case BTN_LEFT: return 0;
        case BTN_RIGHT: return 1;
        case BTN_MIDDLE: return 2;
        case BTN_SIDE: case BTN_BACK: return 4;
        case BTN_EXTRA: case BTN_FORWARD: return 5;
        default: return -1;
    }
}

struct Device {
    int fd = -1;
    std::string path;
    std::string name;
    bool keyboard = false;
    bool mouse = false;
    bool pressedVk[256] = {};
    bool pressedButton[6] = {};
    float dx = 0.0f;
    float dy = 0.0f;
    int wheel = 0;
    bool hideComplete = false;
    std::vector<std::string> hiddenPaths;
};

} // namespace

DirectInput& DirectInput::Get() {
    static DirectInput input;
    return input;
}

DirectInput::~DirectInput() { Stop(); }

bool DirectInput::Start(PacketCallback callback) {
    if (m_running.load()) return true;
    if (!callback) return false;
    m_callback = std::move(callback);
    m_running = true;
    try {
        m_thread = std::thread(&DirectInput::Loop, this);
    } catch (...) {
        m_running = false;
        return false;
    }
    return true;
}

void DirectInput::Stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    m_deviceCount = 0;
    m_hiddenCount = 0;
}

void DirectInput::Loop() {
    std::vector<Device> devices;
    int64_t lastScanMs = 0;

    auto sendKey = [this](uint16_t vk, bool pressed) {
        InputPacket packet{0x10, {(uint8_t)(vk & 0xFF), (uint8_t)(vk >> 8), (uint8_t)(pressed ? 1 : 0)}};
        m_callback(packet);
    };
    auto sendButton = [this](int button, bool pressed) {
        InputPacket packet{0x12, {(uint8_t)button, (uint8_t)(pressed ? 1 : 0)}};
        m_callback(packet);
    };
    auto sendMove = [this](float dx, float dy) {
        InputPacket packet;
        packet.type = 0x11;
        packet.payload.resize(8);
        memcpy(packet.payload.data(), &dx, 4);
        memcpy(packet.payload.data() + 4, &dy, 4);
        m_callback(packet);
    };
    auto sendWheel = [this](int wheel) {
        int16_t delta = (int16_t)std::max(-32768, std::min(32767, wheel));
        InputPacket packet{0x13, {(uint8_t)(delta & 0xFF), (uint8_t)((uint16_t)delta >> 8)}};
        m_callback(packet);
    };
    auto releaseDevice = [&](Device& device) {
        for (int vk = 1; vk < 256; ++vk)
            if (device.pressedVk[vk]) sendKey((uint16_t)vk, false);
        for (int button = 0; button < 6; ++button)
            if (device.pressedButton[button]) sendButton(button, false);
        for (auto it = device.hiddenPaths.rbegin(); it != device.hiddenPaths.rend(); ++it)
            Injector::Get().Driver()->HidePath(it->c_str(), false);
        if (device.fd >= 0) {
            ioctl(device.fd, EVIOCGRAB, 0);
            close(device.fd);
            device.fd = -1;
        }
    };
    auto publishCounts = [&]() {
        int hidden = 0;
        for (const auto& device : devices) if (device.hideComplete) ++hidden;
        m_deviceCount = (int)devices.size();
        m_hiddenCount = hidden;
        // (无 UI 版本: 设备数变化无需通知谁)
    };
    auto scan = [&]() {
        DIR* dir = opendir("/dev/input");
        if (!dir) return;
        std::vector<std::string> paths;
        for (dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
            if (strncmp(entry->d_name, "event", 5) == 0)
                paths.emplace_back(std::string("/dev/input/") + entry->d_name);
        }
        closedir(dir);
        std::sort(paths.begin(), paths.end());

        bool changed = false;
        for (const auto& path : paths) {
            bool alreadyOpen = false;
            for (const auto& device : devices)
                if (device.path == path) { alreadyOpen = true; break; }
            if (alreadyOpen) continue;

            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            unsigned long eventBits[FH_NBITS(EV_CNT)] = {};
            unsigned long keyBits[FH_NBITS(KEY_CNT)] = {};
            unsigned long relBits[FH_NBITS(REL_CNT)] = {};
            unsigned long absBits[FH_NBITS(ABS_CNT)] = {};
            ioctl(fd, EVIOCGBIT(0, sizeof(eventBits)), eventBits);
            if (HasBit(eventBits, EV_KEY)) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits);
            if (HasBit(eventBits, EV_REL)) ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relBits)), relBits);
            if (HasBit(eventBits, EV_ABS)) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits);

            bool keyboard = HasBit(eventBits, EV_KEY) && HasBit(keyBits, KEY_A) &&
                            HasBit(keyBits, KEY_Z) && HasBit(keyBits, KEY_SPACE);
            bool mouse = HasBit(eventBits, EV_KEY) && HasBit(eventBits, EV_REL) &&
                         HasBit(keyBits, BTN_LEFT) && HasBit(relBits, REL_X) && HasBit(relBits, REL_Y);
            bool touch = HasBit(eventBits, EV_ABS) &&
                         (HasBit(keyBits, BTN_TOUCH) ||
                          (HasBit(absBits, ABS_MT_POSITION_X) && HasBit(absBits, ABS_MT_POSITION_Y)));
            input_id id{};
            bool hasId = ioctl(fd, EVIOCGID, &id) >= 0;
            bool external = hasId && (id.bustype == BUS_USB || id.bustype == BUS_BLUETOOTH);
            if ((!keyboard && !mouse) || touch || !external) { close(fd); continue; }
            if (ioctl(fd, EVIOCGRAB, 1) < 0) {
                FH_LOG("WARN", "Direct", "EVIOCGRAB failed for %s: %s", path.c_str(), strerror(errno));
                close(fd);
                continue;
            }

            Device device;
            device.fd = fd;
            device.path = path;
            device.keyboard = keyboard;
            device.mouse = mouse;
            char name[256] = {};
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) device.name = name;
            else device.name = "input device";

            bool eventHidden = Injector::Get().Driver()->HidePath(path.c_str(), true);
            if (eventHidden) device.hiddenPaths.push_back(path);
            size_t slash = path.find_last_of('/');
            std::string eventName = slash == std::string::npos ? path : path.substr(slash + 1);
            std::string sysPath = std::string("/sys/class/input/") + eventName;
            char resolved[PATH_MAX] = {};
            bool hasResolvedPath = realpath(sysPath.c_str(), resolved) != nullptr;
            bool sysEventHidden = Injector::Get().Driver()->HidePath(sysPath.c_str(), true);
            if (sysEventHidden) device.hiddenPaths.push_back(sysPath);
            bool resolvedEventHidden = hasResolvedPath && Injector::Get().Driver()->HidePath(resolved, true);
            if (resolvedEventHidden) device.hiddenPaths.emplace_back(resolved);

            bool registrationHidden = sysEventHidden || resolvedEventHidden;
            if (hasResolvedPath) {
                std::string inputPath(resolved);
                size_t inputSlash = inputPath.find_last_of('/');
                if (inputSlash != std::string::npos) inputPath.resize(inputSlash);
                size_t nameSlash = inputPath.find_last_of('/');
                std::string inputName = nameSlash == std::string::npos ? inputPath : inputPath.substr(nameSlash + 1);
                std::string classInputPath = std::string("/sys/class/input/") + inputName;
                bool classInputHidden = Injector::Get().Driver()->HidePath(classInputPath.c_str(), true);
                if (classInputHidden) device.hiddenPaths.push_back(classInputPath);
                bool inputPathHidden = Injector::Get().Driver()->HidePath(inputPath.c_str(), true);
                if (inputPathHidden) device.hiddenPaths.push_back(inputPath);
                registrationHidden = registrationHidden && (classInputHidden || inputPathHidden);

                // Also hide the concrete HID interface backing this input node.
                // This remains per keyboard/mouse interface and never hides the
                // shared /dev/input or /sys/class/input directories.
                size_t hidEnd = inputPath.find("/input/input");
                if (hidEnd != std::string::npos) {
                    std::string hidPath = inputPath.substr(0, hidEnd);
                    bool hidPathHidden = Injector::Get().Driver()->HidePath(hidPath.c_str(), true);
                    if (hidPathHidden) device.hiddenPaths.push_back(hidPath);

                    size_t hidSlash = hidPath.find_last_of('/');
                    std::string hidName = hidSlash == std::string::npos ? hidPath : hidPath.substr(hidSlash + 1);
                    std::string classHidPath = std::string("/sys/bus/hid/devices/") + hidName;
                    bool classHidHidden = access(classHidPath.c_str(), F_OK) == 0 &&
                                          Injector::Get().Driver()->HidePath(classHidPath.c_str(), true);
                    if (classHidHidden) device.hiddenPaths.push_back(classHidPath);
                    registrationHidden = registrationHidden && hidPathHidden;
                }
            }
            device.hideComplete = eventHidden && registrationHidden;

            FH_LOG("INFO", "Direct", "Captured %s (%s%s), hidden=%d", device.name.c_str(),
                 keyboard ? "keyboard" : "", mouse ? (keyboard ? "+mouse" : "mouse") : "",
                 device.hideComplete ? 1 : 0);
            devices.push_back(std::move(device));
            changed = true;
        }
        if (changed) publishCounts();
    };

    FH_LOG("INFO", "Direct", "Direct keyboard/mouse mode started");
    while (m_running.load()) {
        if (NowMs() - lastScanMs >= 1000) {
            lastScanMs = NowMs();
            scan();
        }

        std::vector<pollfd> pollFds;
        pollFds.reserve(devices.size());
        for (const auto& device : devices) pollFds.push_back({device.fd, POLLIN, 0});
        int result = poll(pollFds.empty() ? nullptr : pollFds.data(), pollFds.size(), 200);
        if (result < 0 && errno != EINTR) usleep(50000);

        bool changed = false;
        for (int index = (int)devices.size() - 1; index >= 0; --index) {
            Device& device = devices[index];
            short revents = pollFds.empty() ? 0 : pollFds[index].revents;
            bool remove = (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (revents & POLLIN) {
                input_event event{};
                for (;;) {
                    ssize_t size = read(device.fd, &event, sizeof(event));
                    if (size == (ssize_t)sizeof(event)) {
                        if (event.type == EV_KEY) {
                            if (event.value == 2) continue;
                            int button = device.mouse ? LinuxButtonToProtocol(event.code) : -1;
                            if (button >= 0) {
                                bool pressed = event.value != 0;
                                device.pressedButton[button] = pressed;
                                sendButton(button, pressed);
                            } else if (device.keyboard) {
                                uint16_t vk = LinuxKeyToVk(event.code);
                                if (vk) {
                                    bool pressed = event.value != 0;
                                    device.pressedVk[vk] = pressed;
                                    sendKey(vk, pressed);
                                }
                            }
                        } else if (event.type == EV_REL && device.mouse) {
                            if (event.code == REL_X) device.dx += (float)event.value;
                            else if (event.code == REL_Y) device.dy += (float)event.value;
                            else if (event.code == REL_WHEEL) device.wheel += event.value;
                        } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
                            if (device.dx != 0.0f || device.dy != 0.0f) sendMove(device.dx, device.dy);
                            if (device.wheel != 0) sendWheel(device.wheel);
                            device.dx = device.dy = 0.0f;
                            device.wheel = 0;
                        }
                        continue;
                    }
                    if (size == 0 || (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                        remove = true;
                    break;
                }
            }
            if (remove) {
                FH_LOG("INFO", "Direct", "Released disconnected device %s", device.name.c_str());
                releaseDevice(device);
                devices.erase(devices.begin() + index);
                changed = true;
            }
        }
        if (changed) publishCounts();
    }

    for (auto& device : devices) releaseDevice(device);
    devices.clear();
    publishCounts();
    FH_LOG("INFO", "Direct", "Direct keyboard/mouse mode stopped");
}

}  // namespace fh
