cmake_minimum_required(VERSION 3.20)
project(hot_update LANGUAGES CXX VERSION 1.0.0)

# Hot-update agent: a client whose functional part is replaced at run time
# without restarting, and an update server that pushes new versions.
# No third-party runtime dependencies: sockets, SHA-256 and the module loader are
# implemented here.
#
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
#   scripts/demo.sh

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NX_BUILD_TESTS "Build the unit and end-to-end tests" ON)
option(NX_SANITIZE "Build with ASan + UBSan (not on MSVC)" OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
  # Static runtime: binaries without extra DLL dependencies.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  if(NX_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

find_package(Threads REQUIRED)

# --- common: protocol, SHA-256, sockets, versions, logging -------------------
add_library(nx_common STATIC
  src/common/sha256.cpp
  src/common/protocol.cpp
  src/common/net.cpp
  src/common/version.cpp
  src/common/log.cpp
  src/common/files.cpp)
target_include_directories(nx_common PUBLIC include)
target_link_libraries(nx_common PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(nx_common PUBLIC ws2_32)
endif()

# --- the functional part: one shared library, built twice --------------------
function(nx_functional target version)
  add_library(${target} SHARED src/functional/functional.cpp)
  target_include_directories(${target} PRIVATE include)
  target_compile_definitions(${target} PRIVATE NX_BUILDING_FUNCTIONAL NX_FUNCTIONAL_VERSION="${version}")
  set_target_properties(${target} PROPERTIES PREFIX "" NO_SONAME ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
endfunction()
nx_functional(functional_v1 "1.0.0")
nx_functional(functional_v2 "2.0.0")

# --- client --------------------------------------------------------------------
add_library(nx_client STATIC src/client/module_host.cpp src/client/update_agent.cpp)
target_link_libraries(nx_client PUBLIC nx_common ${CMAKE_DL_LIBS})
add_executable(nx-client src/client/main.cpp)
target_link_libraries(nx-client PRIVATE nx_client)

# --- server --------------------------------------------------------------------
add_library(nx_server STATIC src/server/update_server.cpp)
target_link_libraries(nx_server PUBLIC nx_common)
add_executable(nx-server src/server/main.cpp)
target_link_libraries(nx-server PRIVATE nx_server)

# --- publisher (uploads a new functional part to the server) -----------------
add_library(nx_publisher STATIC src/publisher/publish.cpp)
target_link_libraries(nx_publisher PUBLIC nx_common)
add_executable(nx-publish src/publisher/main.cpp)
target_link_libraries(nx-publish PRIVATE nx_publisher)

# The functional libraries sit next to the executables for the demo.
foreach(t nx-client nx-server nx-publish)
  add_dependencies(${t} functional_v1 functional_v2)
endforeach()

# --- tests ---------------------------------------------------------------------
if(NX_BUILD_TESTS)
  enable_testing()
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
  endif()
  add_executable(nx_tests
    tests/test_sha256.cpp tests/test_protocol.cpp tests/test_version.cpp
    tests/test_module_host.cpp tests/test_end_to_end.cpp)
  target_link_libraries(nx_tests PRIVATE nx_client nx_server nx_publisher GTest::gtest_main)
  target_compile_definitions(nx_tests PRIVATE
    NX_TEST_V1="$<TARGET_FILE:functional_v1>" NX_TEST_V2="$<TARGET_FILE:functional_v2>")
  add_dependencies(nx_tests functional_v1 functional_v2)
  include(GoogleTest)
  gtest_discover_tests(nx_tests DISCOVERY_TIMEOUT 30)
endif()
