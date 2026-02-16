# src/contracts/CMakeLists.txt

# Соберём список заголовков (для IDE/FILE_SET)
file(GLOB_RECURSE CONTRACT_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/*.h
        ${CMAKE_CURRENT_LIST_DIR}/*.hpp
)

add_library(avantgarde_contracts INTERFACE
        IRtExtension.h
        IClipTrack.h
        ITransportBridge.h)

# Дадим IDE явный файлсет заголовков и базовый include-root = src/
# (CMake ≥ 3.23: FILE_SET HEADERS поддерживается IDE, CLion точно это любит)
target_sources(avantgarde_contracts
        INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/..   # => <repo>/src
        FILES     ${CONTRACT_HEADERS}
)

# Путь инклюдов для потребителей: <repo>/src
target_include_directories(avantgarde_contracts
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/..              # => src/
)

target_compile_definitions(avantgarde_contracts
        INTERFACE AVANTGARDE_API_LEVEL=1
)
