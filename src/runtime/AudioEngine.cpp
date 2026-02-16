#include "contracts/IAudioEngine.h"
#include "contracts/IParamBridge.h"
#include "contracts/IRtCommandQueue.h"

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

            // 3) Последовательно обрабатываем треки. Никаких аллокаций здесь.
            for (auto& t : tracks_) {
                t->process(ctx);
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
        std::vector<std::unique_ptr<ITrack>> tracks_; // Только вне RT
        IRtCommandQueue* rtQueue_{nullptr};           // Владеет внешний код
        IParamBridge*    paramBridge_{nullptr};       // Владеет внешний код
        std::shared_ptr<void> audioHost_;
        double sampleRate_{48000.0};
    };

// Фабрика (без отдельного заголовка; тесты объявляют её как extern)
    std::unique_ptr<IAudioEngine> MakeAudioEngine(IRtCommandQueue* q, IParamBridge* p) {
        return std::unique_ptr<IAudioEngine>(new AudioEngine(q, p));
    }

} // namespace avantgarde
