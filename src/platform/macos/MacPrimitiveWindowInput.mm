#include "platform/macos/MacPrimitiveWindowInput.h"

#import <AppKit/AppKit.h>

#include <deque>
#include <mutex>

#include "platform/macos/MacPrimitiveInputMapper.h"

namespace avantgarde::macos {

struct MacPrimitiveWindowInput::Impl {
    std::mutex inputMutex{};
    std::deque<PrimitiveInputEvent> inputQueue{};
    id keyMonitor{nil};
};

MacPrimitiveWindowInput::MacPrimitiveWindowInput()
    : impl_(std::make_unique<Impl>()) {
    __block Impl* weakImpl = impl_.get();
    impl_->keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:(NSEventMaskKeyDown | NSEventMaskKeyUp)
                                                               handler:^NSEvent* _Nullable(NSEvent* _Nonnull event) {
        if (!weakImpl) {
            return event;
        }
        const PrimitiveInputEvent ev = mapPrimitiveWindowEvent(event);
        if (ev.control == PrimitiveControl::None) {
            return event;
        }
        std::lock_guard<std::mutex> lock(weakImpl->inputMutex);
        weakImpl->inputQueue.push_back(ev);
        return nil;
    }];
}

MacPrimitiveWindowInput::~MacPrimitiveWindowInput() {
    if (!impl_) {
        return;
    }
    if (impl_->keyMonitor) {
        [NSEvent removeMonitor:impl_->keyMonitor];
        impl_->keyMonitor = nil;
    }
}

bool MacPrimitiveWindowInput::readNextInputEvent(PrimitiveInputEvent& out) noexcept {
    out = PrimitiveInputEvent{};
    if (!impl_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->inputMutex);
    if (impl_->inputQueue.empty()) {
        return false;
    }
    out = impl_->inputQueue.front();
    impl_->inputQueue.pop_front();
    return true;
}

} // namespace avantgarde::macos
