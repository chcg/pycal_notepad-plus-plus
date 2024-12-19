cmake_minimum_required(VERSION 3.29)
project(pycalc)

add_definitions(-DUNICODE)

set(CMAKE_CXX_STANDARD 20)

find_package(Python3 REQUIRED COMPONENTS Development)

include_directories(${Python3_INCLUDE_DIRS})

add_library(pycalc SHARED src/library.cpp src/worker.cpp src/library.h src/library.rc)

target_link_libraries(pycalc ${Python3_LIBRARIES})

