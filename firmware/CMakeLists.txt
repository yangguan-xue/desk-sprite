cmake_minimum_required(VERSION 3.16)

# ESP-IDF Project Definition
if(DEFINED ENV{IDF_PATH})
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
else()
    message(STATUS "IDF_PATH not defined, ESP-IDF build rules skipped for local native builds.")
endif()

project(desk_sprite)
