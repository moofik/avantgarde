file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if (RUNTIME_SOURCES)
    add_library(avantgarde_runtime STATIC ${RUNTIME_SOURCES})
    set(_iface PUBLIC)
else()
    add_library(avantgarde_runtime INTERFACE
            ParamBridgeDualBuffer.cpp
            AudioEngine.cpp)
    set(_iface INTERFACE)
endif()

target_link_libraries(avantgarde_runtime
        ${_iface} avantgarde_contracts
)
target_include_directories(avantgarde_runtime
        ${_iface} ${CMAKE_CURRENT_LIST_DIR}/..
)
