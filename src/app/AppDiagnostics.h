#pragma once

#include <string>
#include <string_view>

namespace avantgarde {

// Уровень сообщения в диагностическом логе приложения.
enum class AppLogLevel : unsigned char {
    Debug = 0,
    Info,
    Warn,
    Error,
    Fatal
};

// Централизованный слой диагностики:
// - запись структурированных логов в файл;
// - установка crash/terminate обработчиков;
// - безопасная фиксация контекста при редких падениях.
class AppDiagnostics final {
public:
    // Инициализирует файловый лог. Если path пустой, берется дефолт:
    // "logs/avantgarde.log".
    static bool init(std::string path = {});

    // Устанавливает обработчики std::terminate и POSIX сигналов (SIGSEGV и т.д.).
    static void installCrashHandlers() noexcept;

    // Освобождает файловый дескриптор лога.
    static void shutdown() noexcept;

    // Базовая запись готового сообщения.
    static void log(AppLogLevel level, std::string_view message) noexcept;

    // Форматированная запись.
    static void logf(AppLogLevel level, const char* fmt, ...) noexcept;

    // Включить/выключить дублирование логов в STDERR.
    // Полезно для framebuffer-режима (rpi-wrapper), где вывод в консоль
    // визуально конфликтует с отрисовкой на /dev/fb0.
    static void setStderrEnabled(bool enabled) noexcept;
};

} // namespace avantgarde
