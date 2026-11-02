# Standalone host test for the game rules. Kept out of test/CMakeLists.txt so
# upstream syncs never touch it.
#
#   cmake -S test/inkle_core -B build/inkle-test
#   cmake --build build/inkle-test
#   ctest --test-dir build/inkle-test --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(InkleCoreTest CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(INKLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps_local/inkle)
add_executable(InkleCoreTest InkleCoreTest.cpp ${INKLE_DIR}/InkleCore.cpp ${INKLE_DIR}/InkleWords.cpp ${INKLE_DIR}/InkleState.cpp)
target_include_directories(InkleCoreTest PRIVATE ${INKLE_DIR})
target_link_libraries(InkleCoreTest PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(InkleCoreTest)
