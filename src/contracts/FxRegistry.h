#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "contracts/ids.h"

namespace avantgarde {

// Описание одного параметра FX в каталоге.
struct FxParamDescriptor {
    // Стабильный индекс параметра в wire/API (UiIntent::paramIndex).
    uint16_t paramIndex{0};
    // Короткий ключ параметра (для логов/диагностики).
    std::string_view key{};
    // Отображаемое имя параметра в UI.
    std::string_view label{};
    // Диапазон и дефолт.
    float minValue{0.0f};
    float maxValue{1.0f};
    float defaultValue{0.0f};
};

// Описание одного FX-профиля в реестре.
struct FxDescriptor {
    // Канонический ID профиля (используем для сохранения в состоянии трека).
    std::string_view id{};
    // Отображаемое имя для списка FX.
    std::string_view displayName{};
    // Массив параметров профиля.
    const FxParamDescriptor* params{nullptr};
    std::size_t paramCount{0};
};

// Единый реестр встроенных FX-профилей.
// Нужен как source-of-truth для:
// - резолва строковых id в канонический профиль;
// - отображения названий FX в UI;
// - выдачи списка параметров для FxEditor.
class FxRegistry final {
public:
    static constexpr std::string_view kUnknownFxId = "fx.unknown";
    static constexpr std::string_view kReverbSchroederId = "fx.reverb.schroeder";
    static constexpr std::string_view kHpfOnePoleId = "fx.filter.hpf.onepole";
    static constexpr std::string_view kStutterId = "fx.stutter.sync";
    static constexpr std::string_view kBufferFxId = "fx.buffer.superglitch";

    // Резолв ID/алиаса в descriptor. Возвращает nullptr, если профиль неизвестен.
    static const FxDescriptor* find(std::string_view id) noexcept {
        if (id == kReverbSchroederId || id == "reverb.schroeder" || id == "reverb") {
            return &reverb_;
        }
        if (id == kHpfOnePoleId || id == "hpf.onepole" || id == "hpf") {
            return &hpf_;
        }
        if (id == kStutterId || id == "stutter.sync" || id == "stutter") {
            return &stutter_;
        }
        if (id == kBufferFxId || id == "buffer" || id == "bufferfx" || id == "superglitch" || id == "glitch") {
            return &bufferFx_;
        }
        return nullptr;
    }

    // Безопасный резолв: всегда возвращает descriptor (fallback = reverb).
    static const FxDescriptor& findOrFallback(std::string_view id) noexcept {
        if (const FxDescriptor* d = find(id)) {
            return *d;
        }
        return reverb_;
    }

private:
    static inline constexpr std::array<FxParamDescriptor, 4> reverbParams_{{
        {toParamIndex(ReverbParamId::Wet), "wet", "Wet", 0.0f, 1.0f, 0.25f},
        {toParamIndex(ReverbParamId::Room), "room", "Room", 0.0f, 1.0f, 0.65f},
        {toParamIndex(ReverbParamId::Damp), "damp", "Damp", 0.0f, 1.0f, 0.30f},
        {toParamIndex(ReverbParamId::Width), "width", "Width", 0.0f, 1.0f, 0.85f},
    }};

    static inline constexpr std::array<FxParamDescriptor, 1> hpfParams_{{
        {toParamIndex(HpfParamId::Cutoff), "cutoff", "Cutoff", 0.0f, 1.0f, 0.50f},
    }};

    static inline constexpr std::array<FxParamDescriptor, 4> stutterParams_{{
        {toParamIndex(StutterParamId::Wet), "wet", "Wet", 0.0f, 1.0f, 0.70f},
        {toParamIndex(StutterParamId::Rate), "rate", "Rate", 0.0f, 1.0f, 0.65f},
        {toParamIndex(StutterParamId::Gate), "gate", "Gate", 0.0f, 1.0f, 0.60f},
        {toParamIndex(StutterParamId::Retrigger), "retrig", "Retrig", 0.0f, 1.0f, 0.00f},
    }};

    static inline constexpr std::array<FxParamDescriptor, 8> bufferFxParams_{{
        {toParamIndex(BufferFxParamId::Mix), "mix", "Mix", 0.0f, 1.0f, 0.70f},
        {toParamIndex(BufferFxParamId::SliceSize), "slice", "Slice", 0.0f, 1.0f, 0.50f},
        {toParamIndex(BufferFxParamId::Repeat), "repeat", "Repeat", 0.0f, 1.0f, 0.15f},
        {toParamIndex(BufferFxParamId::Speed), "speed", "Speed", 0.0f, 1.0f, 0.50f},
        {toParamIndex(BufferFxParamId::Jitter), "jitter", "Jitter", 0.0f, 1.0f, 0.00f},
        {toParamIndex(BufferFxParamId::BufferSize), "buffer", "Buffer", 0.0f, 1.0f, 0.30f},
        {toParamIndex(BufferFxParamId::Retrig), "retrig", "Retrig", 0.0f, 1.0f, 0.30f},
        {toParamIndex(BufferFxParamId::Reverse), "reverse", "Reverse", 0.0f, 1.0f, 0.00f},
    }};

    static inline constexpr FxDescriptor reverb_{
        kReverbSchroederId,
        "Schroeder Reverb",
        reverbParams_.data(),
        reverbParams_.size(),
    };

    static inline constexpr FxDescriptor hpf_{
        kHpfOnePoleId,
        "OnePole HPF",
        hpfParams_.data(),
        hpfParams_.size(),
    };

    static inline constexpr FxDescriptor stutter_{
        kStutterId,
        "Sync Stutter",
        stutterParams_.data(),
        stutterParams_.size(),
    };

    static inline constexpr FxDescriptor bufferFx_{
        kBufferFxId,
        "Buffer FX",
        bufferFxParams_.data(),
        bufferFxParams_.size(),
    };
};

} // namespace avantgarde
