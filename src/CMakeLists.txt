# 查找所有源文件
file(GLOB SOURCES "*.cc")

# 查找所有头文件
file(GLOB HEADERS "*.h")

# 包含头文件
include_directories(${PROJECT_SOURCE_DIR}/include)

# 编译生成动态库 mymuduo，SHARED 表示生成动态库
add_library(mymuduo SHARED ${SOURCES})

# 设置库的输出目录
set_target_properties(mymuduo PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${LIBRARY_OUTPUT_PATH})
