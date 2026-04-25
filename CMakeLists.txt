cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(dbuf)
option(DBUF_BUILD_TESTS "Build tests" OFF)

add_library(dbuf INTERFACE)
target_include_directories(dbuf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (DBUF_BUILD_TESTS)
    if (NOT CURRENT_CPM_VERSION)
        include(cmake/get_cpm.cmake)
    endif()

    enable_testing()

    CPMAddPackage("gh:google/googletest@1.16.0")

    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "test/*.cpp")
    add_executable(dbuf_tests ${TEST_SOURCES})
    target_link_libraries(dbuf_tests PRIVATE dbuf gtest_main)

    include(GoogleTest)
    gtest_discover_tests(dbuf_tests)
endif()
