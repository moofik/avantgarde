file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
        ${CMAKE_CURRENT_LIST_DIR}/*.cxx
        ${CMAKE_CURRENT_LIST_DIR}/*.cc
)

if (MODULE_SOURCES)
    add_library(avantgarde_module STATIC ${MODULE_SOURCES})
    set(_iface PUBLIC)
else()
    add_library(avantgarde_module INTERFACE
            OnePoleHPFModule.cpp)
    set(_iface INTERFACE)
endif()

target_link_libraries(avantgarde_module
        ${_iface} avantgarde_contracts
        # если  использует runtime — раскомментируй:
        # PRIVATE avantgarde_runtime
)

# GCC/ARM (в т.ч. Raspberry Pi) может требовать libatomic для __atomic_* символов
# при использовании std::atomic на 8/16-байтовых типах.
# Линкуем как PUBLIC зависимость модуля, чтобы потребители (app/tests) получали это автоматически.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(AVANTGARDE_ATOMIC_LIB atomic)
    if (AVANTGARDE_ATOMIC_LIB)
        target_link_libraries(avantgarde_module
                ${_iface} ${AVANTGARDE_ATOMIC_LIB}
        )
    endif()
endif()

target_include_directories(avantgarde_module
        ${_iface} ${CMAKE_CURRENT_LIST_DIR}/..
)
