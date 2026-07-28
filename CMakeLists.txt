cmake_minimum_required(VERSION 3.20)
project(csync CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(csync_core STATIC
  src/git.cpp
  src/paths.cpp
  src/project.cpp
  src/config.cpp
  src/lock.cpp
  src/manifest.cpp
  src/mirror.cpp
  src/repo.cpp
  src/sync.cpp
)
target_include_directories(csync_core PUBLIC src third_party)

add_executable(csync src/main.cpp)
target_link_libraries(csync PRIVATE csync_core)

enable_testing()
add_executable(normalize_test tests/normalize_test.cpp)
target_link_libraries(normalize_test PRIVATE csync_core)
add_test(NAME normalize COMMAND normalize_test)
