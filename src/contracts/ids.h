#pragma once
#include <cstdint>


namespace avantgarde {


// Команды RT‑ядра (используются в RtCommand.id)
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