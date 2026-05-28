cmake_minimum_required(VERSION 3.20)
project(prog CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED)

add_executable(prog
    src/main.cpp
    src/pp_allocator.cpp
    src/string_pool.cpp
    src/access_log.cpp
    src/table.cpp
    src/database.cpp
    src/storage_manager.cpp
    src/lexer.cpp
    src/parser.cpp
    src/executor.cpp
)

target_include_directories(prog PRIVATE include)

target_link_libraries(prog PRIVATE
    nlohmann_json::nlohmann_json
    Boost::boost
)

if(WIN32)
    target_compile_options(prog PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
