#pragma once

#include <cstdint>
#include <memory>

#include "types.h"

namespace avantgarde {

/**
 * @brief Регион проигрывания в фреймах исходного буфера.
 *
 * [startFrame, endFrame) — полузакрытый диапазон.
 */
struct SampleRegion {
    int32_t startFrame{0};
    int32_t endFrame{0};
};

enum class SamplePreviewLoopMode : uint8_t {
    Off = 0,
    On = 1
};

/**
 * @brief Опции предпрослушивания сэмпла.
 */
struct PreviewOptions {
    float gain{0.25f};   // [0..1]
    float speed{1.0f};   // [0.25..4]
};

/**
 * @brief Снимок runtime-состояния preview-движка для UI.
 */
struct SamplePreviewState {
    bool playing{false};
    float playhead01{0.0f};   // Нормализованный курсор внутри текущего region.
};

/**
 * @brief Отдельный движок предпрослушивания сэмплов (не Track/не Transport).
 *
 * Принцип:
 * - принимает готовый аудиоресурс (SharedClipBuffer);
 * - не знает о треках, FX-цепочках, секвенсоре и транспорте;
 * - микшит preview как отдельный слой в master out.
 */
struct ISamplePreviewEngine {
    virtual ~ISamplePreviewEngine() = default;

    /**
     * @brief Запустить предпрослушивание буфера в заданном регионе.
     * @param sample Готовый аудиобуфер.
     * @param region Регион в фреймах.
     * @param speed Скорость чтения материала (pitch/speed), [0.25..4.0].
     * @param loopMode Зацикливание региона.
     * @param options Доп. опции (gain/speed override).
     */
    virtual void play(const SharedClipBuffer& sample,
                      const SampleRegion& region,
                      float speed,
                      SamplePreviewLoopMode loopMode,
                      const PreviewOptions& options) noexcept = 0;

    /**
     * @brief Немедленная остановка preview.
     */
    virtual void stop() noexcept = 0;

    /**
     * @brief Обновить loop-регион/режим для текущего preview.
     */
    virtual void setLoop(const SampleRegion& region,
                         SamplePreviewLoopMode loopMode) noexcept = 0;

    /**
     * @brief RT-обработка: домиксовать preview в master out.
     * Вызывается из аудио-колбэка.
     */
    virtual void process(const AudioProcessContext& ctx) noexcept = 0;

    /**
     * @brief Быстрый флаг маршрутизации в аудио-колбэке.
     *
     * true  -> текущий блок должен обрабатываться preview-движком;
     * false -> можно отдавать блок основному engine.
     */
    virtual bool isActive() const noexcept = 0;

    /**
     * @brief Состояние для UI/control (вне RT).
     */
    virtual SamplePreviewState state() const noexcept = 0;
};

/**
 * @brief Фабрика runtime-реализации preview-движка.
 */
std::unique_ptr<ISamplePreviewEngine> MakeSamplePreviewEngine() noexcept;

} // namespace avantgarde
