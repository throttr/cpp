cmake_minimum_required(VERSION 3.25)
project(throttr-sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(BOOST_ALL_NO_LIB ON)
set(BUILD_SHARED_LIBS OFF)

option(BUILD_SDK_TESTS "Build SDK tests cases" ON)
option(BUILD_SDK_BENCHMARK "Build SDK BENCHMARK" ON)

set(RUNTIME_VALUE_SIZE "UINT16" CACHE STRING "Set value type for Throttr (UINT8, UINT16, UINT32, UINT64)")
set_property(CACHE RUNTIME_VALUE_SIZE PROPERTY STRINGS UINT8 UINT16 UINT32 UINT64)

message(STATUS "Using RUNTIME_VALUE_SIZE = ${RUNTIME_VALUE_SIZE}")

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g --coverage -fprofile-arcs -ftest-coverage")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CTEST_ARGUMENTS "--output-on-failure")

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
        GIT_TAG 5.0.0
)
FetchContent_MakeAvailable(throttr-protocol)

add_library(throttr-sdk INTERFACE)
target_include_directories(throttr-sdk INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(throttr-sdk INTERFACE cxx_std_23)

target_compile_definitions(throttr-sdk INTERFACE
        THROTTR_VALUE_SIZE_${RUNTIME_VALUE_SIZE}
)


target_link_libraries(throttr-sdk INTERFACE
        ${Boost_LIBRARIES}
        throttr::protocol
)

include(CMakePackageConfigHelpers)

install(TARGETS throttr-sdk
        EXPORT throttr-sdkTargets
)

install(EXPORT throttr-sdkTargets
        FILE throttr-sdkTargets.cmake
        NAMESPACE throttr::
        DESTINATION lib/cmake/throttr-sdk
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/throttr-sdkConfigVersion.cmake"
        VERSION 5.1.1
        COMPATIBILITY SameMajorVersion
)

install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/throttr-sdkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/throttr-sdkConfigVersion.cmake"
        DESTINATION lib/cmake/throttr-sdk
)

if (BUILD_SDK_BENCHMARK)
    add_executable(benchmark benchmark.cpp)
    target_compile_options(benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_options(benchmark PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(benchmark PRIVATE throttr-sdk ${Boost_LIBRARIES})
endif()

if (BUILD_SDK_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    file(GLOB_RECURSE TEST_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc)

    add_executable(throttr-sdk-tests ${TEST_FILES})

    target_compile_options(throttr-sdk-tests PRIVATE --coverage -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(throttr-sdk-tests PRIVATE --coverage)

    target_link_libraries(throttr-sdk-tests
            GTest::gtest_main
            throttr-sdk
            throttr::protocol
    )

    include(GoogleTest)
    gtest_discover_tests(throttr-sdk-tests)

    target_compile_definitions(throttr-sdk-tests PRIVATE
            THROTTR_VALUE_SIZE_${RUNTIME_VALUE_SIZE}
    )
endif()

add_library(throttr::sdk ALIAS throttr-sdk)
