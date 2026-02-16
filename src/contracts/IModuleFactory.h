#pragma once
#include <memory>
#include <string>
#include "IAudioModule.h"


namespace avantgarde {


// Создание модулей по строковому id; позволяет подменять реализации (builtin/FAUST/LV2).
    struct IModuleFactory {
        virtual ~IModuleFactory() = default;
        virtual std::unique_ptr<IAudioModule> create(const std::string& id) = 0; // вне RT
        virtual const ModuleDescriptor& getDescriptor(const std::string& id) const = 0; // вне RT
    };


} // namespace avantgarde