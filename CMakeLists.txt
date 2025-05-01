cmake_minimum_required(VERSION 3.31)
project(throttr-sdk)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(BOOST_ALL_NO_LIB ON)
set(BUILD_SHARED_LIBS OFF)

option(BUILD_SDK_TESTS "Build SDK tests cases" ON)
option(BUILD_PROTOCOL_TESTS "Build PROTOCOL tests cases" OFF)


set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g --coverage -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")


find_package(Boost 1.87.0 REQUIRED COMPONENTS
        coroutine
        thread
        system
        charconv
)

include(FetchContent)

FetchContent_Declare(
        throttr-protocol
        GIT_REPOSITORY https://github.com/throttr/protocol.git
        GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(throttr-protocol)

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS src/include/*.hpp)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(throttr-sdk STATIC ${HEADERS} ${SOURCES})

target_include_directories(throttr-sdk
        PUBLIC
        src/include
)

target_link_libraries(throttr-sdk
        ${Boost_LIBRARIES}
        throttr::protocol
)

target_compile_options(throttr-sdk PRIVATE -static -static-libgcc -static-libstdc++)
target_link_options(throttr-sdk PRIVATE -static -static-libgcc -static-libstdc++)

if (BUILD_SDK_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS  ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc)

    add_executable(throttr-sdk-tests ${TESTS})
    target_link_libraries(throttr-sdk-tests
            throttr-sdk
            GTest::gtest_main
            throttr::protocol
    )

    target_include_directories(throttr-sdk-tests
            PRIVATE
            src/include
    )

    include(GoogleTest)
    gtest_discover_tests(throttr-sdk-tests)
endif()
