#include "platform/macos/MacPrimitiveWindowInput.h"

#import <AppKit/AppKit.h>

#include <deque>
#include <mutex>

#include "platform/macos/MacPrimitiveInputMapper.h"

namespace avantgarde::macos {

struct MacPrimitiveWindowInput::Impl {
    std::mutex inputMutex{};
    std::deque<UiGesture> inputQueue{};
    id keyMonitor{nil};
};

MacPrimitiveWindowInput::MacPrimitiveWindowInput()
    : impl_(std::make_unique<Impl>()) {
    __block Impl* weakImpl = impl_.get();
    impl_->keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                               handler:^NSEvent* _Nullable(NSEvent* _Nonnull event) {
        if (!weakImpl) {
            return event;
        }
        const UiGesture action = mapPrimitiveWindowEvent(event);
        if (action == UiGesture::None) {
            return event;
        }
        std::lock_guard<std::mutex> lock(weakImpl->inputMutex);
        weakImpl->inputQueue.push_back(action);
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

bool MacPrimitiveWindowInput::readNextInputEvent(UiGestureEvent& out) noexcept {
    out.action = UiGesture::None;
    if (!impl_) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->inputMutex);
    if (impl_->inputQueue.empty()) {
        return false;
    }
    out.action = impl_->inputQueue.front();
    impl_->inputQueue.pop_front();
    return true;
}

} // namespace avantgarde::macos
