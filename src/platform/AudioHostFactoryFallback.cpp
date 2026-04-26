#include "contracts/IPlatform.h"

namespace avantgarde {

#if !defined(__APPLE__) && !defined(__linux__)
std::shared_ptr<IAudioHost> createDefaultAudioHost() {
    return nullptr;
}
#endif

} // namespace avantgarde
