file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
        ${CMAKE_CURRENT_LIST_DIR}/*.mm
        ${CMAKE_CURRENT_LIST_DIR}/*.m
)

add_library(avantgarde_platform STATIC ${PLATFORM_SOURCES})

target_link_libraries(avantgarde_platform
        PUBLIC avantgarde_contracts
)

target_include_directories(avantgarde_platform
        PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..
)

# 👇 ВАЖНО: если CLion CMake ломается на OBJCXX
# принудительно компилируем .mm как C++ с режимом objective-c++
set_source_files_properties(
        ${CMAKE_CURRENT_LIST_DIR}/macos/MacAudioHost.mm
        PROPERTIES
        LANGUAGE CXX
        COMPILE_OPTIONS "-x;objective-c++"
)

if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/macos/MacGbWindowRenderer.mm)
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
