add_executable(benchmark-reading)
target_sources(benchmark-reading PRIVATE ${CMAKE_CURRENT_LIST_DIR}/benchmark-reading.cpp)
target_link_libraries(benchmark-reading PRIVATE indexed_bzip2 Threads::Threads)
target_compile_features(benchmark-reading PRIVATE cxx_std_17)
