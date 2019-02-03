cmake_minimum_required(VERSION 3.0)

project(
    hvd
)

add_library(hvd SHARED hvd.c)
target_link_libraries(hvd avcodec avutil)
install(TARGETS hvd DESTINATION lib)
install(FILES hvd.h DESTINATION include)

#add_executable(hve-encode-raw-h264 examples/hve_encode_raw_h264.c)
#target_link_libraries(hve-encode-raw-h264 hve)

