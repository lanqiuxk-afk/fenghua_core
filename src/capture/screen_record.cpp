#include "capture/screen_record.h"
#include "capture/screen_stream.h"
#include "io/file_logger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <cerrno>
#include <cstring>
#include <atomic>

namespace fh {


static std::atomic<bool> g_active{false};
static pthread_t g_thread = 0;
static volatile int g_pid = -1;
static int g_w = 1280, g_h = 720;
static const char* kFifo = "/data/local/tmp/fh_screen.fifo";

static const uint8_t kMagic[4] = {'F', 'H', 'S', 'C'};

// spawn screenrecord 输出到 FIFO; 返回子进程 pid, 失败返回 -1
static int spawnScreenRecord() {
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // screenrecord 的状态信息大多走 stdout, 错误走 stderr, 统一写日志便于诊断
        int logfd = open("/data/adb/fenghua/screenrecord.log",
                         O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (logfd >= 0) {
            dup2(logfd, 1);
            dup2(logfd, 2);
            if (logfd > 2) close(logfd);
        }
        char size[64], brate[64];
        snprintf(size, sizeof(size), "--size=%dx%d", g_w, g_h);
        snprintf(brate, sizeof(brate), "--bit-rate=%d", 16000000);  // 16Mbps 提升画质
        execl("/system/bin/screenrecord", "screenrecord",
              "--output-format=h264", size, brate, "--time-limit=1800",
              kFifo, (char*)nullptr);
        execlp("screenrecord", "screenrecord",
               "--output-format=h264", size, brate, "--time-limit=1800",
               kFifo, (char*)nullptr);
        _exit(127);
    }
    return pid;
}

// 发送 16 字节流头: "FHSC" + width(u32 BE) + height(u32 BE) + flags(u32 BE)
static void sendStreamHeader() {
    uint8_t hdr[16];
    memcpy(hdr, kMagic, 4);
    uint32_t w = (uint32_t)g_w, h = (uint32_t)g_h;
    hdr[4] = (uint8_t)((w >> 24) & 0xFF);
    hdr[5] = (uint8_t)((w >> 16) & 0xFF);
    hdr[6] = (uint8_t)((w >> 8) & 0xFF);
    hdr[7] = (uint8_t)(w & 0xFF);
    hdr[8] = (uint8_t)((h >> 24) & 0xFF);
    hdr[9] = (uint8_t)((h >> 16) & 0xFF);
    hdr[10] = (uint8_t)((h >> 8) & 0xFF);
    hdr[11] = (uint8_t)(h & 0xFF);
    hdr[12] = hdr[13] = hdr[14] = hdr[15] = 0;
    ScreenStreamBegin();
    ScreenStreamPush(hdr, sizeof(hdr));
}

static void* recordLoop(void*) {
    while (g_active) {
        g_pid = spawnScreenRecord();
        if (g_pid < 0) {
            FH_LOG("ERROR", "ScreenRec", "spawn screenrecord failed: %s", strerror(errno));
            usleep(2000000);
            continue;
        }
        FH_LOG("INFO", "ScreenRec", "spawned pid=%d", g_pid);
        int fd = open(kFifo, O_RDONLY);
        if (fd < 0) {
            FH_LOG("ERROR", "ScreenRec", "open fifo failed: %s", strerror(errno));
            kill(g_pid, SIGTERM);
            waitpid(g_pid, nullptr, 0);
            g_pid = -1;
            usleep(1000000);
            continue;
        }
        FH_LOG("INFO", "ScreenRec", "screenrecord connected fifo, reading %dx%d", g_w, g_h);
        sendStreamHeader();

        char buf[16384];
        ssize_t n;
        uint64_t total = 0;
        uint32_t ticks = 0;
        bool firstLog = true;
        while (g_active && (n = read(fd, buf, sizeof(buf))) > 0) {
            total += (uint64_t)n;
            ScreenStreamPush((const uint8_t*)buf, (size_t)n);
            if (firstLog) {
                firstLog = false;
                FH_LOG("INFO", "ScreenRec", "first data: %zd bytes", n);
            }
            if (++ticks >= 200) {  // 约每 5 秒报一次吞吐
                ticks = 0;
                FH_LOG("INFO", "ScreenRec", "pushed %llu KB, alive=%d",
                     (unsigned long long)(total / 1024),
                     (g_pid > 0 && kill(g_pid, 0) == 0) ? 1 : 0);
            }
        }
        close(fd);
        int wstatus = 0;
        if (g_pid > 0) {
            kill(g_pid, SIGTERM);
            waitpid(g_pid, &wstatus, 0);
        }
        bool exited = WIFEXITED(wstatus);
        int code = exited ? WEXITSTATUS(wstatus) : 0;
        bool signaled = WIFSIGNALED(wstatus);
        int sig = signaled ? WTERMSIG(wstatus) : 0;
        FH_LOG("INFO", "ScreenRec", "read end, got %llu KB; child exit=%d code=%d sig=%d, restarting...",
             (unsigned long long)(total / 1024), exited ? 1 : 0, code, sig);
        g_pid = -1;
        usleep(500000);
    }
    return nullptr;
}

bool StartScreenRecord(int width, int height) {
    if (g_active) return true;
    // H.264 要求宽高为偶数, 奇数会编码失败(如 658x655)
    g_w = (width > 0 ? width : 1280) & ~1;
    g_h = (height > 0 ? height : 720) & ~1;
    FH_LOG("INFO", "ScreenRec", "capture size rounded to %dx%d", g_w, g_h);
    unlink(kFifo);
    if (mkfifo(kFifo, 0666) < 0 && errno != EEXIST) {
        FH_LOG("ERROR", "ScreenRec", "mkfifo failed: %s", strerror(errno));
        return false;
    }
    g_active = true;
    if (pthread_create(&g_thread, nullptr, recordLoop, nullptr) != 0) {
        g_active = false;
        unlink(kFifo);
        return false;
    }
    return true;
}

void StopScreenRecord() {
    g_active = false;
    if (g_pid > 0) kill(g_pid, SIGTERM);
    if (g_thread) {
        pthread_join(g_thread, nullptr);
        g_thread = 0;
    }
    if (g_pid > 0) { waitpid(g_pid, nullptr, WNOHANG); g_pid = -1; }
    unlink(kFifo);
    FH_LOG("INFO", "ScreenRec", "stopped");
}

}  // namespace fh
