// include/contracts/IAudioRecorder.h
#pragma once
#include <string>
#include <cstdint>
#include <vector>

namespace avantgarde {

// Конфигурация записи
    struct RecordConfig {
        int sampleRate = 48000;
        int channels   = 2;     // 1=mono, 2=stereo (non-interleaved в RT)
        int bitDepth   = 24;    // физический файл: 16/24/32f
        std::string format = "wav"; // "wav" | "flac" (MVP: "wav")
    };

// RT-часть: вызывается из аудио-рендера. Никаких аллокаций/исключений/блокировок.
    struct IRtRecordSink {
        virtual ~IRtRecordSink() = default;

        // Пишем один блок неинтерливнутых каналов: [channels][nframes]
        // Возвращает false если внутренний кольцевой буфер переполнен (дроп кадра допустим).
        virtual bool writeBlock(const float* const* ch, int nframes) noexcept = 0;

        // Опционально — отметка тактов/локаторов (без формата файла, просто события)
        virtual void mark(uint32_t code) noexcept = 0;
    };

// Non-RT контроллер: управляет файлом и потоками записи.
    struct IAudioRecorder {
        virtual ~IAudioRecorder() = default;

        // Подготовка/открытие файла. Создаёт внутренний предвыделенный ringbuffer.
        // Путь обычно даёт IProjectStore (папка проекта).
        virtual bool start(const std::string& filePath, const RecordConfig& cfg) = 0;

        // Остановка и финализация контейнера (запись заголовков, flush).
        virtual void stop() = 0;

        virtual bool isRecording() const noexcept = 0;

        // Доступ к RT-синку. Получаем один раз после start() и кэшируем в RT.
        virtual IRtRecordSink* rtSink() noexcept = 0;

        // Статистика/диагностика (вне RT)
        virtual uint64_t totalFramesWritten() const noexcept = 0;
        virtual uint64_t droppedBlocks() const noexcept = 0;
    };

} // namespace avantgarde
