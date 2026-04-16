#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "contracts/UiPreparedLayout.h"

namespace avantgarde {

// Логический уровень HUD-уведомления.
enum class HudNotificationLevel : uint8_t {
    Info = 0,
    Action,
    Critical
};

// Канонические события HUD (единый реестр для showHud(EventId, ...)).
enum class HudEventId : uint8_t {
    SnapshotCaptured = 0,
    SnapshotApplied
};

// Аргументы события HUD (placeholder payload).
struct HudEventPayload {
    int slot{0};
    std::string text{};
};

// Слой HUD-уведомлений:
// - хранит очередь сообщений,
// - резолвит сообщения из event-registry + hud.json,
// - отдает готовый UiHudOverlayView на текущий момент времени.
class HudNotificationsLayer final {
public:
    HudNotificationsLayer();

    // Загрузить конфиг HUD из json-файла.
    // При ошибке конфигурация не меняется.
    bool loadConfigFromFile(const std::string& path, std::string& errorOut);

    // Очистить очередь/активное уведомление.
    void clear();

    // API верхнего уровня: показать уведомление по каноническому событию.
    void notify(HudEventId eventId, const HudEventPayload& payload = {});

    // Прямой показ текста (для ad-hoc сценариев).
    void notifyText(std::string text,
                    HudNotificationLevel level = HudNotificationLevel::Info);

    // Снимок HUD-оверлея для рендера в момент nowMs.
    UiHudOverlayView view(uint64_t nowMs);

private:
    struct HudAnimationSpec {
        std::string type{"fade"};
        uint32_t durationMs{200};
        float fromOpacity{0.0f};
        float toOpacity{1.0f};
        float fromScale{1.0f};
        float toScale{1.0f};
        float fromGlow{0.0f};
        float toGlow{0.0f};
        float glitchTo{0.0f};
    };

    struct HudStyleSpec {
        uint16_t width{20};
        uint16_t height{5};
        uint16_t padding{1};
        UiHudPosition position{UiHudPosition::TopCenter};
        std::string font{"gothic"};
        float fontSize{0.0f};
        UiLayoutAlign align{UiLayoutAlign::Center};
        UiLayoutJustify justify{UiLayoutJustify::Center};
        bool textWrap{true};
        std::vector<std::string> textAnimationRefs{};
        std::string textColor{"#E3D4E8"};
        std::string borderColor{"#8F6E95"};
        std::string backgroundColor{"#130D16D8"};
    };

    struct HudTypeSpec {
        HudNotificationLevel level{HudNotificationLevel::Info};
        std::string styleRef{"hud.base"};
        std::string enterRef{"fade_in"};
        std::string exitRef{"fade_out"};
        uint32_t lifetimeMs{1300};
        uint32_t holdMs{900};
    };

    struct HudEventSpec {
        std::string typeRef{"info"};
        std::string textTemplate{};
    };

    struct HudConfig {
        HudTypeSpec defaults{};
        std::unordered_map<std::string, HudStyleSpec> styles{};
        std::unordered_map<std::string, HudAnimationSpec> animations{};
        std::unordered_map<std::string, UiLayoutNode::EffectSpec> textAnimations{};
        std::unordered_map<std::string, HudTypeSpec> types{};
        std::unordered_map<std::string, HudEventSpec> events{};
    };

    struct RuntimeItem {
        UiHudOverlayView baseView{};
        uint64_t startMs{0U};
        uint32_t enterMs{180U};
        uint32_t holdMs{900U};
        uint32_t exitMs{250U};
        std::string enterAnim{"fade_in"};
        std::string exitAnim{"fade_out"};
    };

    static HudConfig defaultConfig_();
    static uint64_t nowMs_();

    static std::string eventKey_(HudEventId eventId);
    static std::string levelKey_(HudNotificationLevel level);
    static std::string normalizeRef_(std::string ref,
                                     const char* atPrefix,
                                     const char* plainPrefix);
    static std::string applyPayload_(std::string templ, const HudEventPayload& payload);

    RuntimeItem buildRuntimeItemFromEvent_(std::string eventKey,
                                           const HudEventPayload& payload,
                                           uint64_t nowMs) const;
    RuntimeItem buildRuntimeItemFromText_(std::string text,
                                          HudNotificationLevel level,
                                          uint64_t nowMs) const;

    UiHudOverlayView evaluate_(const RuntimeItem& item, uint64_t nowMs, bool& finished) const;

private:
    mutable std::mutex mutex_{};
    HudConfig config_{};
    std::deque<RuntimeItem> queue_{};
    std::optional<RuntimeItem> active_{};
};

} // namespace avantgarde
