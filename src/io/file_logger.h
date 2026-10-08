#ifndef FH_IO_FILE_LOGGER_H
#define FH_IO_FILE_LOGGER_H

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace fh {

// ============================================================================
//  FileLogger —— 进程内环形日志缓冲。
//
//  设计取向: 高频路径 (收包/注入) 绝不做磁盘 IO。
//  Write() 只把一行推入内存环; 想落盘时显式 DumpToFile(),
//  或让后台线程按周期调用。缓冲满后丢最早的, 不会无限增长。
// ============================================================================
class FileLogger {
public:
    static FileLogger& Get();

    void Init(const std::string& logDir);
    void SetEnabled(bool on) { m_enabled = on; }
    bool Enabled() const { return m_enabled; }

    // 把当前缓冲整份写入 fenghua_YYYYmmdd_HHMMSS.log
    void DumpToFile();

    void Write(const char* level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    size_t Size() const;

private:
    FileLogger() = default;
    ~FileLogger() = default;
    std::string MakeLogPath();

    std::string m_logDir = "./fenghua";
    std::string m_logPath;
    mutable std::mutex m_mutex;
    std::vector<std::string> m_buf;
    bool m_enabled = true;
    static const size_t kMaxBuf = 4000;
};

}  // namespace fh

// 发布版想彻底关掉日志, 把 FLOG 定义成 ((void)0) 即可
#ifndef FH_LOG
#define FH_LOG(level, tag, fmt, ...) ::fh::FileLogger::Get().Write(level, tag, fmt, ##__VA_ARGS__)
#endif

#endif  // FH_IO_FILE_LOGGER_H
