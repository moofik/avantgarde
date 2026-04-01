#include "contracts/IPlatform.h"

namespace avantgarde {

#if !defined(__APPLE__)
std::shared_ptr<IAudioHost> createDefaultAudioHost() {
    return nullptr;
}
#endif

} // namespace avantgarde

