cmake_minimum_required(VERSION 3.16)

project(
    fooyin-dlna
    VERSION 0.1.0
    DESCRIPTION "DLNA/UPnP browser plugin for fooyin"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(dlna)
