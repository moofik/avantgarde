# test/CMakeLists.txt
include(FetchContent)

# 1) Подтянуть Catch2 v3
FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.6.0
)
FetchContent_MakeAvailable(Catch2)

# 2) Подключить CMake-модули Catch2 (чтобы работал include(Catch))
#    (важно делать ПОСЛЕ MakeAvailable)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

# 3) Собрать все тестовые .cpp (без явного добавления того же файла ещё раз)
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

if (TEST_SOURCES)
    add_executable(avantgarde_tests ${TEST_SOURCES}
            ParamBridgeDualBufferTests.cpp
            AudioEngineTests.cpp
            GainSlewModuleTests.cpp
            OnePoleHPFModuleTests.cpp)

    target_link_libraries(avantgarde_tests PRIVATE
            Catch2::Catch2WithMain      # тянет инклюды Catch2 транзитивно
            avantgarde_contracts
            avantgarde_runtime
            avantgarde_control
            avantgarde_service
            avantgarde_platform
            avantgarde_module
    )

    # если тестам нужен C++20 (на всякий случай)
    target_compile_features(avantgarde_tests PRIVATE cxx_std_20)

    include(Catch)
    catch_discover_tests(avantgarde_tests)
else()
    message(STATUS "No tests found in test/. Skipping test target.")
endif()
