#include "app/AppDiagnostics.h"

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define AVANTGARDE_HAS_EXECINFO 1
#else
#define AVANTGARDE_HAS_EXECINFO 0
#endif

namespace avantgarde {
namespace {

std::mutex gLogMutex{};
std::atomic<int> gLogFd{-1};
std::atomic<bool> gHandlersInstalled{false};

const char* levelToString(AppLogLevel level) noexcept {
    switch (level) {
        case AppLogLevel::Debug: return "DEBUG";
        case AppLogLevel::Info: return "INFO";
        case AppLogLevel::Warn: return "WARN";
        case AppLogLevel::Error: return "ERROR";
        case AppLogLevel::Fatal: return "FATAL";
        default: return "UNK";
    }
}

void writeRawLine(int fd, std::string_view text) noexcept {
    if (fd < 0) {
        return;
    }
    (void)::write(fd, text.data(), text.size());
    (void)::write(fd, "\n", 1);
}

void writeRawLineBoth(std::string_view text) noexcept {
    const int fd = gLogFd.load(std::memory_order_acquire);
    writeRawLine(fd, text);
    writeRawLine(STDERR_FILENO, text);
}

std::string timestampNow() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto tt = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    char buf[64]{};
    std::snprintf(buf,
                  sizeof(buf),
                  "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900,
                  tm.tm_mon + 1,
                  tm.tm_mday,
                  tm.tm_hour,
                  tm.tm_min,
                  tm.tm_sec,
                  static_cast<int>(ms.count()));
    return std::string(buf);
}

void dumpBacktraceTo(int fd) noexcept {
#if AVANTGARDE_HAS_EXECINFO
    std::array<void*, 128> trace{};
    const int n = ::backtrace(trace.data(), static_cast<int>(trace.size()));
    if (n > 0) {
        (void)::backtrace_symbols_fd(trace.data(), n, fd);
    }
#else
    (void)fd;
#endif
}

void crashSignalHandler(int sig) noexcept {
    const char* sigName = "UNKNOWN";
    switch (sig) {
        case SIGSEGV: sigName = "SIGSEGV"; break;
        case SIGABRT: sigName = "SIGABRT"; break;
        case SIGBUS: sigName = "SIGBUS"; break;
        case SIGILL: sigName = "SIGILL"; break;
        case SIGFPE: sigName = "SIGFPE"; break;
        case SIGTRAP: sigName = "SIGTRAP"; break;
        default: break;
    }

    char msg[256]{};
    std::snprintf(msg, sizeof(msg), "[CRASH][SIGNAL] received %s (%d)", sigName, sig);
    writeRawLineBoth(msg);
    const int fd = gLogFd.load(std::memory_order_acquire);
    dumpBacktraceTo(STDERR_FILENO);
    if (fd >= 0) {
        dumpBacktraceTo(fd);
    }
    std::_Exit(128 + sig);
}

void terminateHandler() noexcept {
    std::string reason = "unknown";
    if (const std::exception_ptr ep = std::current_exception(); ep) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& ex) {
            reason = ex.what();
        } catch (...) {
            reason = "non-std exception";
        }
    }

    std::string line = "[CRASH][TERMINATE] std::terminate called: " + reason;
    writeRawLineBoth(line);
    const int fd = gLogFd.load(std::memory_order_acquire);
    dumpBacktraceTo(STDERR_FILENO);
    if (fd >= 0) {
        dumpBacktraceTo(fd);
    }
    std::_Exit(134);
}

std::string makeDefaultLogPath() {
    return "logs/avantgarde.log";
}

} // namespace

bool AppDiagnostics::init(std::string path) {
    if (path.empty()) {
        path = makeDefaultLogPath();
    }
    namespace fs = std::filesystem;
    std::error_code ec{};
    const fs::path p(path);
    if (p.has_parent_path()) {
        (void)fs::create_directories(p.parent_path(), ec);
    }

    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        writeRawLineBoth("[LOG][WARN] failed to open log file, fallback to stderr only");
        return false;
    }

    const int oldFd = gLogFd.exchange(fd, std::memory_order_acq_rel);
    if (oldFd >= 0) {
        (void)::close(oldFd);
    }

    log(AppLogLevel::Info, std::string("log initialized: ") + path);
    return true;
}

void AppDiagnostics::installCrashHandlers() noexcept {
    if (gHandlersInstalled.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::set_terminate(terminateHandler);

    struct sigaction sa{};
    sa.sa_handler = crashSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    (void)sigaction(SIGSEGV, &sa, nullptr);
    (void)sigaction(SIGABRT, &sa, nullptr);
    (void)sigaction(SIGBUS, &sa, nullptr);
    (void)sigaction(SIGILL, &sa, nullptr);
    (void)sigaction(SIGFPE, &sa, nullptr);
    (void)sigaction(SIGTRAP, &sa, nullptr);
    log(AppLogLevel::Info, "crash handlers installed");
}

void AppDiagnostics::shutdown() noexcept {
    const int fd = gLogFd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        (void)::close(fd);
    }
}

void AppDiagnostics::log(AppLogLevel level, std::string_view message) noexcept {
    const std::lock_guard<std::mutex> lock(gLogMutex);
    const std::size_t tidHash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const std::string line = "[" + timestampNow() + "][T" + std::to_string(tidHash) + "][" +
                             levelToString(level) + "] " + std::string(message);
    const int fd = gLogFd.load(std::memory_order_acquire);
    if (fd >= 0) {
        writeRawLine(fd, line);
    }
    writeRawLine(STDERR_FILENO, line);
}

void AppDiagnostics::logf(AppLogLevel level, const char* fmt, ...) noexcept {
    if (!fmt) {
        return;
    }
    char buffer[2048]{};
    va_list args{};
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log(level, buffer);
}

} // namespace avantgarde
