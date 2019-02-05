cmake_minimum_required(VERSION 3.0)

project(
    hvd
)

add_library(hvd SHARED hvd.c)
target_link_libraries(hvd avcodec avutil)
install(TARGETS hvd DESTINATION lib)
install(FILES hvd.h DESTINATION include)

add_executable(hvd-decoding-example examples/hvd_decoding_example.c)
target_link_libraries(hvd-decoding-example hvd)

