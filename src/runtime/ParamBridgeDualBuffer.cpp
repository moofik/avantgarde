#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <new>

#include "contracts/types.h"            // Target, ParamMeta и пр.
#include "contracts/IParamBridge.h"     // pushParam(...), swapBuffers()
#include "contracts/IParameterized.h"   // IParameterized

namespace avantgarde {

// Двойная страница для апдейтов параметров.
// Control-тред: pushParam(...)
// RT-тред: swapBuffers() — строго один раз в прологе аудиоблока.
    class ParamBridgeDualBuffer final : public IParamBridge {
    public:
        struct Update {
            Target      target;
            std::size_t index;
            float       value01; // нормализованное [0..1]
        };

        using ResolverFn = IParameterized* (*)(Target) noexcept;

        explicit ParamBridgeDualBuffer(std::size_t capacityPerPage,
                                       ResolverFn resolver = nullptr) noexcept
                : m_capacity(capacityPerPage)
                , m_resolver(resolver)
                , m_writePage(0)
        {
            for (int i = 0; i < 2; ++i) {
                m_pages[i].reset(new (std::nothrow) Update[m_capacity]);
                m_count[i].store(0, std::memory_order_relaxed);
                m_overflow[i].store(false, std::memory_order_relaxed);
            }
            if (!m_pages[0] || !m_pages[1]) {
                // fail-soft: без исключений
                m_capacity = 0;
            }
        }

        void setResolver(ResolverFn r) noexcept { m_resolver = r; }

        // -------- IParamBridge (CONTROL) --------
        void pushParam(Target target, std::size_t index, float value) override {
            // Нормализация [0..1]
            if (value < 0.f) value = 0.f;
            else if (value > 1.f) value = 1.f;

            const int w = m_writePage.load(std::memory_order_relaxed);

            if (m_capacity == 0) {
                m_overflow[w].store(true, std::memory_order_relaxed);
                return;
            }

            std::size_t pos = m_count[w].load(std::memory_order_relaxed);
            if (pos >= m_capacity) {
                // Переполнение: фиксируем флаг и пишем в последний слот (drop-oldest).
                m_overflow[w].store(true, std::memory_order_relaxed);
                pos = m_capacity - 1;
            }

            m_pages[w][pos] = Update{target, index, value};
            // Публикуем рост длины текущей write-страницы
            m_count[w].store(pos + 1, std::memory_order_release);
        }

        // -------- IParamBridge (RT) --------
        void swapBuffers() override {
            const int w = m_writePage.load(std::memory_order_relaxed); // текущая write-страница
            const int r = w ^ 1;                                       // станет новой write после свопа

            const std::size_t readyCount = m_count[w].load(std::memory_order_acquire);
            const bool hadOverflow       = m_overflow[w].load(std::memory_order_relaxed);

            // Идемпотентность: если новых записей нет и переполнения не было — ничего не делаем.
            if (readyCount == 0 && !hadOverflow) {
                return;
            }

            // Публикуем длину именно в СТРАНИЦЕ w, которая теперь станет read-страницей!
            m_count[w].store(readyCount, std::memory_order_relaxed);
            m_overflow[w].store(hadOverflow, std::memory_order_relaxed);

            // Готовим будущую write-страницу r: чистые счетчики.
            m_count[r].store(0, std::memory_order_relaxed);
            m_overflow[r].store(false, std::memory_order_relaxed);

            // Переключаемся: новой write становится r, а w остаётся read (мы её не трогаем).
            m_writePage.store(r, std::memory_order_release);

            // Применяем апдейты из read-страницы w (если задан резолвер).
            if (m_resolver && readyCount) {
                applyReadPage_(/*page=*/w, /*count=*/readyCount);
            }
        }


        // Утилита: снять апдейты из текущей read-страницы (если применяешь вручную).
        std::size_t drainRead(Update* out, std::size_t outCapacity) noexcept {
            const int r = m_writePage.load(std::memory_order_relaxed) ^ 1;
            const std::size_t n = m_count[r].load(std::memory_order_acquire);
            const std::size_t toCopy = (outCapacity < n) ? outCapacity : n;
            for (std::size_t i = 0; i < toCopy; ++i) out[i] = m_pages[r][i];
            return toCopy;
        }

        bool readOverflowed() const noexcept {
            const int r = m_writePage.load(std::memory_order_relaxed) ^ 1;
            return m_overflow[r].load(std::memory_order_relaxed);
        }

    private:
        void applyReadPage_(int page, std::size_t count) noexcept {
            Update* data = m_pages[page].get();
            for (std::size_t i = 0; i < count; ++i) {
                IParameterized* mod = m_resolver ? m_resolver(data[i].target) : nullptr;
                if (mod) {
                    // Контракт IParameterized: RT-safe запись в локальный кэш модуля
                    mod->setParam(data[i].index, data[i].value01);
                }
            }
        }

    private:
        std::size_t                       m_capacity{0};
        ResolverFn                        m_resolver{nullptr};
        std::unique_ptr<Update[]>         m_pages[2];
        std::atomic<std::size_t>          m_count[2];
        std::atomic<bool>                 m_overflow[2];
        std::atomic<int>                  m_writePage; // 0 или 1
    };

} // namespace avantgarde
