// ============================================================================
//  Fenghua core (headless)
//
//  把 PC 键鼠映射到 Android 触摸的接收端骨架:
//      收包 (UDP)  ->  按键/摇杆/指针映射  ->  触摸注入 (内核驱动)
//
//  无界面: 启动即服务。内核驱动通过 src/driver/driver.h 的接口接入;
//  本仓库只带 stub 示例 (不注入) 与 paradise 参考实现 (需自备驱动库)。
// ============================================================================
#include "core/engine.h"
#include "driver/driver.h"
#include "io/file_logger.h"
#include "io/injector.h"
#include "io/mapping_manager.h"
#include "util/keys.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void OnSignal(int) { g_running = false; }

void PrintUsage(const char* argv0) {
    printf(
        "usage: %s [options]\n"
        "  --driver <name>  注入驱动 (默认 stub; 可用: %s)\n"
        "  --port <n>       UDP 监听端口 (默认 56789)\n"
        "  --screen <WxH>   逻辑分辨率, 按横屏给出 (默认读 wm size)\n"
        "  --config <path>  配置 json (默认 /data/adb/fenghua/mappings.json)\n"
        "  --logdir <path>  日志目录 (默认 /data/adb/fenghua)\n"
        "  --quiet          关闭日志\n"
        "  --help\n",
        argv0, fh::ListDrivers().c_str());
}

// 物理分辨率 -> 横屏逻辑尺寸 (长边为宽)
bool QueryScreenSize(int& w, int& h) {
    FILE* fp = popen("wm size 2>/dev/null | tail -1", "r");
    if (!fp) return false;
    char buf[128] = {};
    bool got = fgets(buf, sizeof(buf), fp) != nullptr;
    pclose(fp);
    if (!got) return false;
    int a = 0, b = 0;
    if (sscanf(buf, "Physical size: %dx%d", &a, &b) != 2) return false;
    if (a <= 0 || b <= 0) return false;
    w = a > b ? a : b;
    h = a > b ? b : a;
    return true;
}

// 摇杆/陀螺仪的固定节拍 (120Hz): 与 PC 端事件率解耦, 手感稳定
void JoyLoop() {
    constexpr int kIntervalUs = 8000;
    auto next = std::chrono::steady_clock::now();
    while (g_running.load()) {
        fh::Engine::Get().JoyTick();
        next += std::chrono::microseconds(kIntervalUs);
        std::this_thread::sleep_until(next);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string driverName = "stub";
    std::string configPath;
    std::string logDir = "/data/adb/fenghua";
    int port = 56789;
    int screenW = 0, screenH = 0;
    bool quiet = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto nextArg = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { printf("[!] %s needs a value\n", what); exit(2); }
            return argv[++i];
        };
        if (a == "--driver")      driverName = nextArg("--driver");
        else if (a == "--port")   port = atoi(nextArg("--port"));
        else if (a == "--config") configPath = nextArg("--config");
        else if (a == "--logdir") logDir = nextArg("--logdir");
        else if (a == "--quiet")  quiet = true;
        else if (a == "--help" || a == "-h") { PrintUsage(argv[0]); return 0; }
        else if (a == "--screen") {
            const char* s = nextArg("--screen");
            int w = 0, h = 0;
            if (sscanf(s, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
                printf("[!] --screen expects WxH, e.g. 2400x1080\n");
                return 2;
            }
            screenW = w > h ? w : h;
            screenH = w > h ? h : w;
        } else {
            printf("[!] unknown option: %s\n", a.c_str());
            PrintUsage(argv[0]);
            return 2;
        }
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);
    signal(SIGPIPE, SIG_IGN);

    fh::FileLogger::Get().Init(logDir);
    if (quiet) fh::FileLogger::Get().SetEnabled(false);

    printf("============================================================\n");
    printf("  Fenghua core (headless)   driver=%s  port=%d\n", driverName.c_str(), port);
    printf("============================================================\n");

    // ---- 驱动 ----
    auto driver = fh::CreateDriver(driverName);
    if (!driver) {
        printf("[!] unknown driver: %s (available: %s)\n",
               driverName.c_str(), fh::ListDrivers().c_str());
        return 1;
    }
    if (!driver->Init()) {
        printf("[!] driver %s init failed\n", driverName.c_str());
        FH_LOG("ERROR", "Main", "driver %s init failed", driverName.c_str());
        return 1;
    }
    fh::Injector::Get().SetDriver(driver.get());
    FH_LOG("INFO", "Main", "driver %s ready", driverName.c_str());

    // ---- 屏幕 ----
    if (screenW <= 0 || screenH <= 0) {
        if (!QueryScreenSize(screenW, screenH)) {
            screenW = 1920;
            screenH = 1080;
            FH_LOG("WARN", "Main", "screen size unknown, fallback %dx%d", screenW, screenH);
        }
    }
    printf("  screen (landscape): %d x %d\n", screenW, screenH);
    FH_LOG("INFO", "Main", "screen %dx%d", screenW, screenH);

    if (!fh::Injector::Get().Init(screenW, screenH)) {
        printf("[!] touch init failed (driver has no TouchInit?)\n");
        FH_LOG("ERROR", "Main", "injector init failed");
        return 1;
    }

    // ---- 引擎 ----
    if (!fh::Engine::Get().Start(port, configPath)) {
        printf("[!] start failed (udp:%d already in use?)\n", port);
        return 1;
    }

    auto& mgr = fh::MappingManager::Get();
    printf("  config: %zu key mappings, %zu joysticks\n",
           mgr.GetKeyMappings().size(), mgr.GetJoystickMappings().size());
    printf("  device ip: %s\n", fh::GetDeviceIP().c_str());
    printf("  put this ip into the PC sender. Ctrl+C to quit.\n\n");

    std::thread joy(JoyLoop);
    joy.detach();

    int64_t lastDump = fh::NowMs();
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        int64_t now = fh::NowMs();
        if (now - lastDump > 10000) {   // 每 10 秒落一次日志, 便于事后回看
            lastDump = now;
            fh::FileLogger::Get().DumpToFile();
        }
    }

    printf("\nshutting down...\n");
    fh::Engine::Get().Stop();
    fh::Injector::Get().Release();
    driver->Release();
    fh::FileLogger::Get().DumpToFile();
    printf("done. logs: %s/fenghua_*.log\n", logDir.c_str());
    return 0;
}
