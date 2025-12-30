cmake_minimum_required(VERSION 3.14)
project(gcodeviewer LANGUAGES C)
include(FetchContent)
FetchContent_Declare(
  raylib
  URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

file(GLOB SRC_CXX CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cxx"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.c"
)
set(MAIN_CXX "${CMAKE_CURRENT_SOURCE_DIR}/../src/main.c")
list(REMOVE_ITEM SRC_CXX "${MAIN_CXX}")
add_executable(gcodeviewer ${SRC_CXX} ${MAIN_CXX})
add_executable(test_selected ${SRC_CXX} "../src/selected.c")
target_compile_definitions(test_selected PRIVATE TESTING)

target_link_libraries(gcodeviewer PRIVATE raylib)
