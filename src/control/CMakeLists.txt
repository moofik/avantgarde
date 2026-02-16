file(GLOB_RECURSE CONTROL_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if (CONTROL_SOURCES)
    add_library(avantgarde_control STATIC ${CONTROL_SOURCES})
    set(_iface PUBLIC)
else()
    add_library(avantgarde_control INTERFACE)
    set(_iface INTERFACE)
endif()

target_link_libraries(avantgarde_control
        ${_iface} avantgarde_contracts
        # при необходимости: PRIVATE avantgarde_runtime
)
target_include_directories(avantgarde_control
        ${_iface} ${CMAKE_CURRENT_LIST_DIR}/..
)
