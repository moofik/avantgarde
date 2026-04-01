file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if(APPLE)
    list(APPEND PLATFORM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacAudioHost.mm
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacGbWindowRenderer.mm
    )
else()
    # На non-Apple платформах Objective-C++ исходники не компилируем.
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX ".*/macos/.*")
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
            ${CMAKE_CURRENT_LIST_DIR}/macos/MacGbWindowRenderer.mm
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
    )
endif()
