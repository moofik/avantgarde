#include "contracts/IAudioEngine.h"
#include "contracts/IParamBridge.h"
#include "contracts/IRtCommandQueue.h"

// NEW:
#include "contracts/IRtExtension.h"
#include "contracts/IAudioRecorder.h"  // IRtRecordSink
#include "contracts/ITransport.h"      // ITransportBridge

#include <vector>
#include <memory>
#include "contracts/ids.h"
#include <utility>
#include <cstdint>

namespace avantgarde {

// Внутренний движок: чистый RT-путь, без аллокаций/блокировок/исключений в processBlock.
// Вся конфигурация — вне RT, строго по контракту IAudioEngine.
    class AudioEngine final : public IAudioEngine {
    public:
        explicit AudioEngine(IRtCommandQueue* rtQueue,
                             IParamBridge* paramBridge) noexcept
                : rtQueue_(rtQueue), paramBridge_(paramBridge) {}

        ~AudioEngine() override = default;

        // --- вне RT ---
        void registerTrack(std::unique_ptr<ITrack> track) override {
            tracks_.push_back(std::move(track));
        }

        void setSampleRate(double sr) override {
            sampleRate_ = sr;
        }

        void setAudioHost(std::shared_ptr<void> host) noexcept override {
            audioHost_ = std::move(host);
        }

        void onCommand(const Command& cmd) override {
            if (!rtQueue_) return;
            const CmdId id = parseCmdId(cmd.name);
            RtCommand rtc{};
            rtc.id    = static_cast<uint16_t>(id);
            rtc.track = static_cast<int16_t>(cmd.target.trackId);
            rtc.slot  = static_cast<int16_t>(cmd.target.slotId);
            rtc.index = 0;      // (для NoteOn/Off и др. при необходимости будем заполнять выше по стеку)
            rtc.value = cmd.value;
            (void)rtQueue_->push(rtc);
        }

        /**
         * setTransportBridge
         *
         * Подключает/отключает транспорт (глобальный музыкальный state).
         *
         * ВАЖНО:
         *  - предпочтительно вызывать ВНЕ RT (до старта стрима).
         *  - transport bridge не владеется движком.
         */
        void setTransportBridge(ITransportBridge* t) noexcept override {
            transport_ = t;
        }

        /**
         * addRtExtension
         *
         * Регистрирует RT-расширение (хуки onBlockBegin/onBlockEnd).
         *
         * Ограничения:
         *  - вызывать только ВНЕ RT (до старта аудиострима)
         *  - фиксированный лимит, без аллокаций
         */
        void addRtExtension(IRtExtension* ext) noexcept override {
            if (!ext) return;
            if (rtExtCount_ >= kMaxRtExtensions) return;
            rtExt_[rtExtCount_++] = ext;
        }

        /**
         * setMasterRecordSink
         *
         * Подключает/отключает RT-safe sink для записи MASTER OUT.
         *
         * ВАЖНО:
         *  - sink должен быть RT-safe: writeBlock() без аллокаций/блокировок/исключений.
         *  - предпочтительно вызывать ВНЕ RT (до старта).
         *
         * Поведение:
         *  - если sink == nullptr → запись отключена.
         */
        void setMasterRecordSink(IRtRecordSink* sink) noexcept override {
            masterSink_ = sink;
        }

        // --- RT-путь ---
        void processBlock(const AudioProcessContext& ctx) override {
            // 1) Считываем все накопившиеся RT-команды.
            RtCommand rc{};
            while (rtQueue_ && rtQueue_->pop(rc)) {
                handleRtCommand(rc);
            }

            // 2) Атомарный своп параметров — строго в прологе блока.
            if (paramBridge_) {
                paramBridge_->swapBuffers();
            }

            // 3) Транспорт — строго в прологе блока (после параметров).
            //    RT читает снапшот, затем увеличивает sampleTime.
            if (transport_) {
                transport_->swapBuffers();
                transport_->advanceSampleTime(static_cast<uint64_t>(ctx.nframes));
            }

            // 4) RT extensions — пролог блока (квантизация/секвенсор потом сюда).
            for (uint32_t i = 0; i < rtExtCount_; ++i) {
                rtExt_[i]->onBlockBegin(ctx);
            }

            // 5) Последовательно обрабатываем треки. Никаких аллокаций здесь.
            for (auto& t : tracks_) {
                t->process(ctx);
            }

            // 6) RT extensions — эпилог блока.
            for (uint32_t i = 0; i < rtExtCount_; ++i) {
                rtExt_[i]->onBlockEnd(ctx);
            }

            // 7) Запись master out (если подключен sink).
            // Важно: предполагаем, что ctx.out указывает на итоговый мастер-буфер.
            if (masterSink_) {
                (void)masterSink_->writeBlock(ctx.out, static_cast<int>(ctx.nframes));
            }
        }

    private:
        // Минимальная RT-обработка команд: зарезервировано под транспорт/квантизацию.
        void handleRtCommand(const RtCommand& rc) noexcept {
            const int t = rc.track;
            if (t >= 0 && static_cast<std::size_t>(t) < tracks_.size()) {
                tracks_[t]->onRtCommand(rc); // RT-чисто, без аллокаций
            } else {
                // TODO: master / глобальные команды (track == -1) — обработать здесь или в MasterTrack
            }
        }

    private:
        static constexpr uint32_t kMaxRtExtensions = 8;

        std::vector<std::unique_ptr<ITrack>> tracks_; // Только вне RT
        IRtCommandQueue* rtQueue_{nullptr};           // Владеет внешний код
        IParamBridge*    paramBridge_{nullptr};       // Владеет внешний код
        ITransportBridge* transport_{nullptr};        // NEW: транспорт (не владеем)

        std::shared_ptr<void> audioHost_;
        double sampleRate_{48000.0};

        // NEW: RT extensions (фиксированный массив, без аллокаций)
        IRtExtension* rtExt_[kMaxRtExtensions]{};
        uint32_t rtExtCount_{0};

        // NEW: мастер-синк для записи (не владеем)
        IRtRecordSink* masterSink_{nullptr};
    };

// Фабрика (без отдельного заголовка; тесты объявляют её как extern)
    std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue* q, IParamBridge* p) {
        return std::unique_ptr<IAudioEngine>(new AudioEngine(q, p));
    }

} // namespace avantgarde
