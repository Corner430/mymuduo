cmake_minimum_required(VERSION 3.0)
project(mymuduo)

# 设置动态库的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置调试信息以及启动 C++11 语言标准，fPIC 表示生成位置无关的代码
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

# 添加 include 目录到搜索路径
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加子目录
add_subdirectory(src)
add_subdirectory(example)
