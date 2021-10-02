cmake_minimum_required(VERSION 3.14)

project("Ratarmount++" CXX)


option(CODE_COVERAGE "Enable coverage reporting" OFF)


set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})


add_compile_options(
    "$<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Wshadow>"
    "$<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Wshadow;-Wunused;-Wsuggest-override>"
    "$<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:-O0;-g;--coverage>"
    "-D_FILE_OFFSET_BITS=64"
)

add_link_options(
    "$<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${CODE_COVERAGE}>>:--coverage>"
)


find_package(FUSE REQUIRED)
find_package(Threads REQUIRED)

# This command should be in the source directory root because ctest expects
# to find a test file in the build directory root.
include(CTest)

add_library(cxxopts INTERFACE)
target_include_directories(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include)
target_sources(cxxopts INTERFACE ${CMAKE_SOURCE_DIR}/external/cxxopts/include/cxxopts.hpp)

add_library(indexed_bzip2 INTERFACE)
target_include_directories(indexed_bzip2 INTERFACE ${CMAKE_SOURCE_DIR}/external/indexed_bzip2/)

add_subdirectory(external/SQLiteCpp)


add_subdirectory(src)


# Add convenience custom targets

add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --extra-verbose)

add_custom_target(check-memcheck
    COMMAND ${CMAKE_CTEST_COMMAND} --extra-verbose --force-new-ctest-process --test-action memcheck
    COMMAND cat "${CMAKE_BINARY_DIR}/Testing/Temporary/MemoryChecker.*.log"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

#add_dependencies(check all_tests)
#add_dependencies(check-memcheck all_tests)
