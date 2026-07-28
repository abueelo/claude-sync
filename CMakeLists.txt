cmake_minimum_required(VERSION 3.20)
project(claude-sync C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(claude_sync_core STATIC
  third_party/monocypher.c
  src/crypt.cpp
  src/git.cpp
  src/paths.cpp
  src/project.cpp
  src/config.cpp
  src/lock.cpp
  src/manifest.cpp
  src/merge.cpp
  src/mirror.cpp
  src/repo.cpp
  src/sync.cpp
)
target_include_directories(claude_sync_core PUBLIC src third_party)

add_executable(claude-sync src/main.cpp)
target_link_libraries(claude-sync PRIVATE claude_sync_core)

enable_testing()
add_executable(normalize_test tests/normalize_test.cpp)
target_link_libraries(normalize_test PRIVATE claude_sync_core)
add_test(NAME normalize COMMAND normalize_test)

add_executable(merge_test tests/merge_test.cpp)
target_link_libraries(merge_test PRIVATE claude_sync_core)
add_test(NAME merge COMMAND merge_test)

add_executable(crypt_test tests/crypt_test.cpp)
target_link_libraries(crypt_test PRIVATE claude_sync_core)
add_test(NAME crypt COMMAND crypt_test)
