cmake_minimum_required(VERSION 3.20)

project(itch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(HIGHFIVE_SOURCE_DIR "" CACHE PATH "Optional local HighFive source checkout")

include(CTest)
include(FetchContent)

if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}")
endif()

if(EXISTS "/opt/anaconda3/include/hdf5.h")
    list(APPEND CMAKE_PREFIX_PATH "/opt/anaconda3")
endif()

set(HIGHFIVE_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(HIGHFIVE_TEST_MDSPAN OFF CACHE BOOL "" FORCE)

find_package(HighFive QUIET)
if(NOT HighFive_FOUND AND HIGHFIVE_SOURCE_DIR)
    add_subdirectory(${HIGHFIVE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_deps/highfive-build EXCLUDE_FROM_ALL)
elseif(NOT HighFive_FOUND)
    FetchContent_Declare(
        highfive
        GIT_REPOSITORY https://githubfast.com/highfive-devs/highfive.git
        GIT_TAG v3.3.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(highfive)
endif()

add_library(itch
    src/frame_reader.cpp
    src/parser.cpp
    src/formatter.cpp
    src/order_book.cpp
)

target_include_directories(itch
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(itch
    PRIVATE
        -Wall -Wextra -Wpedantic
)

add_executable(parse_messages
    tools/parse_messages.cpp
)

target_link_libraries(parse_messages
    PRIVATE
        itch
)

target_compile_options(parse_messages
    PRIVATE
        -Wall -Wextra -Wpedantic
)

add_executable(filter
    tools/filter.cpp
)

target_link_libraries(filter
    PRIVATE
        itch
)

target_compile_options(filter
    PRIVATE
        -Wall -Wextra -Wpedantic
)

add_executable(export_l2_snapshots
    tools/export_l2_snapshots.cpp
)

target_link_libraries(export_l2_snapshots
    PRIVATE
        itch
        HighFive::HighFive
)

target_compile_options(export_l2_snapshots
    PRIVATE
        -Wall -Wextra -Wpedantic
)

if(BUILD_TESTING)
    add_executable(order_book_tests
        tests/order_book_tests.cpp
    )

    target_link_libraries(order_book_tests
        PRIVATE
            itch
    )

    target_compile_options(order_book_tests
        PRIVATE
            -Wall -Wextra -Wpedantic
    )

    add_test(
        NAME parse_test_bin_first_2000
        COMMAND parse_messages
            --input ${CMAKE_CURRENT_SOURCE_DIR}/../data/filtered/test.bin
            --limit 2000
            --log ${CMAKE_CURRENT_BINARY_DIR}/test_first_2000.log
    )
    set_tests_properties(parse_test_bin_first_2000 PROPERTIES
        PASS_REGULAR_EXPRESSION "Unknown messages: 0.*Malformed messages: 0"
    )

    add_test(
        NAME order_book_unit_tests
        COMMAND order_book_tests
    )
endif()
