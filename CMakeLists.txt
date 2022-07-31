cmake_minimum_required(VERSION 3.8)

project(os_model)

set(OS_LIB task.c fifo.c rtos.c io.c)

add_library(os_lib STATIC ${OS_LIB})

add_executable(main main.c)

target_link_libraries(main os_lib)

