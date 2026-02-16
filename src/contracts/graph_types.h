#ifndef AVANTGARDE_CONTRACTS_GRAPH_TYPES_H
#define AVANTGARDE_CONTRACTS_GRAPH_TYPES_H

#include <cstdint>
#include <cstddef>


/**
 * graph_types.h — базовые типы и инварианты для топологии DSP-графа.
 *
 * Назначение:
 *   Этот файл содержит POD-структуры (plain-old data), которые описывают
 *   топологию графа в "плоском" виде: список узлов (nodes) и рёбер (edges).
 *   Такие структуры безопасно использовать:
 *     • в RT-коде (только чтение уже зафиксированных массивов),
 *     • в кодеке сериализации/десериализации,
 *     • в тестах/моках для детерминируемых проверок.
 *
 * Дизайн-принципы:
 *   1) Никаких динамических аллокаций, строк и исключений в RT-пути.
 *   2) Детерминированный порядок: внешние представления сортируются по ID.
 *   3) Значения параметров узлов отдельно (ParamBridge); граф хранит только форму/связи.
 */
namespace avantgarde {

// Лимиты по умолчанию (если нужны другие — правим их в одном месте).
    inline constexpr std::uint32_t kMaxNodes          = 64;
    inline constexpr std::uint32_t kMaxParamsPerNode  = 32;

/** Типы идентификаторов (числовые, стабильные для сериализации/RT). */
    using NodeId   = std::uint16_t;   // уникален в пределах проекта
    using NodeKind = std::uint16_t;   // код из внешнего реестра типов (Input, FX и т.п.)

/**
 * Описание узла графа в плоском виде.
 * Инварианты:
 *  • id — уникален;
 *  • paramCount ∈ [0..kMaxParamsPerNode];
 *  • kind — внешний тип (маппинг решает приложение).
 */
    struct GraphNodeDesc {
        NodeId            id;
        NodeKind          kind;
        std::uint16_t     paramCount;
        std::uint16_t     reserved{0}; // выравнивание/расширение без слома ABI
    };

/**
 * Ориентированное ребро графа.
 * Инварианты:
 *  • fromId/toId существуют; fromId != toId;
 *  • без дублей; итоговый граф — DAG.
 */
    struct GraphEdgeDesc {
        NodeId fromId;
        NodeId toId;
    };

/**
 * Окно для массового чтения/записи топологии.
 * Вызвавший код предоставляет внешние буферы и их вместимость.
 * Реализация IGraph заполняет их и выставляет фактические счётчики.
 */
    struct GraphTopoView {
        GraphNodeDesc*    nodes;      // [out] вместимость >= *nodeCount
        GraphEdgeDesc*    edges;      // [out] вместимость >= *edgeCount
        std::uint16_t*    nodeCount;  // [in/out] capacity -> filled
        std::uint16_t*    edgeCount;  // [in/out] capacity -> filled
    };

/**
 * Требования к корректности топологии:
 *  1) Уникальные NodeId.
 *  2) Параметры узлов в лимите kMaxParamsPerNode.
 *  3) Рёбра ссылаются на существующие узлы, без петель.
 *  4) Нет дублей рёбер.
 *  5) Граф — DAG (топологически сортируем).
 *
 * Детерминизм внешнего представления:
 *  • Узлы отсортированы по id по возрастанию.
 *  • Рёбра отсортированы по (fromId, toId).
 */

} // namespace avantgarde

#endif // AVANTGARDE_CONTRACTS_GRAPH_TYPES_H
