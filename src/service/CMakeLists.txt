file(GLOB_RECURSE SERVICE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if (SERVICE_SOURCES)
    add_library(avantgarde_service STATIC ${SERVICE_SOURCES})
    set(_iface PUBLIC)
else()
    add_library(avantgarde_service INTERFACE)
    set(_iface INTERFACE)
endif()

target_link_libraries(avantgarde_service
        ${_iface} avantgarde_contracts
)
target_include_directories(avantgarde_service
        ${_iface} ${CMAKE_CURRENT_LIST_DIR}/..
)
