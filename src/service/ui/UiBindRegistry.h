#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "contracts/UiLayout.h"
#include "contracts/UiScene.h"
#include "service/ui/UiBindTypes.h"

namespace avantgarde {

/**
 * @brief Каталог bind-ключей и alias-ов.
 *
 * Роль registry:
 * - хранит доступные alias -> canonical для разных UI-контекстов;
 * - выдает дефолтный bind для узла;
 * - проверяет, поддерживается ли canonical bind как публичный ключ.
 *
 * Важно:
 * - canonical bind валиден сам по себе (namespace-first);
 * - scene/nodeType используются только как "уместность в данном узле".
 */
class UiBindRegistry final {
public:
    static const UiBindRegistry& instance();

    std::vector<UiBindOption> catalog(UiScene scene, UiLayoutNodeType nodeType) const;

    bool tryResolveAlias(UiScene scene,
                         UiLayoutNodeType nodeType,
                         std::string_view normalizedKey,
                         std::string& canonicalOut) const;

    std::string defaultCanonical(UiScene scene, UiLayoutNodeType nodeType) const;

    bool isCanonicalSupported(std::string_view canonical, std::string& errorOut) const;

private:
    struct CatalogEntry {
        UiScene scene{UiScene::Tracks};
        UiLayoutNodeType nodeType{UiLayoutNodeType::Unknown};
        std::string defaultCanonical{};
        std::vector<UiBindOption> options{};
    };

    UiBindRegistry();

    const CatalogEntry* findEntry_(UiScene scene, UiLayoutNodeType nodeType) const noexcept;

    std::vector<CatalogEntry> entries_{};
};

} // namespace avantgarde
