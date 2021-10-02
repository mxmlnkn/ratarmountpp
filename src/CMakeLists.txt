add_subdirectory(tools)

add_executable(ratarmount++)
target_sources(ratarmount++ PRIVATE ${CMAKE_CURRENT_LIST_DIR}/ratarmount++.cpp)
target_link_libraries(ratarmount++ PRIVATE cxxopts indexed_bzip2 SQLiteCpp ${FUSE_LIBRARIES})
target_compile_features(ratarmount++ PRIVATE cxx_std_17)
