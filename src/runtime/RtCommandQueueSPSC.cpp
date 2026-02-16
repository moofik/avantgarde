#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "contracts/IRtCommandQueue.h" // из CONTRACTS.md, раздел 11

namespace avantgarde {

// Однописатель / одночитатель. Без аллокаций внутри push/pop, строго noexcept.
// В конструкторе — единоразовая аллокация буфера фиксированного размера (power-of-two).
    class RtCommandQueueSPSC final : public IRtCommandQueue {
    public:
        // capacity обязана быть степенью двойки (для mask-индексации).
        explicit RtCommandQueueSPSC(std::size_t capacityPow2 = 1024)
                : m_capacity(normalizePow2(capacityPow2)),
                  m_mask(m_capacity - 1),
                  m_buffer(m_capacity)
        {
            // индексы и флаги уже по умолчанию = 0
        }

        // Producer (Control-поток)
        bool push(const RtCommand& cmd) noexcept override {
            const std::size_t w = m_writeIndex.load(std::memory_order_relaxed);
            const std::size_t r = m_readIndex.load(std::memory_order_acquire);

            // Классическая SPSC: максимум m_capacity - 1 элементов.
            if (UNLIKELY((w - r) >= (m_capacity - 1))) {
                m_overflow.store(true, std::memory_order_relaxed);
                return false; // полный
            }
            m_buffer[w & m_mask] = cmd;
            m_writeIndex.store(w + 1, std::memory_order_release);
            return true;
        }

        // Consumer (RT-поток)
        bool pop(RtCommand& out) noexcept override {
            const std::size_t r = m_readIndex.load(std::memory_order_relaxed);
            const std::size_t w = m_writeIndex.load(std::memory_order_acquire);

            if (r == w) {
                return false; // пусто
            }

            out = m_buffer[r & m_mask];
            m_readIndex.store(r + 1, std::memory_order_release);
            return true;
        }

        void clear() noexcept override {
            // Сбрасываем рид к райту: «моментально опустошить».
            const std::size_t w = m_writeIndex.load(std::memory_order_acquire);
            m_readIndex.store(w, std::memory_order_release);
            m_overflow.store(false, std::memory_order_relaxed);
        }

        std::size_t capacity() const noexcept override { return m_capacity; }

        std::size_t size() const noexcept override {
            const std::size_t w = m_writeIndex.load(std::memory_order_acquire);
            const std::size_t r = m_readIndex.load(std::memory_order_acquire);
            return w - r; // 0..m_capacity-1
        }

        bool overflowFlagAndReset() noexcept override {
            return m_overflow.exchange(false, std::memory_order_relaxed);
        }

    private:
        static constexpr bool isPow2(std::size_t x) noexcept { return x && ((x & (x - 1)) == 0); }
        static std::size_t normalizePow2(std::size_t x) noexcept {
            if (isPow2(x)) return x;
            // округление вверх до ближайшей степени двойки (минимум 2)
            std::size_t p = 1;
            while (p < x) p <<= 1;
            return (p < 2) ? 2 : p;
        }

        // Небольшой хинт для предсказателя ветвлений
        static inline bool UNLIKELY(bool v) noexcept {
#if defined(__GNUC__) || defined(__clang__)
            return __builtin_expect(static_cast<long>(v), 0);
#else
            return v;
#endif
        }

        // Выравнивания против false sharing
        alignas(64) std::atomic<std::size_t> m_writeIndex{0};
        alignas(64) std::atomic<std::size_t> m_readIndex{0};

        const std::size_t            m_capacity;
        const std::size_t            m_mask;
        std::vector<RtCommand>       m_buffer; // фиксированный размер

        alignas(64) std::atomic<bool> m_overflow{false};
    };

} // namespace avantgarde
