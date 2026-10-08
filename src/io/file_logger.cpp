#include "io/file_logger.h"

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

namespace fh {

FileLogger& FileLogger::Get() {
    static FileLogger logger;
    return logger;
}

void FileLogger::Init(const std::string& logDir) {
    m_logDir = logDir;
    if (mkdir(m_logDir.c_str(), 0755) != 0 && errno != EEXIST) {
        std::string cmd = "mkdir -p " + m_logDir + " 2>/dev/null";
        if (system(cmd.c_str()) != 0) {
            // 落盘会失败, 但内存日志仍然可用
        }
    }
}

void FileLogger::Write(const char* level, const char* tag, const char* fmt, ...) {
    if (!m_enabled) return;
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_buf.push_back(std::string("[") + ts + "] [" + level + "] [" + tag + "] " + msg);
    while (m_buf.size() > kMaxBuf) m_buf.erase(m_buf.begin());
}

size_t FileLogger::Size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buf.size();
}

void FileLogger::DumpToFile() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_buf.empty()) return;
    if (m_logPath.empty()) m_logPath = MakeLogPath();
    FILE* fp = fopen(m_logPath.c_str(), "w");
    if (!fp) return;
    for (const auto& s : m_buf) {
        fwrite(s.data(), 1, s.size(), fp);
        fputc('\n', fp);
    }
    fclose(fp);
}

std::string FileLogger::MakeLogPath() {
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tmv);
    return m_logDir + "/fenghua_" + ts + ".log";
}

}  // namespace fh
