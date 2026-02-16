file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if (PLATFORM_SOURCES)
    add_library(avantgarde_platform STATIC ${PLATFORM_SOURCES})
    set(_iface PUBLIC)
else()
    add_library(avantgarde_platform INTERFACE)
    set(_iface INTERFACE)
endif()

target_link_libraries(avantgarde_platform
        ${_iface} avantgarde_contracts
)
target_include_directories(avantgarde_platform
        ${_iface} ${CMAKE_CURRENT_LIST_DIR}/..
)
