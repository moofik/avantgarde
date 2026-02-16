#pragma once
#include <cstddef>
#include "types.h"


namespace avantgarde {


// Единый интерфейс доступа к параметрам модулей. Значения нормализованы [0..1].
    struct IParameterized {
        virtual ~IParameterized() = default;
        virtual std::size_t getParamCount() const = 0;
        virtual float getParam(std::size_t index) const = 0; // O(1), RT‑safe
        virtual void setParam(std::size_t index, float value) = 0; // запись в локальный кэш
        virtual const ParamMeta& getParamMeta(std::size_t index) const = 0; // вне RT

        // === Новое: батч-обновление параметров (опционально) ===
        // По умолчанию — безопасный цикл через setParam; noexcept; RT-safe; без аллокаций.
        virtual void setParamsBatch(const ParamKV* kvs, std::size_t count) noexcept {
            for (std::size_t i = 0; i < count; ++i) {
                setParam(kvs[i].index, kvs[i].value);
            }
        }

        // === Новое: граница аудио-блока ===
        // Вызывается RT-нитью в начале каждого блока для атомарного swap write→read
        // или иной подготовки локальных кэшей. По умолчанию — no-op; noexcept.
        virtual void beginBlock() noexcept {}
    };


} // namespace avantgarde