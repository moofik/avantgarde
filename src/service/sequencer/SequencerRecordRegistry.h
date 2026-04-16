#pragma once

#include <cstdint>

#include "contracts/ISequencer.h"
#include "contracts/UiIntent.h"

namespace avantgarde {

/**
 * @brief Единый реестр допустимых UiIntent для записи в секвенсор.
 *
 * Зачем нужен:
 * - чтобы в lane не попадал служебный UI-шум (ARM toggle, scene navigation и т.п.);
 * - чтобы список "что можно писать в секвенсор" редактировался в одном месте;
 * - чтобы mapping intent -> automation/event был централизован и прозрачен.
 *
 * Правило:
 * - если intent отсутствует в allow-list, он не пишется в sequencer lanes.
 */
class SequencerRecordRegistry final {
public:
    /// true, если intent разрешен для записи в AutomationLane.
    static bool isAutomationIntent(UiIntentType type) noexcept;
    /// true, если intent разрешен для записи в EventLane.
    static bool isEventIntent(UiIntentType type) noexcept;

    /// Преобразовать intent в automation target. Возвращает false, если intent запрещен/неподдержан.
    static bool mapAutomationTarget(const UiIntent& intent,
                                    SequencerParamTarget& outTarget) noexcept;
    /// Преобразовать intent в event lane событие. Возвращает false, если intent запрещен/неподдержан.
    static bool mapEvent(const UiIntent& intent,
                         uint64_t sampleTime,
                         EventLaneEvent& outEvent) noexcept;
};

} // namespace avantgarde

