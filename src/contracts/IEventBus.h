#pragma once
#include <cstdint>
#include <functional>
#include <memory>

/**
 * Назначение: шина не‑RT событий для сервисного мира: OLED/UI, логирование,
 * сохранения/загрузки, телеметрия, питание, всплывающие подсказки, help‑оверлеи и т.д.
 *
 * Требования:
 * Множественные издатели/подписчики. Темы (строковые или TopicId), фильтры, опционально
 * sticky‑последние значения.
 * Допускаются аллокации, батчинг, очереди подписчиков раздельные (разная backpressure‑политика).
 * Поток исполнения — Service Thread (один диспетчер), либо cooperative multi‑worker (опционально).
 */
namespace avantgarde {


    using TopicId = uint32_t; // см. раздел 13


// Type‑erased конверт события для шины (вне RT)
    struct EventEnvelope {
        TopicId topic;
        const void* payload; // указатель на T; владение/копия определяются реализацией
        std::size_t payloadLen; // размер T
        uint64_t tsMono; // монотонное время публикации
    };


    struct Subscription { virtual ~Subscription() = default; virtual void unsubscribe() = 0; };
    using SubscriptionPtr = std::unique_ptr<Subscription>;


    struct IEventBus {
        virtual ~IEventBus() = default;
        virtual void publish(const EventEnvelope& ev) = 0; // вызывается из Service‑потока
        virtual SubscriptionPtr subscribe(TopicId topic,
                                          std::function<void(const EventEnvelope&)> callback) = 0; // колбэк в Service‑потоке
        virtual void setSticky(TopicId topic, const EventEnvelope& last) = 0; // последнее значение темы
        virtual bool getSticky(TopicId topic, EventEnvelope& out) const = 0;
        virtual uint64_t totalPublished() const = 0;
        virtual uint64_t totalDelivered() const = 0;
    };


}