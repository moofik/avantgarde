#pragma once
#include "types.h"


/**
 * @file IControlSurface.h
 * @brief Источник событий UI (кнопки/энкодеры/поты).
 *
 * poll() выполняется в control-потоке, должен быть неблокирующим и без аллокаций.
 * Возвращает одно событие за вызов; если событий нет — возвращает false.
*/
namespace avantgarde {


// Источник событий UI: опрос кнопок/энкодеров/потов. poll() неблокирующий.
    struct IControlSurface {
        virtual ~IControlSurface() = default;

        virtual bool poll(ControlEvent &outEvent) noexcept = 0; // true, если событие получено
        virtual void attachEventBus(void *bus) noexcept = 0; // слабая привязка к сервисной шине
    };
}