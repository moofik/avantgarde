#ifndef AVANTGARDE_CONTRACTS_IAudioGraph_H
#define AVANTGARDE_CONTRACTS_IAudioGraph_H

#include <cstdint>
#include "graph_types.h"

/**
 * IAudioGraph — контракт управления топологией DSP-графа (вне RT).
 *
 * Обязанности:
 *  • Хранить и валидировать топологию.
 *  • Предоставлять плоский детерминированный снимок (GraphTopoView).
 *  • Вести ревизию (revision) для безопасной смены топологии в Engine.
 *
 * Ограничения:
 *  • Изменения выполняются на control/service-потоках (НЕ RT).
 *  • Методы noexcept; ошибки — через bool.
 */

namespace avantgarde {

    class IAudioGraph {
    public:
        virtual ~IAudioGraph() = default;

        // -------- Чтение текущей топологии --------
        // Наполняет внешний буфер детерминированным снимком (nodes/edges отсортированы).
        // Возвращает false при ошибке (некорректные указатели/вместимость).
        virtual bool getTopology(GraphTopoView& view) const noexcept = 0;

        // -------- Полная замена топологии --------
        // Выполняет полную валидацию (см. graph_types.h). False — при невалидности/циклах/выходе за лимиты.
        virtual bool setTopology(const GraphTopoView& view) noexcept = 0;

        // -------- Инкрементальные операции (опционально для v1) --------
        virtual bool addNode(const GraphNodeDesc& nd) noexcept = 0;
        virtual bool removeNode(NodeId id) noexcept = 0;
        virtual bool addEdge(const GraphEdgeDesc& e) noexcept = 0;
        virtual bool removeEdge(NodeId fromId, NodeId toId) noexcept = 0;

        // -------- Ревизии --------
        // bumpRevision: монотонно увеличивает номер ревизии (хост зовёт после смены графа).
        virtual void        bumpRevision() noexcept = 0;
        // revision: текущее значение (Engine/кеши могут реагировать на изменение).
        virtual std::uint64_t revision() const noexcept = 0;
    };

} // namespace avantgarde

#endif // AVANTGARDE_CONTRACTS_IAudioGraph_H
