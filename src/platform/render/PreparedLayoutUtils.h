#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "contracts/UiPreparedLayout.h"

namespace avantgarde::render {

// Быстрый индекс компонент по id для prepared-layout кадра.
// Значения - сырые указатели, владеет ими UiPreparedLayout.
using UiComponentIndex = std::unordered_map<std::string, const IUiComponent*>;

// Рекурсивно добавляет компонент и вложенные компоненты функциональных view.
void collectComponentsById(const IUiComponent* component, UiComponentIndex& out);

// Строит полный id->component индекс для кадра.
UiComponentIndex buildComponentIndex(const UiPreparedLayout& prepared);

// Оценка внутренней высоты (в символах), если виджет не задал frameHeightHint.
uint16_t estimateInnerHeight(const UiPreparedLayout& prepared,
                             uint16_t minInnerHeight = 12U,
                             uint16_t baseRows = 8U);

// Префикс-маркер для строк list-компонента.
std::string markerPrefix(UiListComponent::Marker marker, bool selected);

} // namespace avantgarde::render

