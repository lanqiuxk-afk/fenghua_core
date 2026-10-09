#include "capture/screen_cap_loader.h"
#include "io/file_logger.h"
// 20MB 的内嵌采集库 (libscreen_cap.so) 不在仓库里。
// 想启用这条采集路径: 用 tools/embed_binary.py 生成
//   src/capture/libscreen_cap_embedded.h
// 生成后 CMake 会自动定义 FH_HAVE_SCREEN_CAP_LIB 并编译下面的实现。
#ifdef FH_HAVE_SCREEN_CAP_LIB
#include "capture/libscreen_cap_embedded.h"
#endif

#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <android/log.h>

namespace fh {

#ifdef FH_HAVE_SCREEN_CAP_LIB


#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

typedef bool (*StartFn)(int, int, int, int, int, void*);
typedef void (*StopFn)();

// libselinux: 切换到 shell SELinux 域, 否则 SurfaceFlinger 拒绝采集
extern "C" int setcon(const char* context);

static void* g_lib = nullptr;
static StartFn g_start = nullptr;
static StopFn g_stop = nullptr;
static pid_t g_pid = -1;

static const char* kLibPath = "/data/adb/fenghua/libscreen_cap.so";
static const char* g_logPath = "/data/adb/fenghua/loader.log";

static void LogLine(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_ERROR, "ScreenLoader", "%s", buf);
    FILE* f = fopen(g_logPath, "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}

// 内存加载 libscreen_cap.so (memfd + dlopen)
static bool LoadFromMemory() {
    long fd = syscall(__NR_memfd_create, "libscreen_cap", MFD_CLOEXEC);
    if (fd < 0) {
        LogLine("memfd_create failed: %s", strerror(errno));
        return false;
    }
    size_t off = 0;
    while (off < kLibScreenCapSoLen) {
        ssize_t n = write((int)fd, kLibScreenCapSo + off, kLibScreenCapSoLen - off);
        if (n <= 0) { LogLine("memfd write failed"); close((int)fd); return false; }
        off += (size_t)n;
    }
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%ld", fd);
    void* lib = dlopen(path, RTLD_NOW);
    if (!lib) {
        LogLine("dlopen memfd failed: %s", dlerror());
        close((int)fd);
        return false;
    }
    LogLine("loaded from memory (memfd)");
    g_lib = lib;
    return true;
}

static int ConnectForwarder() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(56790);
    if (connect(fd, (struct sockaddr*)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}

// --cap 子进程入口 (干净进程, 由 fork+exec 启动)
void ScreenCapChildEntry(int cropW, int cropH, int dispW, int dispH) {
    // 子进程降权后写 /data/adb 会被 SELinux 拦, 日志改到 shell 可写的 /data/local/tmp
    g_logPath = "/data/local/tmp/cap.log";
    LogLine("child entry pid=%d (pre-setuid)", getpid());
    // cap.log 刚被 root 创建是 0644 root:root, setuid 后 shell 追加不了, 先放开
    chmod("/data/local/tmp/cap.log", 0666);
    mkdir("/data/adb/fenghua", 0755);
    chmod("/data/adb/fenghua", 0777);
    chmod("/data/adb/fenghua/loader.log", 0666);
    chmod("/data/adb/fenghua/screencap.log", 0666);
    LogLine("child step: before setuid");
    // 试 system uid(1000): SF 放行名单通常含 system
    int gr = setgid(1000);
    int ur = setuid(1000);
    LogLine("child pid=%d uid=%d gid=%d (setgid=%d setuid=%d)", getpid(), getuid(),
            getgid(), gr, ur);
    if (setcon("u:r:shell:s0") == 0) {
        LogLine("setcon to shell OK");
    } else {
        LogLine("setcon to shell failed: %s", strerror(errno));
    }
    // 读取实际生效的 SELinux 上下文
    FILE* cf = fopen("/proc/self/attr/current", "r");
    if (cf) {
        char ctx[128] = {};
        if (fgets(ctx, sizeof(ctx), cf)) LogLine("actual context: %s", ctx);
        fclose(cf);
    }

    LogLine("child step: loading so");
    if (!LoadFromMemory()) return;
    g_start = (StartFn)dlsym(g_lib, "ScreenCapStart");
    g_stop = (StopFn)dlsym(g_lib, "ScreenCapStop");
    if (!g_start || !g_stop) { LogLine("child dlsym failed"); return; }

    LogLine("child step: connect forwarder");
    int fd = ConnectForwarder();
    if (fd < 0) {
        LogLine("child: connect forwarder failed");
        return;
    }
    LogLine("child connected forwarder fd=%d", fd);
    LogLine("child step: ScreenCapStart");
    bool ok = g_start(cropW, cropH, dispW, dispH, fd, nullptr);
    LogLine("child ScreenCapStart=%d", ok ? 1 : 0);
    if (!ok) return;
    // 监控父 fenghua: 父进程退出后自动结束采集, 避免残留占端口
    pid_t parentPid = getppid();
    for (;;) {
        if (getppid() != parentPid) {
            LogLine("parent fenghua exited, capture child exiting");
            break;
        }
        static int tick = 0;
        if ((++tick % 5) == 0) LogLine("child alive tick");
        sleep(1);
    }
}

bool ScreenStart(int cropW, int cropH, int dispW, int dispH) {
    // 清理已退出的旧子进程
    if (g_pid > 0 && kill(g_pid, 0) != 0) g_pid = -1;

    // fork + exec 重新加载自己: 干净进程跑采集, 不带父进程 GPU/binder 状态
    pid_t pid = fork();
    if (pid < 0) { LogLine("fork failed: %s", strerror(errno)); return false; }
    if (pid == 0) {
        char w[16], h[16], dw[16], dh[16];
        snprintf(w, sizeof(w), "%d", cropW);
        snprintf(h, sizeof(h), "%d", cropH);
        snprintf(dw, sizeof(dw), "%d", dispW);
        snprintf(dh, sizeof(dh), "%d", dispH);
        execl("/proc/self/exe", "fenghua", "--cap", w, h, dw, dh, (char*)nullptr);
        _exit(127);  // exec 失败
    }
    g_pid = pid;
    LogLine("capture child pid=%d", pid);
    // 记录采集子进程 PID, 供下次启动 KillOldInstances 清理(子进程可能被隐藏)
    FILE* pf = fopen("/data/adb/fenghua/cap.pid", "w");
    if (pf) { fprintf(pf, "%d\n", (int)pid); fclose(pf); }
    // 短暂等待, 检测子进程是否立即退出(exec 失败/崩溃)
    usleep(300000);
    int wstatus = 0;
    pid_t r = waitpid(pid, &wstatus, WNOHANG);
    if (r == pid) {
        LogLine("child exited immediately: exitcode=%d signal=%d",
                WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1,
                WIFSIGNALED(wstatus) ? WTERMSIG(wstatus) : 0);
    } else if (r == 0) {
        LogLine("child alive after 300ms");
    }
    return true;
}

void ScreenStop() {
    if (g_pid > 0) {
        kill(g_pid, SIGTERM);
        waitpid(g_pid, nullptr, 0);
        g_pid = -1;
    }
    unlink("/data/adb/fenghua/cap.pid");
}


#else  // !FH_HAVE_SCREEN_CAP_LIB

// 没有内嵌采集库: 这条路径不可用。
// 它只在 Android 16 上用 SurfaceControl 直采时需要; 一般用 scrcpy-server 路径即可
// (--stream 默认就是走 scrcpy)。要启用请生成 libscreen_cap_embedded.h。

bool ScreenStart(int cropW, int cropH, int dispW, int dispH) {
    (void)cropW; (void)cropH; (void)dispW; (void)dispH;
    FH_LOG("WARN", "ScreenCap", "libscreen_cap 内嵌库未编译进来, 该采集路径不可用");
    return false;
}

void ScreenStop() {}

void ScreenCapChildEntry(int cropW, int cropH, int dispW, int dispH) {
    (void)cropW; (void)cropH; (void)dispW; (void)dispH;
}

#endif  // FH_HAVE_SCREEN_CAP_LIB

}  // namespace fh
