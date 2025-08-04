cmake_minimum_required(VERSION 2.8.12...3.29)
project(fsst-plus)

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g ")
set(CMAKE_CXX_STANDARD 11)

include_directories(third_party/duckdb/src/include)
link_directories(third_party/duckdb/build/release/src)

include_directories(third_party/fsst)
add_subdirectory(third_party/fsst)

include_directories(src/cleaving)
include_directories(src/util)
include_directories(src/block)

add_executable(fsst_plus src/fsst_plus.cpp)
target_link_libraries(fsst_plus duckdb fsst)

add_executable(compress_w_basic_fsst src/compress_w_basic_fsst.cpp)
target_link_libraries(compress_w_basic_fsst duckdb fsst)

# TEST #
#add_executable(cleaving_test test/cleaving_test.cpp)
#target_link_libraries(cleaving_test PRIVATE duckdb fsst Catch2::Catch2WithMain)
#
#add_executable(binary_search_test test/binary_search_test.cpp)
#target_link_libraries(binary_search_test PRIVATE Catch2::Catch2WithMain)
#
#add_executable(sizing_test test/sizing_test.cpp)
#target_link_libraries(sizing_test PRIVATE duckdb fsst Catch2::Catch2WithMain)
#
#add_executable(fsst_plus_test test/fsst_plus_test.cpp)
#target_link_libraries(fsst_plus_test PRIVATE duckdb fsst Catch2::Catch2WithMain)

# Catch2
Include(FetchContent)

FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0 # or a later release
)

FetchContent_MakeAvailable(Catch2)



