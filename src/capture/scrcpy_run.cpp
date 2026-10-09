#include "capture/scrcpy_run.h"
#include "capture/scrcpy_server_embedded.h"
#include "capture/screen_stream.h"
#include "io/file_logger.h"
#include <android/log.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

namespace fh {


extern "C" int setcon(const char* context);

static std::atomic<bool> g_run{false};
static pthread_t g_thread = 0;
static std::atomic<pid_t> g_serverPid{-1};
static std::atomic<int> g_listenFd{-1};
static const char* kJarPath = "/data/local/tmp/scrcpy-server.jar";
static const char* kSocketName = "scrcpy";
static int g_cropW = 640, g_cropH = 400, g_dispW = 0, g_dispH = 0;

static const uint8_t kMagic[4] = {'F', 'H', 'S', 'C'};

static void LogLine(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "FenghuaScrcpy", "%s", buf);
    FH_LOG("INFO", "Scrcpy", "%s", buf);
}

static bool WriteJar() {
    FILE* f = fopen(kJarPath, "wb");
    if (!f) {
        LogLine("WriteJar failed: %s", strerror(errno));
        return false;
    }
    size_t written = fwrite(kScrcpyServerJar, 1, kScrcpyServerJarLen, f);
    fclose(f);
    chmod(kJarPath, 0644);
    LogLine("jar written %zu/%u bytes", written, kScrcpyServerJarLen);
    return written == kScrcpyServerJarLen;
}

// 关闭子进程继承的父进程 fd(防止 binder/EGL fd 干扰 app_process/JVM)
static void CloseInheritedFds() {
    DIR* d = opendir("/proc/self/fd");
    if (!d) return;
    int dirfd_ = dirfd(d);
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        int fd = atoi(ent->d_name);
        if (fd > STDERR_FILENO && fd != dirfd_) {
            close(fd);
        }
    }
    closedir(d);
}

// 创建 localabstract:"scrcpy" 监听 socket, 供 scrcpy-server(tunnel_forward=false) 连接
static int CreateListenSocket() {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, kSocketName, sizeof(addr.sun_path) - 2);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(kSocketName));
    if (bind(fd, (struct sockaddr*)&addr, len) < 0) { close(fd); return -1; }
    if (listen(fd, 4) < 0) { close(fd); return -1; }
    return fd;
}

// fork + 降权到 shell + exec app_process 启动 scrcpy server
static pid_t LaunchServer(int cropW, int cropH, int dispW, int dispH, int maxSize, int bitrate) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        CloseInheritedFds();

        // scrcpy-server 日志丢弃, 不写文件
        int logfd = open("/dev/null", O_WRONLY);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            if (logfd > STDERR_FILENO) close(logfd);
        }
        close(STDIN_FILENO);
        open("/dev/null", O_RDONLY);

        // 先降 UID, 再切 SELinux 域: setcon 后进程丢失 CAP_SETUID, setuid 会 EPERM
        // setcon 权限看域不看 uid(还在 su 域时有权切到 shell 域), 所以先 setuid 不影响 setcon
        if (setgid(2000) != 0 || setuid(2000) != 0) {
            fprintf(stderr, "setuid/setgid failed: %s\n", strerror(errno));
            _exit(126);
        }
        if (setcon("u:r:shell:s0") != 0) {
            fprintf(stderr, "setcon failed: %s\n", strerror(errno));
            _exit(126);
        }

        setenv("CLASSPATH", kJarPath, 1);
        char crop[64], maxs[32], brate[32];
        // scrcpy 对奇数旋转(横屏)显示会把 crop 转置(宽高互换), 所以 crop 必须按
        // 竖屏(自然方向)坐标传: width=横屏cropH, height=横屏cropW, x=横屏中心y, y=横屏中心x
        if (cropW >= dispW && cropH >= dispH) {
            strcpy(crop, "crop=");  // 全屏: 空值=不裁剪, 避免转置后超界
        } else {
            int xl = (dispW - cropW) / 2;
            int yl = (dispH - cropH) / 2;
            if (xl < 0) xl = 0;
            if (yl < 0) yl = 0;
            snprintf(crop, sizeof(crop), "crop=%d:%d:%d:%d", cropH, cropW, yl, xl);
        }
        snprintf(maxs, sizeof(maxs), "max_size=%d", maxSize);
        snprintf(brate, sizeof(brate), "video_bit_rate=%d", bitrate);

        execl("/system/bin/app_process", "app_process",
              "/system/bin",
              "com.genymobile.scrcpy.Server",
              "3.3.4",
              "video=true", "audio=false", "control=false",
              "tunnel_forward=false",
              crop, maxs, brate,
              "video_codec=h264",
              "max_fps=90",
              "send_device_meta=false",
              "send_dummy_byte=false",
              "send_codec_meta=true",
              "send_frame_meta=true",
              // Keep device-specific encoder defaults. Some MediaCodec
              // implementations emit codec-config and then terminate when
              // max-b-frames/i-frame-interval are forced here.
              "log_level=info",
              (char*)nullptr);
        fprintf(stderr, "exec app_process failed: %s\n", strerror(errno));
        _exit(127);
    }
    return pid;
}

// 若数据是 AVCC(4字节长度前缀)则转 Annex-B(起始码); 已是 Annex-B 原样返回 true
static bool EnsureAnnexB(const uint8_t* data, size_t len, std::vector<uint8_t>* out) {
    out->clear();
    bool isAnnexB = (len >= 4 && data[0] == 0 && data[1] == 0 &&
                     (data[2] == 1 || (data[2] == 0 && data[3] == 1)));
    if (isAnnexB) {
        out->assign(data, data + len);
        return true;
    }
    for (int lengthBytes : {4, 3, 2, 1}) {
        std::vector<uint8_t> candidate;
        size_t i = 0;
        while (i + (size_t)lengthBytes <= len) {
            uint32_t n = 0;
            for (int j = 0; j < lengthBytes; ++j)
                n = (n << 8) | data[i + j];
            if (n == 0 || i + lengthBytes + n > len) break;
            candidate.insert(candidate.end(), {0, 0, 0, 1});
            candidate.insert(candidate.end(), data + i + lengthBytes,
                             data + i + lengthBytes + n);
            i += lengthBytes + n;
        }
        if (i == len && !candidate.empty()) {
            *out = std::move(candidate);
            return true;
        }
    }
    return false;
}

static int ReadFully(int fd, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static void* RunLoop(void*) {
    if (!g_run.load()) return nullptr;
    if (!WriteJar()) {
        g_run = false;
        return nullptr;
    }
    int lfd = CreateListenSocket();
    if (lfd < 0) {
        LogLine("CreateListenSocket failed: %s", strerror(errno));
        g_run = false;
        return nullptr;
    }
    if (!g_run.load()) {
        close(lfd);
        g_run = false;
        return nullptr;
    }
    g_listenFd.store(lfd);

    // Keep the requested capture size. The PC uses software decoding, so do
    // not silently resize high-resolution full-screen captures to 1920.
    int maxSize;
    if (g_cropW >= g_dispW && g_cropH >= g_dispH) {
        int longest = (g_dispW > g_dispH) ? g_dispW : g_dispH;
        maxSize = longest;
        if (maxSize <= 0) maxSize = 1920;
    } else {
        maxSize = (g_cropW > g_cropH) ? g_cropW : g_cropH;
        if (maxSize <= 0) maxSize = 1920;
    }
    // 码率按像素面积分配, 保证大裁剪区域清晰 (1904²≈3.6MP 需要 ~30Mbps 才不发糊)
    int bitrate = 12000000;
    long long area = (long long)g_cropW * g_cropH;
    if (area >= 3000000) bitrate = 32000000;        // ≥3MP (如 1904×1904): 32M
    else if (area >= 1800000) bitrate = 26000000;   // ~1.8-3MP: 26M
    else if (area >= 900000) bitrate = 20000000;    // ~1080p 级: 20M
    else if (area >= 450000) bitrate = 15000000;    // ~720p 级: 15M
    else bitrate = 10000000;
    LogLine("crop=%d:%d disp=%dx%d max_size=%d bitrate=%d",
            g_cropW, g_cropH, g_dispW, g_dispH, maxSize, bitrate);
    g_serverPid.store(LaunchServer(g_cropW, g_cropH, g_dispW, g_dispH, maxSize, bitrate));
    LogLine("server pid=%d", (int)g_serverPid.load());
    if (g_serverPid.load() < 0) {
        close(lfd);
        g_listenFd = -1;
        g_run = false;
        return nullptr;
    }

    // 检测 execl 是否立即失败(exec 失败会 _exit(127))
    usleep(300000);
    int wstatus = 0;
    pid_t r = waitpid(g_serverPid.load(), &wstatus, WNOHANG);
    if (r == g_serverPid.load()) {
        LogLine("server exited immediately exit=%d sig=%d",
                WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1,
                WIFSIGNALED(wstatus) ? WTERMSIG(wstatus) : 0);
        close(lfd);
        g_listenFd = -1;
        g_serverPid = -1;
        g_run = false;
        return nullptr;
    }

    int cfd = accept(lfd, nullptr, nullptr);
    close(lfd);
    g_listenFd = -1;
    if (cfd < 0) {
        LogLine("accept failed: %s", strerror(errno));
        pid_t spid = g_serverPid.exchange(-1);
        if (spid > 0) { kill(spid, SIGKILL); waitpid(spid, nullptr, 0); }
        g_run = false;
        return nullptr;
    }
    LogLine("video connection accepted");

    // scrcpy 视频头: [codec_id:4][width:4][height:4], 大端
    uint8_t hdr[12];
    if (ReadFully(cfd, hdr, 12) < 0) {
        LogLine("read video header failed");
        close(cfd);
        pid_t spid = g_serverPid.exchange(-1);
        if (spid > 0) { kill(spid, SIGKILL); waitpid(spid, nullptr, 0); }
        g_run = false;
        return nullptr;
    }
    int codecId = (hdr[0] << 24) | (hdr[1] << 16) | (hdr[2] << 8) | hdr[3];
    int w = (hdr[4] << 24) | (hdr[5] << 16) | (hdr[6] << 8) | hdr[7];
    int h = (hdr[8] << 24) | (hdr[9] << 16) | (hdr[10] << 8) | hdr[11];
    LogLine("video header codec=0x%08x %dx%d", codecId, w, h);

    // scrcpy 错误码: 0=显式禁用流, 1=配置错误
    if (codecId == 0) {
        LogLine("scrcpy disabled stream");
    } else if (codecId == 1) {
        LogLine("scrcpy configuration error");
    }
    if (codecId == 0 || codecId == 1 || w <= 0 || h <= 0) {
        close(cfd);
        pid_t spid = g_serverPid.exchange(-1);
        if (spid > 0) { kill(spid, SIGKILL); waitpid(spid, nullptr, 0); }
        g_run = false;
        return nullptr;
    }

    // 发送 FHSC 流头; 后续数据为 [u32BE frame length][Annex-B H.264 AU].
    uint8_t fhsc[16];
    memcpy(fhsc, kMagic, 4);
    uint32_t ww = (uint32_t)w, hh = (uint32_t)h;
    fhsc[4] = (uint8_t)((ww >> 24) & 0xFF); fhsc[5] = (uint8_t)((ww >> 16) & 0xFF);
    fhsc[6] = (uint8_t)((ww >> 8) & 0xFF);  fhsc[7] = (uint8_t)(ww & 0xFF);
    fhsc[8] = (uint8_t)((hh >> 24) & 0xFF); fhsc[9] = (uint8_t)((hh >> 16) & 0xFF);
    fhsc[10] = (uint8_t)((hh >> 8) & 0xFF); fhsc[11] = (uint8_t)(hh & 0xFF);
    fhsc[12] = fhsc[13] = fhsc[14] = fhsc[15] = 0;
    ScreenStreamBegin();
    ScreenStreamPush(fhsc, sizeof(fhsc));

    uint8_t meta[12];
    int packetLogCount = 0;
    while (g_run.load()) {
        // scrcpy 视频包: [ptsAndFlags:8][packetSize:4][data]
        if (ReadFully(cfd, meta, 12) < 0) {
            LogLine("frame meta read failed errno=%d (%s)", errno, strerror(errno));
            break;
        }
        uint32_t size = ((uint32_t)meta[8] << 24) | ((uint32_t)meta[9] << 16) |
                        ((uint32_t)meta[10] << 8) | meta[11];
        if (size == 0 || size > (8 << 20)) {
            LogLine("bad packet size %u", size);
            break;
        }
        std::vector<uint8_t> pkt(size);
        if (ReadFully(cfd, pkt.data(), size) < 0) {
            LogLine("frame payload read failed size=%u errno=%d (%s)", size, errno, strerror(errno));
            break;
        }
        if (packetLogCount < 8) {
            uint64_t ptsFlags = 0;
            for (int i = 0; i < 8; ++i) ptsFlags = (ptsFlags << 8) | meta[i];
            LogLine("packet[%d] size=%u flags=0x%016llx bytes=%02x %02x %02x %02x",
                    packetLogCount++, size, (unsigned long long)ptsFlags,
                    pkt.size() > 0 ? pkt[0] : 0, pkt.size() > 1 ? pkt[1] : 0,
                    pkt.size() > 2 ? pkt[2] : 0, pkt.size() > 3 ? pkt[3] : 0);
        }

        // 已是 Annex-B 直接转发; 若是 AVCC 则转成 Annex-B.
        std::vector<uint8_t> conv;
        const uint8_t* out = pkt.data();
        size_t outLen = pkt.size();
        if (size >= 4 && !(pkt[0] == 0 && pkt[1] == 0 &&
                           (pkt[2] == 1 || (pkt[2] == 0 && pkt[3] == 1)))) {
            if (EnsureAnnexB(pkt.data(), pkt.size(), &conv)) {
                static int warned = 0;
                if (!warned) {
                    LogLine("note: AVCC -> Annex-B converted");
                    warned = 1;
                }
                out = conv.data();
                outLen = conv.size();
            }
        }
        // Preserve the original scrcpy packet stream. The PC side performs
        // access-unit assembly from the Annex-B NAL boundaries.
        ScreenStreamPush(out, outLen);
    }

    LogLine("stream ended");
    close(cfd);
    pid_t spid = g_serverPid.exchange(-1);
    if (spid > 0) {
        kill(spid, SIGKILL);
        int endStatus = 0;
        waitpid(spid, &endStatus, 0);
        LogLine("scrcpy stopped exit=%d signal=%d",
                WIFEXITED(endStatus) ? WEXITSTATUS(endStatus) : -1,
                WIFSIGNALED(endStatus) ? WTERMSIG(endStatus) : 0);
    }
    g_run = false;
    return nullptr;
}

bool ScrcpyStart(int cropW, int cropH, int dispW, int dispH) {
    if (g_run.exchange(true)) return true;
    if (g_thread) {
        pthread_join(g_thread, nullptr);
        g_thread = 0;
    }
    // cropW/cropH 是调用方给的采集区域, dispW/dispH 是显示尺寸。
    // scrcpy 侧按横屏处理(宽>=高), 这里先把 disp 规范为横屏, 再钳制 crop。
    bool fullScreen = (cropW >= dispW && cropH >= dispH);
    if (dispW < dispH) {
        int t = dispW;
        dispW = dispH;
        dispH = t;
    }
    if (fullScreen) {
        cropW = dispW;
        cropH = dispH;
    }
    if (cropW <= 0 || cropW > dispW) cropW = dispW;
    if (cropH <= 0 || cropH > dispH) cropH = dispH;
    g_cropW = cropW;
    g_cropH = cropH;
    g_dispW = dispW;
    g_dispH = dispH;
    if (pthread_create(&g_thread, nullptr, RunLoop, nullptr) != 0) {
        g_run = false;
        return false;
    }
    return true;
}

void ScrcpyStop() {
    g_run = false;
    int lfd = g_listenFd.exchange(-1);
    if (lfd >= 0) close(lfd); // 解除 accept 阻塞

    pid_t spid = g_serverPid.exchange(-1);
    if (spid > 0) {
        kill(spid, SIGKILL);
        waitpid(spid, nullptr, WNOHANG);
    }
    if (g_thread) {
        pthread_join(g_thread, nullptr);
        g_thread = 0;
    }
    if (spid > 0) waitpid(spid, nullptr, 0); // 确保收尸
}

}  // namespace fh
