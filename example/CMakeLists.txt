# 查找示例源文件
set(EXAMPLE_SOURCE example.cpp)

# 添加可执行文件
add_executable(example ${EXAMPLE_SOURCE})

# 链接主程序库和动态库
target_link_libraries(example mymuduo)

# 包含头文件
target_include_directories(example PRIVATE ${PROJECT_SOURCE_DIR}/include)

# 设置可执行文件的输出目录
set_target_properties(example PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/example)