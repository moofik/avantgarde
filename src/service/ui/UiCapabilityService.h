#pragma once

#include <string>
#include <string_view>

#include "contracts/IUi.h"
#include "contracts/UiLayout.h"
#include "contracts/UiNavState.h"
#include "contracts/UiScene.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

/**
 * @brief Сводка доступности bind/target для текущего UI-контекста.
 *
 * Это отдельный capability/availability-слой, чтобы виджеты могли
 * не гадать по косвенным признакам, а явно узнать:
 * - поддерживается ли bind/target на уровне контракта;
 * - доступно ли чтение bind прямо сейчас;
 * - активна ли запись target в текущем состоянии UI.
 */
struct UiCapabilityState {
    // true, если bind является валидным canonical-ключом.
    bool bindSupported{false};
    // true, если bind можно читать в текущем runtime/nav контексте.
    bool bindAvailable{false};
    // true, если target поддерживается контрактом write-path.
    bool targetSupported{false};
    // true, если target активен сейчас (например, выбран трек/FX).
    bool targetActive{false};
    // true, если в текущем контексте есть выбранный трек.
    bool hasSelectedTrack{false};
    // true, если в текущем контексте есть выбранный существующий FX-слот.
    bool hasSelectedFx{false};
    // Короткая причина недоступности (если есть).
    std::string reason{};
};

/**
 * @brief Capability-сервис доступности bind/target и UI-условий.
 */
class UiCapabilityService final {
public:
    // Есть ли выбранный трек в текущем состоянии.
    static bool hasSelectedTrack(const UiState& state, const UiNavState& nav) noexcept;
    // Есть ли выбранный существующий FX-слот у текущего трека.
    static bool hasSelectedFx(const UiState& state, const UiNavState& nav) noexcept;

    // Поддерживается ли canonical bind-ключ контрактом.
    static bool isBindSupported(std::string_view bindCanonical, std::string& errorOut);
    // Доступен ли bind для чтения в текущем контексте.
    static bool isBindAvailable(UiScene scene,
                                std::string_view bindCanonical,
                                const UiState& state,
                                const UiNavState& nav) noexcept;

    // Поддерживается ли canonical target-ключ контрактом.
    static bool isTargetSupported(std::string_view targetCanonical, std::string& errorOut) noexcept;
    // Активен ли target для записи в текущем контексте.
    static bool isTargetActive(UiScene scene,
                               std::string_view targetCanonical,
                               const UiState& state,
                               const UiNavState& nav) noexcept;

    // Полная capability-сводка для ноды/действия.
    static UiCapabilityState resolve(UiScene scene,
                                     std::string_view bindCanonical,
                                     std::string_view targetCanonical,
                                     const UiState& state,
                                     const UiNavState& nav);

    // Проверка декларативного visible_if / state.if условия.
    // Если condition пустой, возвращается defaultValue.
    // Поддерживаемые ключи:
    // - "track.selected.exists"
    // - "fx.selected.exists"
    // - "bind.supported" / "bind.available"
    // - "target.supported" / "target.active"
    // - любое bool-значение из UiPreparedParams.flag (точное совпадение ключа).
    // Поддерживается префикс отрицания: "!<condition>".
    static bool evaluateCondition(UiScene scene,
                                  const UiLayoutNode& node,
                                  std::string_view condition,
                                  const UiState& state,
                                  const UiNavState& nav,
                                  const UiPreparedParams& params,
                                  bool defaultValue = true) noexcept;
};

} // namespace avantgarde
