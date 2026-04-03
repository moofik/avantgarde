#pragma once
#include <cstdint>
#include <string_view>


namespace avantgarde {


// Команды RT-ядра (используются в RtCommand.id)
    enum class CmdId : uint16_t {
        Play = 1,
        Stop = 2,
        StopQuantized = 3,
        RecArm = 4,
        RecDisarm = 5,
        Overdub = 6,
        ParamSet = 7,
        Clear = 8,
        QuantizeMode = 9,

        // --- hooks ---
        Continue      = 10,
        SetTempoBpm   = 11,
        SetTimeSig    = 12, // index=den, value=num
        SetLoopRegion = 13, // index=start(lo16), value=end
        NoteOn        = 14, // track, index=key, value=vel
        NoteOff       = 15, // track, index=key
        ClipTrigger   = 16  // track, index=clipId
    };

    // Базовые значения wire-протокола RtCommand.
    constexpr int16_t kRtTrackGlobal = -1;
    constexpr int16_t kRtSlotTrackParams = -1;
    constexpr int16_t kRtClipSlot0 = 0;
    constexpr uint16_t kRtIndexUnused = 0;
    constexpr float kRtValueOff = 0.0f;
    constexpr float kRtValueOn = 1.0f;
    constexpr uint16_t kRtQuantizeModeIndex = kRtIndexUnused;

    // Индексы параметров трека для CmdId::ParamSet при slot = kRtSlotTrackParams.
    enum class TrackParamId : uint16_t {
        Gain01 = 0,
        LoopEnabled = 1,
        PlaybackInc = 2,
        // Mute-гейт трека: 1.0 = muted, 0.0 = unmuted.
        MuteEnabled = 3,
        // Arm-флаг трека: 1.0 = armed, 0.0 = disarmed.
        ArmEnabled = 4,
        // Режим запуска:
        // 1.0 = follow global transport.playing
        // 0.0 = one-shot gate через CmdId::Play/CmdId::Stop (preview voice)
        FollowTransportEnabled = 5
    };

    // Индексы параметров транспорта для IParameterized-поверхности global target:
    // Target{trackId=kRtTrackGlobal, slotId=kRtSlotTrackParams}.
    enum class TransportParamId : uint16_t {
        Playing = 0,
        TempoNorm = 1,      // нормализованный tempo [0..1] -> [20..300] BPM
        QuantizeNorm = 2,   // [0..1] -> None/Beat/Bar
        TimeSigNumNorm = 3, // [0..1] -> [1..32]
        TimeSigDenNorm = 4, // [0..1] -> {1,2,4,8,16,32}
        Swing01 = 5
    };

    // Общие параметры FX-слота (не зависят от типа эффекта).
    enum class FxCommonParamId : uint16_t {
        // 1.0 = FX включен, 0.0 = bypass (слот остается в цепи).
        Enabled = 60000
    };

    // Параметры встроенного Schroeder reverb.
    // Эти индексы используются в UiIntent::paramIndex для SetFxParam.
    enum class ReverbParamId : uint16_t {
        Wet = 0,
        Room = 1,
        Damp = 2,
        Width = 3
    };

    // Параметры встроенного OnePole HPF.
    enum class HpfParamId : uint16_t {
        Cutoff = 0
    };

    // Параметры stutter модуля.
    enum class StutterParamId : uint16_t {
        Wet = 0,
        Rate = 1,
        Gate = 2,
        Retrigger = 3
    };

    enum class QuantizeCmdValue : uint8_t {
        None = 0,
        Beat = 1,
        Bar = 2
    };

    constexpr uint16_t toParamIndex(TrackParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toParamIndex(TransportParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toParamIndex(FxCommonParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toParamIndex(ReverbParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toParamIndex(HpfParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toParamIndex(StutterParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr uint16_t toWireCmdId(CmdId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr CmdId fromWireCmdId(uint16_t raw) noexcept {
        return static_cast<CmdId>(raw);
    }

    // Темы сервисной шины (используются в EventBus.TopicId)
    enum class Topic : uint32_t {
        UiStatus           = 1001, // транспорт, BPM, quant
        UiBanner           = 1002, // всплывающие сообщения
        UiPage             = 1003, // текущая страница/FX
        MetersUpdate       = 2001, // уровни/пики
        PowerBatteryLow    = 3001, // питание
        ProjectSaveRequest = 4001,
        ProjectSaveDone    = 4002,
        TelemetryRtAlert   = 5001  // переполнения, xruns
    };

    constexpr const char* cmdIdToCStr(CmdId id) noexcept {
        switch (id) {
            case CmdId::Play:           return "play";
            case CmdId::Stop:           return "stop";
            case CmdId::StopQuantized:  return "stop_quantized";
            case CmdId::RecArm:         return "rec_arm";
            case CmdId::RecDisarm:      return "rec_disarm";
            case CmdId::Overdub:        return "overdub";
            case CmdId::ParamSet:       return "param_set";
            case CmdId::Clear:          return "clear";
            case CmdId::QuantizeMode:   return "quantize";
            case CmdId::Continue:       return "continue";
            case CmdId::SetTempoBpm:    return "set_tempo_bpm";
            case CmdId::SetTimeSig:     return "set_timesig";
            case CmdId::SetLoopRegion:  return "set_loop_region";
            case CmdId::NoteOn:         return "note_on";
            case CmdId::NoteOff:        return "note_off";
            case CmdId::ClipTrigger:    return "clip_trigger";
            default:                    return "";
        }
    }

// Разбор строкового имени в CmdId.
// ВАЖНО: это вне-RT утилита (строковые сравнения); RT-дорожка должна оперировать уже готовым id.
    inline CmdId parseCmdId(std::string_view s) noexcept {
        using enum CmdId;
        if (s == "play")             return Play;
        if (s == "stop")             return Stop;
        if (s == "stop_quantized")   return StopQuantized;
        if (s == "rec_arm")          return RecArm;
        if (s == "rec_disarm")       return RecDisarm;
        if (s == "overdub")          return Overdub;
        if (s == "param_set")        return ParamSet;
        if (s == "clear")            return Clear;
        if (s == "quantize")         return QuantizeMode;
        if (s == "continue")         return Continue;
        if (s == "set_tempo_bpm")    return SetTempoBpm;
        if (s == "set_timesig")      return SetTimeSig;
        if (s == "set_loop_region")  return SetLoopRegion;
        if (s == "note_on")          return NoteOn;
        if (s == "note_off")         return NoteOff;
        if (s == "clip_trigger")     return ClipTrigger;
        return CmdId{}; // 0 = unknown
    }
} // namespace avantgarde
