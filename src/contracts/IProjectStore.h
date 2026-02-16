#pragma once
#include "types.h"
#include <string>


/**
 * @file IProjectStore.h
 * @brief Сохранение/загрузка состояния проекта (graph + params + WAV).
 *
 * Реализация определяет формат project.json и пути к ресурсам.
*/
namespace avantgarde {


    struct ProjectState; // forward‑declare, чтобы не тянуть зависимости


// Хранилище проекта: сериализация графа, параметров и WAV/аудио‑данных.
    struct IProjectStore {
        virtual ~IProjectStore() = default;
        virtual void save(const ProjectState& state, const std::string& path) = 0; // вне RT
        virtual ProjectState load(const std::string& path) = 0; // вне RT
        virtual void attachAudioHost(void* host) noexcept = 0; // для выбора устройства/пути
    };


} // namespace avantgarde
