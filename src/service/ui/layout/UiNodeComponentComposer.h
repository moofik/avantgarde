#pragma once

#include "contracts/UiLayout.h"
#include "contracts/UiPreparedLayout.h"
#include "contracts/UiScene.h"
#include "service/ui/layout/UiPreparedParams.h"

namespace avantgarde {

// Универсальный composer: обходит layout-ноды и собирает typed UiComponent
// из prepared-параметров. Виджету остается только подготовить params-map.
class UiNodeComponentComposer final {
public:
    static void compose(UiScene scene,
                        const UiLayoutTemplate& layoutTemplate,
                        const UiPreparedParams& params,
                        UiPreparedLayoutBuilder& builder);
};

} // namespace avantgarde
