cmake_minimum_required(VERSION 3.17)
project(ex3)

set(CMAKE_CXX_STANDARD 14)

add_executable(ex3
        MapReduceFramework.h
        SampleClient.cpp
        MapReduceFramework.cpp
        )