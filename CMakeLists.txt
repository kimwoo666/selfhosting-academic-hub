cmake_minimum_required(VERSION 3.14)
project(academic-hub-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

# ============================================================
# Dependencies
# ============================================================
include(FetchContent)

# Crow ??REST + WebSocket framework (header-only)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG        v1.2.0
)

# nlohmann/json ??JSON for Modern C++ (header-only)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(crow json)

# System libraries
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

# ============================================================
# Target
# ============================================================
add_executable(academic-hub-server
    src/main.cpp
)

target_include_directories(academic-hub-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(academic-hub-server PRIVATE
    Crow::Crow
    nlohmann_json::nlohmann_json
    Threads::Threads
    CURL::libcurl
    SQLite::SQLite3
)

target_compile_definitions(academic-hub-server PRIVATE
    CROW_ENFORCE_WS_SPEC
)

add_executable(config-smoke-test
    tests/config_smoke_test.cpp
)

target_include_directories(config-smoke-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(config-smoke-test PRIVATE
    nlohmann_json::nlohmann_json
    CURL::libcurl
)

add_test(NAME config-smoke-test COMMAND config-smoke-test)

add_executable(database-smoke-test
    tests/database_smoke_test.cpp
)

target_include_directories(database-smoke-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(database-smoke-test PRIVATE
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

add_test(NAME database-smoke-test COMMAND database-smoke-test)
