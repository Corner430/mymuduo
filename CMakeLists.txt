cmake_minimum_required(VERSION 3.0)
project(mymuduo)

# 设置动态库的路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 设置调试信息以及启动 C++11 语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11 -fPIC")

# 定义参与编译的源代码文件 
aux_source_directory(. SRC_LIST)

# 编译生成动态库 mymuduo，SHARED 表示生成动态库
add_library(mymuduo SHARED ${SRC_LIST})