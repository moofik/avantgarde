#include "service/ui/UiWidgetFactory.h"

#include <utility>

#include "service/ui/ManagerWidget.h"
#include "service/ui/TracksWidget.h"

namespace avantgarde {

UiWidgetFactory::UiWidgetFactory(UiWidgetFactoryOptions options)
    : options_(std::move(options)) {}

std::unique_ptr<IUiWidget> UiWidgetFactory::create(UiScene scene) const {
    switch (scene) {
        case UiScene::Tracks:
            // Основной экран: прокидываем общие параметры кадра и заголовка.
            return std::make_unique<TracksWidget>(
                TracksWidget::Options{
                    .frameWidth = options_.frameWidth,
                    .headerTitle = options_.tracksHeaderTitle,
                    .speedStep = options_.tracksSpeedStep,
                    .bpmStep = options_.tracksBpmStep,
                });
        case UiScene::Manager:
            // Файловый менеджер использует только ширину рамки.
            return std::make_unique<ManagerWidget>(options_.frameWidth);
        case UiScene::FxList:
        case UiScene::FxEditor:
        default:
            // Нереализованные сцены пока не имеют виджетов.
            return nullptr;
    }
}

} // namespace avantgarde
