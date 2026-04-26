file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if(APPLE)
    list(APPEND PLATFORM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacAudioHost.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveWindowInput.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveWindowRenderer.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveInputMapper.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveScenePainter.mm
    )
else()
    # На non-Apple платформах Objective-C++ исходники не компилируем.
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*/macos/.*")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux-specific platform backend.
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*/linux/.*")
endif()

add_library(avantgarde_platform STATIC ${PLATFORM_SOURCES})

target_link_libraries(avantgarde_platform
        PUBLIC avantgarde_contracts
)

target_include_directories(avantgarde_platform
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..
)

# 👇 ВАЖНО: если CLion CMake ломается на OBJCXX
# принудительно компилируем .mm как C++ с режимом objective-c++
if(APPLE)
    set_source_files_properties(
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacAudioHost.mm
            PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS "-x;objective-c++"
    )

    set_source_files_properties(
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveWindowInput.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveWindowRenderer.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveInputMapper.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacPrimitiveScenePainter.mm
            PROPERTIES
            LANGUAGE CXX
            COMPILE_OPTIONS "-x;objective-c++"
    )
endif()

if(APPLE)
    target_link_libraries(avantgarde_platform PRIVATE
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreAudio"
            "-framework CoreFoundation"
            "-framework AppKit"
            "-framework QuartzCore"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(ALSA REQUIRED)
    target_link_libraries(avantgarde_platform PRIVATE ALSA::ALSA)
endif()
