#pragma once
#include "types.h"

/**
 * @file IControlHandler.h
 * @brief Обработчик жестов/событий UI → действия/команды/параметры.
 *
 * Здесь живёт логика страниц, маппинга крутилок, long/comb press и т.п.
*/
namespace avantgarde {

    struct IControlHandler {
        virtual ~IControlHandler() = default;
        virtual void handle(const ControlEvent& ev) = 0; // вызывает onCommand()/pushParam()/publish()
    };

} // namespace avantgarde
