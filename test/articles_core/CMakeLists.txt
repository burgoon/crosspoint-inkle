# Standalone host test for the article shelf bookkeeping. Kept out of test/CMakeLists.txt so
# upstream syncs never touch it.
#
#   cmake -S test/articles_core -B build/articles-test
#   cmake --build build/articles-test
#   ctest --test-dir build/articles-test --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(ArticlesCoreTest CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(ARTICLES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/apps_local/articles)
add_executable(ArticlesCoreTest ArticlesCoreTest.cpp ${ARTICLES_DIR}/ArticleLog.cpp)
target_include_directories(ArticlesCoreTest PRIVATE ${ARTICLES_DIR})
target_link_libraries(ArticlesCoreTest PRIVATE GTest::gtest_main)

enable_testing()
include(GoogleTest)
gtest_discover_tests(ArticlesCoreTest)
