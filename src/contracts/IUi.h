#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "ITransport.h"

namespace avantgarde {

enum class UiTrackState : uint8_t {
    Empty = 0,
    Stopped,
    Playing,
    Recording
};

struct UiTransportState {
    bool playing{false};
    float bpm{120.0f};
    uint8_t tsNum{4};
    uint8_t tsDen{4};
    QuantizeMode quant{QuantizeMode::Bar};
    uint8_t activeTrack{0};
    uint64_t sampleTime{0};
};

struct UiTrackStateView {
    uint8_t id{0};
    UiTrackState state{UiTrackState::Empty};
    uint32_t bars{4};
    float stretchRatio{1.0f};
    float gain01{1.0f};
    bool loop{false};
    uint8_t fxCount{0};
    std::string clipName;
};

struct UiTelemetryState {
    uint64_t totalCallbacks{0};
    uint64_t xruns{0};
    bool rtQueueOverflow{false};
};

struct UiState {
    UiTransportState transport{};
    std::array<UiTrackStateView, 2> tracks{};
    UiTelemetryState telemetry{};
};

struct IUiRenderer {
    virtual ~IUiRenderer() = default;
    virtual void render(const UiState& state) = 0;
};

} // namespace avantgarde
