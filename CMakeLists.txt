cmake_minimum_required(VERSION 3.00.0)
project(pipe C)

add_library(pipe INTERFACE pipe.h pipe_atomic.h)

# Include directories.
target_include_directories(pipe INTERFACE ./)

# Link thread library.
set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)
if (CMAKE_USE_PTHREADS_INIT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
endif ()
target_link_libraries(pipe ${CMAKE_THREAD_LIBS_INIT})
