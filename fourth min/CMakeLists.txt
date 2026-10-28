# 下列基础配置须按此顺序保留，确保 CMake 正确初始化 ESP-IDF 工程。
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(third)
