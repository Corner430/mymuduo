# 开发日志

#### 1 CMakeLists.txt 和 .gitignore 文件的编写

#### 2 noncopyable 类的编写，禁止拷贝构造和赋值构造

**作为基类**

#### 3 Logger （日志）类的编写

**单例模式**

1. 多行的宏可以使用 `do { ... } while(0)` 包裹。
    - 语法完整性
    - 单语句行为
    - 避免变量作用域冲突
2. 可变参宏的编写
3. 提供给外部的接口：`LOG_INFO`, `LOG_ERROR`, `LOG_FATAL`, `LOG_DEBUG`

#### 4 Timestamp 类的编写

**时间戳**

1. `std::chrono::system_clock::now()` 获取当前时间
2. `std::chrono::system_clock::to_time_t()` 将时间转换为 `time_t`
3. `std::chrono::system_clock::from_time_t()` 将 `time_t` 转换为时间
4. `std::chrono::duration_cast<std::chrono::milliseconds>()` 将时间转换为毫秒