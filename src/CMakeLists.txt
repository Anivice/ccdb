cmake_minimum_required(VERSION 3.30)
project(src)

set(CMAKE_CXX_STANDARD 23)

include_directories(./include)
add_executable(ccdb
        main.cpp
        include/log.h include/httplib.h
        mihomo.cpp
        include/mihomo.h)
target_compile_definitions(ccdb PUBLIC VERSION="0.0.1")
target_compile_options(ccdb PUBLIC -fsanitize=address -fsanitize=undefined)
target_link_libraries(ccdb PUBLIC asan ubsan atomic)

# target_compile_options(ccdb PUBLIC -fsanitize=thread -fsanitize=undefined)
# target_link_libraries(ccdb PUBLIC tsan ubsan atomic)
