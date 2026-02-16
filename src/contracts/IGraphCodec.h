#ifndef AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H
#define AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H

#include <cstdint>
#include <cstddef>
#include "IAudioGraph.h"

/**
 * IGraphCodec — сериализация/десериализация топологии (вне RT).
 * Формат данных (JSON/CBOR/…) — деталь реализации; контракт фиксирует поведение.
 * Гарантии:
 *  • Детерминированный вывод (узлы по id, рёбра по (from,to)).
 *  • Сопоставимость через schemaTag.
 */
namespace avantgarde {

    struct GraphCodecConfig {
        const char*  schemaTag;   // например, "avantgarde.project@1" (nullptr -> значение по умолчанию)
        bool         prettyPrint; // форматировать вывод (необязательно)
    };

    class IGraphCodec {
    public:
        virtual ~IGraphCodec() = default;

        // IGraph → bytes (например, JSON).
        // Пишет в заранее выделенный буфер outBuf (ёмкость outCapacity), возвращает фактический размер outSize.
        virtual bool serialize(const IAudioGraph& graph,
                               const GraphCodecConfig& cfg,
                               char* outBuf, std::size_t outCapacity,
                               std::size_t& outSize) const noexcept = 0;

        // bytes → IGraph. Парсит/валидирует и применяет setTopology().
        virtual bool deserialize(const char* data, std::size_t size,
                                 const GraphCodecConfig& cfg,
                                 IAudioGraph& outGraph) const noexcept = 0;
    };

} // namespace avantgarde

/**
 * коротко и по делу — при схеме track1 → master, track2 → master граф у нас выполняет всего три задачи: декларация,
 * валидация и привязка ID. Никакой «магии маршрутизации» он не делает.
 *
 * Что делает граф в такой топологии
 *
 * Декларация состава микшера.
 *
 * Это просто список узлов (Track и один Master) и рёбра Track→Master. Он лежит в проекте и сериализуется детерминированно.
 * Валидация перед запуском
 *
 * Проверка: уникальные id, только разрешённые kind, только Track→Master, без дублей/петель. Это
 * ловит ошибки конфигурации ещё до RT.
 *
 * Привязка идентификаторов.
 * NodeId используется как стабильный ключ:
 * IAudioEngine::bindTrack(nodeId, ITrack*) — связать трек-объект с узлом графа;
 * адресация параметров через ParamBridge как (trackNodeId → baseOffset) + (moduleIndex, paramIndex).
 */

#endif // AVANTGARDE_CONTRACTS_IGRAPH_CODEC_H
