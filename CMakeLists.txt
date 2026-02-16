cmake_minimum_required(VERSION 3.20)
project(avantgarde LANGUAGES CXX)

# -------------------------------------------------------
# Опции сборки
# -------------------------------------------------------
option(AVANTGARDE_ENABLE_WERROR      "Treat warnings as errors"         OFF)
option(AVANTGARDE_ENABLE_UNITY       "Enable Unity builds"               OFF)
option(AVANTGARDE_ENABLE_ASAN        "Enable AddressSanitizer"           OFF)
option(AVANTGARDE_ENABLE_UBSAN       "Enable UndefinedBehaviorSanitizer" OFF)
option(AVANTGARDE_ENABLE_LTO         "Enable Link Time Optimization"     OFF)
option(AVANTGARDE_BUILD_TESTS        "Build tests"                       ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Общие предупреждения
if (MSVC)
    add_compile_options(/permissive- /W4)
    if (AVANTGARDE_ENABLE_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if (AVANTGARDE_ENABLE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# Санитайзеры (не мешаем MSVC)
if (NOT MSVC)
    if (AVANTGARDE_ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
    if (AVANTGARDE_ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
    endif()
endif()

# Unity build
if (AVANTGARDE_ENABLE_UNITY)
    set(CMAKE_UNITY_BUILD ON)
endif()

# LTO
if (AVANTGARDE_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
    if (ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "IPO/LTO not supported: ${ipo_msg}")
    endif()
endif()

# -------------------------------------------------------
# Таргеты по подпапкам
# -------------------------------------------------------
add_subdirectory(src/contracts)
add_subdirectory(src/runtime)
add_subdirectory(src/control)
add_subdirectory(src/service)
add_subdirectory(src/platform)
add_subdirectory(src/module)

# -------------------------------------------------------
# Тесты
# -------------------------------------------------------
if (AVANTGARDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# -------------------------------------------------------
# Executable in repo root
# -------------------------------------------------------
add_executable(avantgarde ${CMAKE_SOURCE_DIR}/main.cpp
        src/module/GainSlewModule.cpp)
target_link_libraries(avantgarde
        PRIVATE
        avantgarde_contracts
        avantgarde_runtime
        avantgarde_control
        avantgarde_service
        avantgarde_platform
        avantgarde_module
)
target_include_directories(avantgarde
        PRIVATE ${CMAKE_SOURCE_DIR}/src
)
