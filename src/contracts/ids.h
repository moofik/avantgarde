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
        ClipTrigger   = 16, // track, index=clipId
        NoteDetune    = 17  // track, index=key, value=fine detune [-1..1]
    };

    // Базовые значения wire-протокола RtCommand.
    constexpr int16_t kRtTrackGlobal = -1;
    constexpr int16_t kRtSlotTrackParams = -1;
    constexpr int16_t kRtClipSlot0 = 0;
    constexpr uint16_t kRtIndexUnused = 0;
    constexpr float kRtValueOff = 0.0f;
    constexpr float kRtValueOn = 1.0f;
    constexpr uint16_t kRtQuantizeModeIndex = kRtIndexUnused;
    constexpr uint16_t kRtMidiNoteMin = 0;
    constexpr uint16_t kRtMidiNoteMax = 127;

    // Индексы параметров трека для CmdId::ParamSet при slot = kRtSlotTrackParams.
    //
    // ВАЖНО про режимы:
    // - TrackMode/LaunchPolicy/StopPolicy хранят "сырой policy-state" трека.
    // - UI-кнопка LOOPER не обязана вручную крутить каждый параметр:
    //   она применяет заранее заданный пресет политик (см. SamplerEngineLayer::setTrackLooperMode()).
    // - Продвинутый пользовательский тюнинг может менять параметры по отдельности.
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
        FollowTransportEnabled = 5,
        // Трековый playback mode (см. TrackPlaybackModeValue).
        PlaybackMode = 6,
        // Политика реакции на новые trigger/note-on во время уже активного проигрывания.
        LaunchPolicy = 7,
        // Политика остановки трека в режиме note-driven playback.
        StopPolicy = 8,
        // Нормализованная точка старта playback-региона [0..1].
        // 0.0 = самое начало файла.
        StartNorm = 9,
        // Нормализованная точка конца playback-региона [0..1].
        // 1.0 = самый конец файла.
        EndNorm = 10,
        // Read-only параметр для UI: текущая позиция playhead внутри trim-региона [0..1].
        // Не используется для setParam (запись игнорируется runtime-слоем).
        PlayheadNorm = 11,
        // true: трек подстраивает playbackInc от transport BPM/TS и bars (tempo sync on).
        // false: playbackInc полностью ручной и не меняется от BPM/TS.
        TempoSyncEnabled = 12
    };

    // Track playback mode:
    // - Looper: трек ориентирован на loop/клиповый playback.
    // - Note: трек ориентирован на note/step playback.
    enum class TrackPlaybackModeValue : uint8_t {
        Looper = 0,
        Note = 1
    };

    // Политика старта при новом trigger/note-on:
    // - IgnoreIfPlaying: если уже играет, новый trigger игнорируется.
    // - RetriggerOnNoteOn: новый trigger сбрасывает playhead в начало.
    enum class TrackLaunchPolicyValue : uint8_t {
        IgnoreIfPlaying = 0,
        RetriggerOnNoteOn = 1
    };

    // Политика остановки:
    // - ManualStop: останавливаем только явной командой stop.
    // - ByNoteOff: в note-режиме останавливаем по note-off активной ноты.
    enum class TrackStopPolicyValue : uint8_t {
        ManualStop = 0,
        ByNoteOff = 1
    };

    // Пользовательские профили режима трека (4 режима "из коробки").
    // Это UX-уровень: один профиль разворачивается в набор mode/policy/loop параметров.
    enum class TrackPlaybackProfileValue : uint8_t {
        // PATTERN: note-mode + loop on.
        Pattern = 0,
        // PATTERN ONCE: note-mode + loop off.
        PatternOnce = 1,
        // LOOP: looper-mode + loop on.
        Loop = 2,
        // ONESHOT: looper-mode + loop off.
        OneShot = 3
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

    // Параметры Buffer FX (super-glitch engine).
    enum class BufferFxParamId : uint16_t {
        Mix = 0,
        SliceSize = 1,
        Repeat = 2,
        Speed = 3,
        Jitter = 4,
        BufferSize = 5,
        Retrig = 6,
        Reverse = 7
    };

    enum class QuantizeCmdValue : uint8_t {
        None = 0,
        Beat = 1,
        Bar = 2
    };

    constexpr uint16_t toParamIndex(TrackParamId id) noexcept {
        return static_cast<uint16_t>(id);
    }

    constexpr float toParamValue(TrackPlaybackModeValue v) noexcept {
        return static_cast<float>(static_cast<uint8_t>(v));
    }

    constexpr float toParamValue(TrackLaunchPolicyValue v) noexcept {
        return static_cast<float>(static_cast<uint8_t>(v));
    }

    constexpr float toParamValue(TrackStopPolicyValue v) noexcept {
        return static_cast<float>(static_cast<uint8_t>(v));
    }

    constexpr float toParamValue(TrackPlaybackProfileValue v) noexcept {
        return static_cast<float>(static_cast<uint8_t>(v));
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

    constexpr uint16_t toParamIndex(BufferFxParamId id) noexcept {
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
            case CmdId::NoteDetune:     return "note_detune";
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
        if (s == "note_detune")      return NoteDetune;
        return CmdId{}; // 0 = unknown
    }
} // namespace avantgarde
