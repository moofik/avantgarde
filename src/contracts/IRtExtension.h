// include/contracts/IRtExtension.h
#pragma once
#include "types.h"

namespace avantgarde {

    struct IRtExtension {
        virtual ~IRtExtension() = default;
        virtual void onBlockBegin(const AudioProcessContext& ctx) noexcept = 0;
        virtual void onBlockEnd(const AudioProcessContext& ctx) noexcept = 0;
    };

} // namespace avantgarde