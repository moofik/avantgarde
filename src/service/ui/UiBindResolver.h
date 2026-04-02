#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/UiAction.h"
#include "contracts/UiLayout.h"
#include "contracts/UiScene.h"

namespace avantgarde {

// Один вариант bind-а для UI-подсказок/каталога.
struct UiBindOption {
    // Короткий алиас, который пользователь может написать в шаблоне.
    std::string alias{};
    // Канонический bind-ключ, в который резолвится алиас.
    std::string canonical{};
    // Короткое объяснение назначения bind-а.
    std::string description{};
};

// Результат резолва bind-строки.
struct UiBindResolution {
    // true, если bind корректный для заданного scene/node-type.
    bool ok{false};
    // Нормализованный канонический bind.
    std::string canonical{};
    // Человеко-понятная ошибка, если ok == false.
    std::string error{};
    // Если bind адресует UiAction напрямую — здесь лежит action-id.
    UiAction::Id actionId{UiAction::Id::None};
    // Индекс параметра для action-ключей вида ...<index>.
    // -1 означает "текущий выбранный параметр".
    int32_t paramIndex{-1};
};

// Резолвер "прозрачного" bind:
// - принимает короткий алиас (например "wet");
// - выдает канонический bind (например "fx.param.wet");
// - валидирует, что bind уместен в конкретном виджете/узле.
class UiBindResolver final {
public:
    // Вернуть каталог допустимых bind-ов для конкретного scene + типа узла.
    static std::vector<UiBindOption> catalog(UiScene scene, UiLayoutNodeType nodeType);

    // Преобразовать алиас/сырой bind в канонический bind.
    // Если bind пустой, применится дефолт для данного scene + nodeType (если есть).
    static UiBindResolution resolve(UiScene scene,
                                    UiLayoutNodeType nodeType,
                                    std::string_view rawBind);
};

} // namespace avantgarde
