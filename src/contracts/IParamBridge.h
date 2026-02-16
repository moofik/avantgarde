#pragma once
#include <cstddef>
#include "types.h"

/**
 * @file IParamBridge.h
 * @brief Мост параметров из Control-потока в RT-нить (двойной буфер).
 * поток независимых параметров (на разные таргеты).
 * pushParam() вызывается из control-потока часто (до 1 кГц).
 * swapBuffers() — строго в прологе аудио-блока (RT).
*/
namespace avantgarde {


// Двойной буфер: Control пишет часто в write‑сторону; RT в прологе блока делает swap.
    struct IParamBridge {
        virtual ~IParamBridge() = default;
        virtual void pushParam(Target target, std::size_t index, float value) = 0; // write‑side
        virtual void swapBuffers() = 0; // вызывается строго в прологе RT‑блока
    };


} // namespace avantgarde