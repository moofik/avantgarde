#pragma once

#include <string>

namespace avantgarde {

// Результат детекта BPM для одного сэмпла.
struct BpmDetectionResult {
    bool ok{false};
    // BPM исходного файла (без speed трека).
    float sourceBpm{0.0f};
    // BPM с учетом speed/stretch трека.
    float effectiveBpm{0.0f};
    // Доверие к результату [0..1].
    float confidence{0.0f};
    // Текст ошибки при ok=false.
    std::string error{};
};

// Offline-сервис детекта BPM из аудиофайла.
// Важно:
// - вызывается только вне RT;
// - предназначен для user action "Detect BPM".
class BpmDetectorService {
public:
    BpmDetectionResult detectFromFile(const std::string& path,
                                      float trackSpeed) const;
};

} // namespace avantgarde

