#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "contracts/IClipTrack.h"

namespace avantgarde {

/**
 * @brief In-memory пул preloaded клипов по clipRefId.
 *
 * Цель:
 * - загружать WAV один раз (вне RT);
 * - хранить декодированные planar буферы в памяти;
 * - быстро назначать буфер треку без IO через loadSlotFromBuffer().
 */
class ClipBufferPool final {
public:
    /**
     * @brief Загрузить WAV в пул под заданным clipRefId.
     * @param clipRefId Идентификатор клипа в проекте/паттерне.
     * @param path Путь до WAV-файла.
     * @param errorOut Текст ошибки (опционально).
     * @return true при успешной загрузке и сохранении в пул.
     */
    bool loadFromFile(uint32_t clipRefId, const std::string& path, std::string* errorOut = nullptr);
    /**
     * @brief Положить заранее подготовленный буфер в пул.
     * @param clipRefId Идентификатор клипа.
     * @param buffer Разделяемый буфер.
     * @return true если буфер валиден и сохранен.
     */
    bool put(uint32_t clipRefId, const SharedClipBuffer& buffer);
    /**
     * @brief Проверить наличие буфера в пуле.
     * @param clipRefId Идентификатор клипа.
     * @return true если буфер есть.
     */
    bool contains(uint32_t clipRefId) const noexcept;
    /**
     * @brief Получить копию дескриптора буфера.
     * @param clipRefId Идентификатор клипа.
     * @param out Выходной буфер-дескриптор.
     * @return true если буфер найден.
     */
    bool get(uint32_t clipRefId, SharedClipBuffer& out) const noexcept;
    /**
     * @brief Удалить буфер из пула.
     * @param clipRefId Идентификатор клипа.
     * @return true если буфер существовал и удален.
     */
    bool erase(uint32_t clipRefId) noexcept;
    /**
     * @brief Количество буферов в пуле.
     */
    std::size_t size() const noexcept;

    /**
     * @brief Быстро назначить preloaded клип в слот трека.
     * @param track Целевой IClipTrack.
     * @param slot Индекс слота (обычно 0 для MVP).
     * @param clipRefId Идентификатор клипа.
     * @return true если clipRef найден в пуле и успешно применен в track.
     */
    bool bindClipToTrack(IClipTrack& track, uint32_t slot, uint32_t clipRefId) const;

private:
    std::unordered_map<uint32_t, SharedClipBuffer> buffers_{};
};

} // namespace avantgarde

